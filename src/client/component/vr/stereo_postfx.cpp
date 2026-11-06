#include <std_include.hpp>
#include "settings.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

// MWR camera motion blur (r_mbLimit, default 2; the renderer settings file
// reapplies it after config.cfg) reprojects each eye against the previous
// frame, which is the other eye, and builds its tile buffers at the desktop
// size: both eyes get a smeared, offset rectangle (NVG, hunted intro). It stays
// off while VR presents and the player's value returns when VR is disabled.
//
// FX lens flares (fx_flare) are screen-space sprites: each eye draws them at
// the same view position, so they ride with the head instead of their light
// (player report; mock killhouse: the desk-lamp flare sits at 52%/53% of both
// eyes). Overlord turns them into world quads (native_flare, H2 emit
// 0x14042E816 / draw 0x140295118, not rebound); until then
// vr_disableLensFlare turns the native flares off while VR presents.
namespace vr::stereo_postfx
{
	namespace
	{
		std::optional<int> saved_limit;
		std::optional<bool> saved_flare;
		std::uint64_t forced{}, flares_forced{};
		game::dvar_t* disable_flares{};

		void update_flares(bool vr)
		{
			auto* flare = game::Dvar_FindVar("fx_flare");
			if (!flare) return;
			const bool off = vr && disable_flares && disable_flares->current.enabled;
			if (off && flare->current.enabled)
			{
				if (!saved_flare) saved_flare = flare->current.enabled;
				flare->current.enabled = flare->latched.enabled = false;
				++flares_forced;
			}
			else if (!off && saved_flare)
			{
				flare->current.enabled = flare->latched.enabled = *saved_flare;
				saved_flare.reset();
			}
		}

		void update()
		{
			const auto* enabled = game::Dvar_FindVar("vr_enable");
			const bool vr = enabled && enabled->current.enabled;
			update_flares(vr);
			const auto* limit = game::Dvar_FindVar("r_mbLimit");
			if (!limit) return;
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
			disable_flares = dvars::register_bool(settings::disable_lens_flare, true, game::DVAR_FLAG_SAVED,
				"Turn off native lens flares while VR presents (MWR draws them screen-space, locked to the view)");
			scheduler::loop(update, scheduler::pipeline::main, 250ms);
			command::add("vr_postfx_status", []
			{
				console::info("[VR postfx] motion blur forced off %llu times, saved limit %d; lens flares forced off %llu times (vr_disableLensFlare=%d)\n",
					forced, saved_limit.value_or(-1), flares_forced, disable_flares ? int(disable_flares->current.enabled) : -1);
			});
		}
	};
}

REGISTER_COMPONENT(vr::stereo_postfx::component)
