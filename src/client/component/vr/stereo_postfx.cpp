#include <std_include.hpp>
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

// MWR camera motion blur (r_mbLimit, default 2; the renderer settings file
// reapplies it after config.cfg) reprojects each eye against the previous
// frame, which is the other eye, and builds its tile buffers at the desktop
// size: both eyes get a smeared, offset rectangle (NVG, hunted intro). It stays
// off while VR presents and the player's value returns when VR is disabled.
namespace vr::stereo_postfx
{
	namespace
	{
		std::optional<int> saved_limit;
		std::uint64_t forced{};

		void update()
		{
			const auto* enabled = game::Dvar_FindVar("vr_enable");
			const auto* limit = game::Dvar_FindVar("r_mbLimit");
			if (!limit) return;
			const bool vr = enabled && enabled->current.enabled;
			if (vr && limit->current.integer != 0)
			{
				if (!saved_limit) saved_limit = limit->current.integer;
				command::execute("set r_mbLimit 0");
				++forced;
			}
			else if (!vr && saved_limit)
			{
				command::execute("set r_mbLimit " + std::to_string(*saved_limit));
				saved_limit.reset();
			}
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			scheduler::loop(update, scheduler::pipeline::main, 250ms);
			command::add("vr_postfx_status", []
			{
				console::info("[VR postfx] motion blur forced off %llu times, saved limit %d\n", forced, saved_limit.value_or(-1));
			});
		}
	};
}

REGISTER_COMPONENT(vr::stereo_postfx::component)
