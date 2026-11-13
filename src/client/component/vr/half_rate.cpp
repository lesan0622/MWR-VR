#include <std_include.hpp>
#include "half_rate.hpp"
#include "settings.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

// CPU/GPU relief for slower PCs (user request): with vr_halfRate each OpenXR
// frame starts two display periods after the previous one, so the native frame
// (which the render thread gates) runs at half the headset refresh and the
// runtime reprojects the missing slots. com_maxfps is not used: it limits in
// whole milliseconds (60 -> 62.5 fps), which drifts against a 60 Hz slot grid.
// Visible trade-off: reprojection artefacts in fast motion.
namespace vr::half_rate
{
	namespace
	{
		game::dvar_t* enabled{};
		std::atomic_int64_t period{};
		std::atomic_bool applied{};
		std::chrono::steady_clock::time_point last_wait{};
		std::atomic_uint64_t paced{}, slept_us{};

		void update()
		{
			const auto* vr = game::Dvar_FindVar("vr_enable");
			const auto ns = period.load(std::memory_order_relaxed);
			applied.store(enabled && enabled->current.enabled && vr && vr->current.enabled &&
			              ns > 1'000'000 && ns < 100'000'000, std::memory_order_relaxed);
		}
	}

	void observe_period(const std::int64_t period_ns) noexcept
	{
		period.store(period_ns, std::memory_order_relaxed);
	}

	bool active() noexcept
	{
		return applied.load(std::memory_order_relaxed);
	}

	void before_wait() noexcept
	{
		const auto ns = period.load(std::memory_order_relaxed);
		if (active() && last_wait.time_since_epoch().count() != 0)
		{
			// Wake one and a half periods after the last wait returned: xrWaitFrame
			// then blocks to the slot boundary two periods after it.
			const auto target = last_wait + std::chrono::nanoseconds(ns + ns / 2);
			const auto now = std::chrono::steady_clock::now();
			if (target > now && target - now < std::chrono::nanoseconds(2 * ns))
			{
				std::this_thread::sleep_until(target);
				++paced;
				slept_us += std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(target - now).count());
			}
		}
	}

	void after_wait() noexcept
	{
		last_wait = std::chrono::steady_clock::now();
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			enabled = dvars::register_bool(settings::half_rate.name, settings::half_rate.default_value,
				game::DVAR_FLAG_SAVED,
				"Render the game at half the headset refresh rate; the VR runtime reprojects the other frames (less CPU/GPU load)");
			scheduler::loop(update, scheduler::pipeline::main, 250ms);
			command::add("vr_half_rate_status", []
			{
				console::info("[VR half rate] enabled=%d applied=%d period_us=%lld paced=%llu slept_us=%llu\n",
					enabled ? int(enabled->current.enabled) : -1, int(applied.load()), period.load() / 1000,
					paced.load(), slept_us.load());
			});
		}
	};
}

REGISTER_COMPONENT(vr::half_rate::component)
