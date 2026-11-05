#include <std_include.hpp>
#include "weapon_carry_runtime.hpp"
#include "native_ammunition.hpp"
#include "physical_reload_runtime.hpp"
#include "tube_runtime.hpp"
#include "hands/position_offset.hpp"
#include "../directional_ui.hpp"
#include "../native_waypoints.hpp"
#include "../engine_stereo_bridge.hpp"
#include "../head_pose_bridge.hpp"
#include "../controller_input.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

// MWR has no ported native weapon-widget LUI tree (Overlord's weapon_hud), so
// each held gun's loaded/reserve count is drawn as world ink just above the
// wrist that holds it: camera-facing, a fixed world size, both eyes at the
// wrist's depth. Values are copied on the server thread; nothing is written.
// By default a counter shows only while its gun's magazine is out, or while a
// tube-fed gun's loading hand holds a shell (and briefly after either ends);
// vr_ammoCounterAlways keeps the original always-on counter.
namespace vr::gameplay::ammo_counter
{
	namespace
	{
		struct sample
		{
			bool valid{};
			int hand{-1}, loaded{}, reserve{};
			std::uint32_t weapon{};
			bool reloading{}; // Magazine out, or a shell held for a tube.
		};
		std::mutex mutex;
		std::array<sample, 2> samples{};
		std::array<std::pair<std::uint32_t, int>, 2> most_loaded{}; // Per hand: weapon and its fullest loaded count.
		game::dvar_t* enabled{};
		game::dvar_t* always{};
		constexpr auto linger = std::chrono::milliseconds(2000); // After the magazine seats or the shell goes in.
		std::array<std::chrono::steady_clock::time_point, 2> reloading_seen{}; // Per hand slot: last reloading frame.
		std::atomic_uint64_t frames{}, drawn{}, rejected{};
		std::mutex debug_mutex;
		std::array<float, 6> last_debug{}; // Last drawn counter and head, world units.

		void collect()
		{
			std::array<sample, 2> batch{};
			if (weapons::carry::active() && game::CL_IsCgameInitialized() && game::g_entities[0].client)
			{
				const auto held = weapons::carry::held_instances();
				for (unsigned h = 0; h < 2; ++h)
				{
					const auto& v = held[h];
					if (!v.id || v.id.weapon >= 512) continue;
					const auto holder = v.owner.holding_hand();
					if (!valid_hand(holder)) continue;
					const auto ammo = weapons::native_ammunition::observe_carried(game::g_entities[0].client, v.id);
					if (!ammo.valid || ammo.loaded < 0 || ammo.reserve < 0) continue;
					const auto reload = weapons::physical_reload::current(v.id);
					const auto tube = weapons::tube::current(v.id);
					const bool reloading = (reload.active && reload.definition && !reload.ammo.magazine_inserted) ||
					                       (tube.active && tube.ammo.held_rounds > 0);
					batch[h] = {true, static_cast<int>(holder), ammo.loaded, ammo.reserve, v.id.weapon, reloading};
				}
			}
			const std::lock_guard lock(mutex);
			samples = batch;
		}

		void draw()
		{
			++frames;
			if (!enabled || !enabled->current.enabled) return;
			if (!game::CL_IsCgameInitialized() || !engine_stereo_bridge::is_active() || *game::keyCatchers) return;
			std::array<sample, 2> current;
			{
				const std::lock_guard lock(mutex);
				current = samples;
			}
			const auto now = std::chrono::steady_clock::now();
			const bool show_always = always && always->current.enabled;
			for (unsigned i = 0; i < 2; ++i)
			{
				if (current[i].valid && current[i].reloading) reloading_seen[i] = now;
				if (!show_always && current[i].valid && now - reloading_seen[i] > linger) current[i].valid = false;
			}
			if (!current[0].valid && !current[1].valid) return;
			head_pose_bridge::spatial_frame body;
			if (!head_pose_bridge::get_spatial_frame(body) || !(body.units_per_meter > 0)) return;
			const auto* placement = game::ScrPlace_GetViewPlacement();
			auto* font = game::R_RegisterFont("fonts/defaultBold.otf", 27);
			if (!placement || !font) return;
			auto* backing_material = game::DB_FindXAssetHeader(game::ASSET_TYPE_MATERIAL, "white", 0).material;
			const auto input = controller_input::latest();
			for (unsigned i = 0; i < 2; ++i)
			{
				const auto& s = current[i];
				if (!s.valid) continue;
				hands::anchor wrist;
				if (!hands::tracked_wrist(input, body, {}, s.hand, {}, wrist)) continue;

				// Low means a quarter of the fullest magazine seen for this gun.
				auto& fullest = most_loaded[i];
				if (fullest.first != s.weapon) fullest = {s.weapon, s.loaded};
				fullest.second = std::max(fullest.second, s.loaded);
				const bool empty = s.loaded == 0, low = !empty && s.loaded * 4 <= fullest.second;

				const auto loaded = std::to_string(s.loaded), reserve = " / " + std::to_string(s.reserve);
				constexpr float loaded_scale = 1.f, reserve_scale = .62f;
				const float loaded_width = float(game::R_TextWidth(loaded.c_str(), int(loaded.size()), font)) * loaded_scale;
				const float reserve_width = float(game::R_TextWidth(reserve.c_str(), int(reserve.size()), font)) * reserve_scale;
				if (!std::isfinite(loaded_width) || !std::isfinite(reserve_width) || loaded_width <= 0) continue;
				const float width = loaded_width + reserve_width;

				directional_ui::waypoint marker;
				marker.world = {wrist.position[0], wrist.position[1], wrist.position[2] + .075f * body.units_per_meter};
				std::copy_n(placement->realViewportSize, 2, marker.viewport.begin());
				marker.anchor = {marker.viewport[0] * .5f, marker.viewport[1] * .5f};
				// Fixed world size: 27 px glyphs 2.5 cm tall on the wrist.
				if (!(marker.viewport[0] >= 16) || !(marker.viewport[1] >= 16)) continue;
				marker.tangent = {.839099631f, .839099631f * marker.viewport[1] / marker.viewport[0]};
				marker.pixels_per_meter = 27.f / .025f;
				const float x = marker.anchor[0] - width * .5f, baseline = marker.anchor[1] + 10;
				const directional_ui::rectangle bounds{x - 8, baseline - 34, x + width + 8, baseline + 10};
				const bool accepted = native_waypoints::draw_world_marker(marker, [&]
				{
					float white[]{1, 1, 1, .95f}, grey[]{.8f, .8f, .8f, .9f}, yellow[]{1, .85f, .2f, 1}, red[]{1, .35f, .3f, 1},
						backing[]{0, 0, 0, .45f};
					if (backing_material)
						game::R_AddCmdDrawStretchPic(bounds.x + 4, bounds.y + 4, bounds.right - bounds.x - 8, bounds.bottom - bounds.y - 8,
						                             0, 0, 1, 1, backing, backing_material);
					game::R_AddCmdDrawText(loaded.c_str(), int(loaded.size()), font, x, baseline, loaded_scale, loaded_scale, 0,
					                       empty ? red : low ? yellow : white, 4);
					game::R_AddCmdDrawText(reserve.c_str(), int(reserve.size()), font, x + loaded_width, baseline, reserve_scale, reserve_scale, 0,
					                       s.reserve == 0 ? red : grey, 4);
				}, bounds);
				if (accepted)
				{
					++drawn;
					const std::lock_guard lock(debug_mutex);
					last_debug = {marker.world[0], marker.world[1], marker.world[2],
					              body.head_position[0], body.head_position[1], body.head_position[2]};
				}
				else ++rejected;
			}
		}
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			enabled = dvars::register_bool("vr_ammoCounter", true, game::DVAR_FLAG_SAVED,
			                               "Show the held gun's loaded and reserve ammo above its wrist");
			always = dvars::register_bool("vr_ammoCounterAlways", false, game::DVAR_FLAG_SAVED,
			                              "Keep the wrist ammo counter on at all times instead of only while reloading");
			scheduler::loop(collect, scheduler::pipeline::server);
			scheduler::loop(draw, scheduler::pipeline::lui);
			command::add("vr_ammo_counter_status", []
			{
				std::array<float, 6> d;
				{
					const std::lock_guard lock(debug_mutex);
					d = last_debug;
				}
				console::info("[VR ammo] frames=%llu drawn=%llu rejected=%llu at (%.1f %.1f %.1f) head (%.1f %.1f %.1f)\n",
				              frames.load(), drawn.load(), rejected.load(), d[0], d[1], d[2], d[3], d[4], d[5]);
			});
		}
	};
}

REGISTER_COMPONENT(vr::gameplay::ammo_counter::component)
