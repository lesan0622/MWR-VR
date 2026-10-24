#include <std_include.hpp>
#include "../controller_input.hpp"
#include "../diagnostics.hpp"
#include "../engine_stereo_owner_pass.hpp"
#include "../settings.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "controller_buttons.hpp"
#include "controller_locomotion.hpp"
#include "controller_stance.hpp"
#include "controller_ads.hpp"
#include "../digital_button_gate.hpp"
#include "weapon_carry_runtime.hpp"
#include "mwr_weapon_pose.hpp"
#include "independent_fire_runtime.hpp"
#include "native_scripted_control.hpp"
#include "world_interaction.hpp"
#include "optic_runtime.hpp"
#include "physical_reload_runtime.hpp"
#include "view_aimed_weapon.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

// MWR command adapter. Physical weapon ADS and native binding notifications
// share the controller samples used by movement and independent weapon fire.
namespace vr::controllers
{
	namespace
	{
		// MWR CL_CreateCmd 0x1401AB230 (H2 0x1403D20F0):
		//   CL_CmdButtons/CL_KeyMove -> CL_MouseMove 0x1401AA500 (call 0x1401AB363)
		//   -> FinishMove 0x1401A9470 (call 0x1401AB3E2).
		// The remote-control branch 0x1401AA9B0 is not connected.
		constexpr std::uintptr_t create_command_address = 0x1401AB230;
		constexpr std::uintptr_t normal_input_call = 0x1401AB363;
		constexpr std::uintptr_t normal_input_address = 0x1401AA500;
		constexpr std::uintptr_t finish_move_call = 0x1401AB3E2;
		constexpr std::uintptr_t finish_move_address = 0x1401A9470;
		// CL_ExecuteKey 0x1401A8350 calls this reliable binding notification
		// before updating the movement keys (H2 0x1403D3A90).
		constexpr std::uintptr_t notify_command_address = 0x1401ACBD0;
		constexpr int jump_binding = 37; // Native whitelist: +gostand.
		constexpr int sprint_binding = 83; // Native whitelist: +sprint.
		bool sprint_binding_verified{};
		// AC-130 gunship script waits: notifyoncommand weapnext (gun cycle) and
		// +usereload/+activate (thermal polarity).
		constexpr int use_reload_binding = 11, weapnext_binding = 91;
		bool gunship_bindings_verified{};
		std::uint64_t gunship_reload_presses{}, gunship_cycle_presses{};
		std::atomic<std::uint64_t> sprint_notify_count{};
		// clientActive_t 0x14252B190 (stride 0xF730): viewangles +0x5C, stance +0x1C.
		constexpr std::uintptr_t client_angles_address = 0x14252B1EC;
		constexpr std::uintptr_t client_yaw_address = 0x14252B1F0;
		constexpr std::uintptr_t execute_binding_address = 0x1401A8350;
		constexpr std::uintptr_t native_stance_address = 0x14252B1AC;
		utils::hook::detour create_command_hook;
		game::dvar_t* enabled{};
		game::dvar_t* deadzone{};
		game::dvar_t* snap_angle{};
		game::dvar_t* turn_mode{};
		game::dvar_t* turn_speed{};
		game::dvar_t* turn_deadzone{};
		game::dvar_t* auto_ads{};
		gameplay::weapons::ads_policy ads;
		std::array<controller_input::digital_press_gate,2> attack_notifications{};
		std::array<gameplay::weapons::hold,2> notified_owners{};
		bool ads_verified{}, binding_verified{}, reload_binding_verified{};
		std::uint64_t reload_inserts_seen{};
		std::atomic_uint64_t reload_notify_count{};
		constexpr int reload_binding = 75; // MWR command whitelist "+reload"
		std::atomic_bool ads_requested{};
		std::atomic_uint64_t ads_command_count{}, attack_notify_count{}, ads_notify_count{};
		controller_input::locomotion locomotion;
		controller_input::command_buttons buttons;
		controller_input::stance_gesture stance;
		bool stance_verified{};
		std::atomic<std::uint64_t> stance_count{};
		thread_local int building_client = -1;
		thread_local bool normal_input_seen{};
		thread_local bool tracked_view_applied{};
		controller_input::clock::time_point last_command{};
		std::atomic<bool> alive{true};
		std::atomic<std::uint64_t> command_count{};
		std::atomic<std::uint64_t> movement_count{};
		std::atomic<std::uint64_t> turn_count{};
		std::atomic<std::uint64_t> sprint_count{};
		std::atomic<std::uint64_t> jump_count{};
		std::atomic<std::uint64_t> jump_notify_count{};
		std::atomic<std::uint64_t> view_count{};
		std::atomic<std::uint64_t> attack_count{}, reload_count{};
		constexpr int attack_button = 0x1, use_reload_button = 0x20; // MWR CL_CmdButtons 0x1401A7F30.
		bool hooks_installed{};

		static_assert(sizeof(game::usercmd_s) == 64);
		static_assert(offsetof(game::usercmd_s, buttons) == 4);
		static_assert(offsetof(game::usercmd_s, forwardmove) == 28);
		static_assert(offsetof(game::usercmd_s, rightmove) == 29);
		static_assert(offsetof(game::playerState_s, commandTime) == 0x4c);
		static_assert(offsetof(game::playerState_s, delta_angles) == 0x98);
		static_assert(offsetof(game::playerState_s, viewangles) == 0x10C);
		static_assert(offsetof(game::usercmd_s, angles) == 8);

		bool normal_gameplay()
		{
			// MWR may not register cl_paused under this name; absence means unpaused.
			const auto* paused = game::Dvar_FindVar("cl_paused");
			return game::CL_IsCgameInitialized() && *game::keyCatchers == 0 &&
				(!paused || paused->current.integer == 0);
		}

		bool gameplay_allowed(const controller_input::frame& input)
		{
			const auto head = head_pose_bridge::get_status();
			return alive && enabled && enabled->current.enabled && head.enabled && head.pose_available &&
				!head.recenter_pending && head.recenter_count == input.reference_generation && normal_gameplay();
		}

		// AC-130 flat screen: the head no longer steers the native view; the right
		// stick aims like the native mouse (the gunship link clamps the result).
		controller_input::clock::time_point gunship_aim_at{};
		void aim_flat_screen(float* angles, const game::playerState_s* ps)
		{
			const auto now = controller_input::clock::now();
			const float dt = gunship_aim_at.time_since_epoch().count() ?
				std::min(std::chrono::duration<float>(now - gunship_aim_at).count(), .1f) : 0.f;
			gunship_aim_at = now;
			const auto input = controller_input::latest();
			if (!input.focused || !input.turn_active || dt <= 0) return;
			const float dead = turn_deadzone ? turn_deadzone->current.value : .2f;
			const auto axis = [dead](float v) {
				const float a = std::abs(v);
				return a <= dead || !std::isfinite(v) ? 0.f : std::copysign((a - dead) / (1 - dead), v);
			};
			// Narrower native FOV (40/25 mm) aims proportionally slower.
			const auto* fov = game::Dvar_FindVar("cg_fov");
			const float rate = 50.f * std::clamp(fov ? fov->current.value / 65.f : 1.f, .2f, 1.5f);
			// Step from the view the gunship link actually allowed, so pushing past
			// its clamp never banks a dead zone on the way back.
			const float pitch = ps->viewangles[0] - ps->delta_angles[0], yaw = ps->viewangles[1] - ps->delta_angles[1];
			if (std::isfinite(pitch) && std::isfinite(yaw))
			{
				angles[1] = std::remainder(yaw - axis(input.turn[0]) * rate * dt, 360.f);
				angles[0] = std::clamp(std::remainder(pitch, 360.f) - axis(input.turn[1]) * rate * dt, -85.f, 85.f);
			}
		}
		void finish_move_stub(const int local_client, game::usercmd_s* const cmd)
		{
			// The native pitch clamp and other input sources run after the
			// normal-input stub. Set the game view at the final packing boundary.
			if (local_client == 0 && building_client == 0 && normal_input_seen && cmd &&
				alive.load(std::memory_order_relaxed) && normal_gameplay())
			{
				if (const auto* const ps = game::CG_GetPredictedPlayerState(0))
				{
					auto* angles = reinterpret_cast<float*>(client_angles_address);
					if (gameplay::view_aimed::active()) aim_flat_screen(angles, ps);
					else
					{
						gunship_aim_at = {};
						tracked_view_applied = head_pose_bridge::apply_game_view(angles, ps->delta_angles);
						if (tracked_view_applied) ++view_count;
					}
				}
			}
			utils::hook::invoke<void>(finish_move_address, local_client, cmd);
		}

		void apply_locomotion(game::usercmd_s* cmd, const controller_input::frame& input, bool gameplay,
			controller_input::clock::time_point now)
		{
			const auto movement = locomotion.consume(input, gameplay, deadzone->current.value,
				{
					static_cast<controller_input::turn_mode>(turn_mode->current.integer),
					turn_deadzone->current.value,
					turn_speed->current.value,
					snap_angle->current.value,
				},
				now);
			if (!movement.active)
				return;
			const auto add_axis = [](const char original, const float axis) {
				return static_cast<char>(std::clamp(static_cast<int>(static_cast<signed char>(original)) +
					static_cast<int>(std::lround(axis * 127.0f)), -127, 127));
			};
			cmd->forwardmove = add_axis(cmd->forwardmove, movement.forward);
			cmd->rightmove = add_axis(cmd->rightmove, movement.right);
			if (movement.forward != 0 || movement.right != 0)
				++movement_count;
			if (movement.yaw_delta != 0)
			{
				auto& yaw = *reinterpret_cast<float*>(client_yaw_address);
				if (std::isfinite(yaw))
				{
					yaw = std::remainder(yaw + movement.yaw_delta, 360.0f);
					if (movement.discontinuous_turn)
						engine_stereo_owner_pass::request_temporal_history_reset();
					++turn_count;
				}
			}
		}

		void normal_input_stub(game::usercmd_s* const cmd, const float frame_time)
		{
			utils::hook::invoke<void>(normal_input_address, cmd, frame_time);
			if (building_client != 0 || !cmd || !alive.load(std::memory_order_relaxed))
				return;
			normal_input_seen = true;
			const auto input = controller_input::latest_interaction();
			const bool gameplay = gameplay_allowed(input);
			const auto now = controller_input::clock::now();
			const bool weapon_gameplay = gameplay && gameplay::scripted_control::predicted_firing_allowed();
			// Open the carried-gun fire gate for this input frame (original
			// controller_component): independent fire owns trigger cadence.
			vr::gameplay::weapons::independent_fire::command(gameplay && vr::gameplay::scripted_control::predicted_allowed());
			const auto button_state = buttons.consume(input, gameplay, now);
			auto button_mask = button_state.held;
			const auto* posture_ps = game::CG_GetPredictedPlayerState(0);
			const auto posture = posture_ps ? controller_input::native_posture(posture_ps->pm_flags) : controller_input::posture::unknown;
			const bool stance_allowed = gameplay && stance_verified && posture_ps &&
				!(posture_ps->pm_flags & (game::PMF_MANTLE | game::PMF_LADDER));
			const auto posture_command = stance.consume(input, posture, stance_allowed, now,
				(button_state.pressed & controller_input::jump_button) != 0, (button_mask & controller_input::jump_button) != 0);
			if (posture_command.target)
			{
				// Reuse the native input-stance commands, not playerState writes.
				const int binding = controller_input::stance_binding(*posture_command.target, *reinterpret_cast<const int*>(native_stance_address));
				if (binding) { utils::hook::invoke<void>(execute_binding_address, building_client, binding, 0, 0u); ++stance_count; }
			}
			if (posture_command.suppress_jump) button_mask &= ~controller_input::jump_button;
			cmd->buttons |= button_mask; // Never clear a keyboard/native button.
			if (button_state.pressed & controller_input::jump_button)
			{
				// One native binding notification per accepted press (notifyoncommand).
				utils::hook::invoke<void>(notify_command_address, building_client, jump_binding, 0);
				++jump_notify_count;
			}
			if (sprint_binding_verified && (button_state.pressed & controller_input::sprint_button))
			{
				// Script hints (killhouse sprint) wait on notifyoncommand +sprint.
				utils::hook::invoke<void>(notify_command_address, building_client, sprint_binding, 0);
				++sprint_notify_count;
			}
			// Right trigger fires, right B reloads (+usereload). Native weapon
			// state, ammunition and fire rate keep authority over the request.
			if (gameplay && input.trigger[1].active && input.trigger[1].down)
			{
				cmd->buttons |= attack_button;
				++attack_count;
			}
			// usercmd attack bits and weapon_fired do not deliver notifyoncommand
			// waits. Use the same reliable binding notification as a native key,
			// once per deliberate trigger press, without firing a second bullet.
			const auto held = gameplay::weapons::carry::held_instances();
			for (unsigned h=0; h<attack_notifications.size(); ++h)
			{
				const auto owner = held[h].owner;
				if (!binding_verified || !weapon_gameplay || !input.focused || !input.sequence ||
					now < input.sampled_at || now-input.sampled_at > std::chrono::milliseconds(150) ||
					!owner.can_fire() || int(owner.rear)!=int(h) || !input.grip[h].valid)
				{
					attack_notifications[h]={}; notified_owners[h]={}; continue;
				}
				if (owner.id()!=notified_owners[h].id() || owner.rear_revision!=notified_owners[h].rear_revision)
					attack_notifications[h]={};
				notified_owners[h]=owner;
				if (attack_notifications[h].consume(input.trigger[h]))
				{
					utils::hook::invoke<void>(notify_command_address, building_client, 1, 0);
					++attack_notify_count;
				}
			}
			// A physical magazine insertion is the VR reload. Script hints wait on the
			// +reload binding (notifyoncommand); deliver it once per committed insert.
			if (const auto inserts = gameplay::weapons::physical_reload::magazine_insertions(); inserts != reload_inserts_seen)
			{
				reload_inserts_seen = inserts;
				if (reload_binding_verified && weapon_gameplay)
				{
					utils::hook::invoke<void>(notify_command_address, building_client, reload_binding, 0);
					++reload_notify_count;
				}
			}
			// AC-130: right B toggles thermal, right A or left Y cycles the guns.
			if (gunship_bindings_verified && gameplay && gameplay::view_aimed::active())
			{
				const auto cycle = input.primary[1].presses + input.secondary[0].presses;
				if (input.secondary[1].presses != gunship_reload_presses)
					utils::hook::invoke<void>(notify_command_address, building_client, use_reload_binding, 0);
				if (cycle != gunship_cycle_presses)
					utils::hook::invoke<void>(notify_command_address, building_client, weapnext_binding, 0);
				gunship_cycle_presses = cycle;
			}
			else gunship_cycle_presses = input.primary[1].presses + input.secondary[0].presses;
			gunship_reload_presses = input.secondary[1].presses;
			if (gameplay && input.secondary[1].active && input.secondary[1].down)
			{
				cmd->buttons |= use_reload_button;
				++reload_count;
			}
			if (button_mask & controller_input::sprint_button) ++sprint_count;
			if (button_mask & controller_input::jump_button) ++jump_count;
			// Empty-hand grip world use (original controller_component): +activate
			// and its binding notification for the hand-selected native use target.
			cmd->buttons |= gameplay::interaction::command(input, gameplay, now);
			if (!gameplay::view_aimed::active()) apply_locomotion(cmd, input, gameplay, now);
		}

		game::usercmd_s* create_command_stub(game::usercmd_s* output, const int local_client)
		{
			if (local_client == 0)
				controller_input::set_gameplay_active(alive.load() && normal_gameplay());
			tracked_view_applied = false;
			const auto now = controller_input::clock::now();
			if (now - last_command > std::chrono::milliseconds(150))
			{
				if (local_client == 0) gameplay::interaction::command(controller_input::latest_interaction(), false, now);
				locomotion.reset();
				buttons.reset();
				stance.reset();
				attack_notifications={}; notified_owners={};
			}
			last_command = now;
			building_client = local_client;
			normal_input_seen = false;
			const auto result = create_command_hook.invoke<game::usercmd_s*>(output, local_client);
			if (local_client==0 && result)
			{
				const auto input=controller_input::latest_interaction();
				const auto owner=gameplay::weapons::sample_native_equipped();
				const auto* ps=game::CG_GetPredictedPlayerState(0);
				const bool allowed=gameplay_allowed(input) && normal_input_seen && ads_verified &&
					auto_ads && auto_ads->current.enabled && gameplay::scripted_control::predicted_allowed() &&
					ps && !(ps->pm_flags&(game::PMF_MANTLE|game::PMF_LADDER)) &&
					owner.weapon==result->weapon && gameplay::weapons::firing_enabled() &&
					gameplay::weapons::controller_fire_delivery(owner.weapon)==gameplay::weapons::fire_delivery::bullets;
				const bool requested=ads.consume(input,owner,gameplay::weapons::current_muzzle(),allowed,now);
				// Physical scope magnification follows the same ADS request (original controller_component).
				gameplay::weapons::optics::request(owner,{allowed,requested},input);
				if (requested) {result->buttons|=game::BUTTON_ADS; ++ads_command_count;}
				if (binding_verified && allowed && requested!=ads_requested.load())
				{
					utils::hook::invoke<void>(notify_command_address,local_client,requested ? 17 : 18,0);
					++ads_notify_count;
				}
				ads_requested=requested;
			}
			else if (local_client==0) {ads.reset(); ads_requested=false; gameplay::weapons::optics::request({},{},{});}
			if (local_client == 0 && result)
				head_pose_bridge::record_game_command(result->serverTime, result->angles[0], tracked_view_applied);
			if (!normal_input_seen)
			{
				// Scripted/suppressed branches must rearm even if they never poll controls.
				if (local_client == 0) gameplay::interaction::command(controller_input::latest_interaction(), false, now);
				locomotion.reset();
				buttons.reset();
				attack_notifications={}; notified_owners={};
			}
			building_client = -1;
			++command_count;
			return result;
		}

		void print_status()
		{
			const auto input = controller_input::latest_interaction();
			const auto head = head_pose_bridge::get_status();
			console::info("[VR input] game_view=hmd pitch_yaw_commands=%llu view_applied=%llu history_misses=%llu "
				"camera_command_head_yaw=%.3f tracking_world_yaw=%.3f\n",
				head.game_view_applications, view_count.load(), head.game_view_history_misses,
				head.game_view_yaw_contribution, head.base_yaw_degrees);
			console::info("[VR input] turn_mode=%s turn_speed=%.1f turn_deadzone=%.2f snap_angle=%.1f\n",
				turn_mode->current.integer == 0 ? "smooth" : "snap", turn_speed->current.value,
				turn_deadzone->current.value, snap_angle->current.value);
			const auto now = controller_input::clock::now();
			const bool sampled = input.sequence != 0 && input.sampled_at != controller_input::clock::time_point{} &&
				now >= input.sampled_at;
			const auto age = sampled
				? std::chrono::duration_cast<std::chrono::milliseconds>(now - input.sampled_at).count()
				: -1;
			console::info("[VR input] hooks=%d enabled=%d frame=%llu age_ms=%lld focus=%d move=%d(%.3f,%.3f) "
				"turn=%d(%.3f,%.3f) commands=%llu moving=%llu turns=%llu gameplay=%d\n",
				hooks_installed, enabled && enabled->current.enabled,
				static_cast<unsigned long long>(input.sequence), static_cast<long long>(age),
				input.focused, input.move_active, input.move[0], input.move[1], input.turn_active,
				input.turn[0], input.turn[1], command_count.load(), movement_count.load(),
				turn_count.load(), normal_gameplay());
			console::info("[VR input] sample_available=%d head_pose=%d recenter_pending=%d recenter_count=%llu\n",
				sampled, head.pose_available, head.recenter_pending, head.recenter_count);
			console::info("[VR input] sprint_active=%d presses=%llu jump_active=%d presses=%llu "
				"sprint_commands=%llu jump_commands=%llu jump_notifies=%llu\n",
				input.sprint.active, input.sprint.presses, input.jump.active, input.jump.presses,
				sprint_count.load(), jump_count.load(), jump_notify_count.load());
			console::info("[VR input] attack_commands=%llu reload_commands=%llu weapon_hand=%d\n",
				attack_count.load(), reload_count.load(), gameplay::mwr_weapon_pose::active());
			console::info("[VR input] auto_ads=%d ads_verified=%d ads_requested=%d ads_commands=%llu "
				"binding_verified=%d attack_notifies=%llu ads_notifies=%llu reload_notifies=%llu\n",
				auto_ads && auto_ads->current.enabled,ads_verified,ads_requested.load(),ads_command_count.load(),
				binding_verified,attack_notify_count.load(),ads_notify_count.load(),reload_notify_count.load());
			console::info("[VR input] ads_gate=%s depth=%.3f lateral=%.3f height=%.3f facing=%.3f\n", gameplay::weapons::ads_policy::last_reason.load(),
				gameplay::weapons::ads_policy::last_depth.load(), gameplay::weapons::ads_policy::last_lateral.load(),
				gameplay::weapons::ads_policy::last_height.load(), gameplay::weapons::ads_policy::last_facing.load());
			const auto* ps = game::CL_IsCgameInitialized() ? game::CG_GetPredictedPlayerState(0) : nullptr;
			console::info("[VR input] stance_verified=%d requests=%llu requested=%d actual=%d\n",
				stance_verified, stance_count.load(), stance_verified && ps ? *reinterpret_cast<const int*>(native_stance_address) : -1,
				ps ? int(controller_input::native_posture(ps->pm_flags)) : -1);
		}

		bool check(std::uintptr_t address, std::initializer_list<std::uint8_t> bytes)
		{
			std::array<std::uint8_t, 16> mask{};
			mask.fill(255);
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(address),
				{bytes.begin(), mask.data(), bytes.size()}));
		}
	} // namespace

	class component final : public component_interface
	{
	  public:
		void post_unpack() override
		{
			// Menus can stop the command consumers while tracking keeps publishing.
			scheduler::loop([] {controller_input::set_gameplay_active(alive.load() && normal_gameplay());}, scheduler::pipeline::main);
			enabled = dvars::register_bool("vr_controllers", true, game::DVAR_FLAG_SAVED,
				"Enable VR controller locomotion");
			deadzone = dvars::register_float("vr_moveDeadzone", 0.2f, 0.05f, 0.5f, game::DVAR_FLAG_SAVED,
				"Radial movement stick deadzone");
			snap_angle = dvars::register_float(settings::snap_angle.name, settings::snap_angle.default_value,
				settings::snap_angle.min, settings::snap_angle.max, game::DVAR_FLAG_SAVED,
				"Degrees per right-stick snap turn (release to rearm)");
			static const char* turn_modes[]{"smooth", "snap", nullptr};
			turn_mode = dvars::register_enum(settings::turn_mode, turn_modes, 0, game::DVAR_FLAG_SAVED,
				"Controller turning: smooth or snap; release sticks after switching");
			turn_speed = dvars::register_float(settings::turn_speed.name, settings::turn_speed.default_value,
				settings::turn_speed.min, settings::turn_speed.max, game::DVAR_FLAG_SAVED,
				"Smooth turning speed in degrees per second at full deflection");
			turn_deadzone = dvars::register_float("vr_turnDeadzone", 0.2f, 0.05f, 0.5f, game::DVAR_FLAG_SAVED,
				"Smooth turning stick deadzone");
			command::add("vr_input_status", print_status);
			auto_ads=dvars::register_bool("vr_autoAds",true,game::DVAR_FLAG_SAVED,
				"Request native ADS while a supported physical weapon is raised to the sights");
			// MWR CL_KeyMove sets/clears usercmd bit 11 before FinishMove.
			ads_verified=check(0x1401AA0C2,{0x81,0x4b,0x04,0x00,0x08,0x00,0x00}) &&
				check(0x1401AA0B9,{0x81,0x63,0x04,0xff,0xf7,0xff,0xff});
			if (!ads_verified) console::error("[VR input] Native ADS contract rejected; automatic ADS disabled\n");
			binding_verified=true;
			for (const auto [index,name] : std::array<std::pair<int,const char*>,3>{{{1,"+attack"},{17,"+speed_throw"},{18,"-speed_throw"}}})
				binding_verified=binding_verified && game::command_whitelist[index] && std::strcmp(game::command_whitelist[index],name)==0;
			sprint_binding_verified=game::command_whitelist[sprint_binding] &&
				std::strcmp(game::command_whitelist[sprint_binding],"+sprint")==0;
			gunship_bindings_verified=game::command_whitelist[use_reload_binding] && game::command_whitelist[weapnext_binding] &&
				std::strcmp(game::command_whitelist[use_reload_binding],"+usereload")==0 &&
				std::strcmp(game::command_whitelist[weapnext_binding],"weapnext")==0;
			reload_binding_verified=binding_verified && game::command_whitelist[reload_binding] &&
				std::strcmp(game::command_whitelist[reload_binding],"+reload")==0;

			// Native stance branches of CL_ExecuteKey and the whitelist indices.
			stance_verified = check(0x1401A91EE, {0x44, 0x89, 0x74, 0x28, 0x1c}) &&
				check(0x1401A91A2, {0x89, 0x4c, 0x28, 0x1c}) &&
				check(0x1401A91E1, {0x48, 0x8d, 0x2d, 0xa8, 0x1f, 0x38, 0x02});
			for (const auto [index, name] : std::array<std::pair<int, const char*>, 4>{{{89, "+togglecrouch"}, {99, "toggleprone"}, {100, "goprone"}, {101, "gocrouch"}}})
				stance_verified = stance_verified && game::command_whitelist[index] && std::strcmp(game::command_whitelist[index], name) == 0;
			if (!stance_verified) console::error("[VR input] Native stance contract rejected; vertical stick stance disabled\n");

			// CreateCmd entry, its call sites, FinishMove's angle packing, the
			// sprint/jump command bits and the binding notification.
			if (!check(create_command_address, {0x48, 0x89, 0x5c, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x40}) ||
				!check(normal_input_call, {0xe8, 0x98, 0xf1, 0xff, 0xff}) ||
				!check(finish_move_call, {0xe8, 0x89, 0xe0, 0xff, 0xff}) ||
				!check(0x1401A9520, {0xf3, 0x41, 0x0f, 0x10, 0x44, 0x36, 0x54}) ||
				!check(0x1401AA1CF, {0x83, 0x4b, 0x04, 0x02}) ||
				!check(0x1401A80FE, {0x81, 0x4a, 0x04, 0x00, 0x04, 0x00, 0x00}) ||
				!check(notify_command_address, {0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xec, 0x30, 0x04, 0x00, 0x00}) ||
				!check(0x1401A838C, {0xe8, 0x3f, 0x48, 0x00, 0x00}) ||
				!game::command_whitelist[jump_binding] ||
				std::strcmp(game::command_whitelist[jump_binding], "+gostand") != 0)
			{
				console::error("[VR input] MWR command hook signature mismatch; native HMD view and controllers "
					"are not connected to gameplay\n");
				return;
			}
			create_command_hook.create(create_command_address, create_command_stub);
			utils::hook::call(normal_input_call, normal_input_stub);
			utils::hook::call(finish_move_call, finish_move_stub);
			hooks_installed = true;
		}

		void pre_destroy() override
		{
			alive.store(false, std::memory_order_relaxed);
		}
	};
} // namespace vr::controllers

REGISTER_COMPONENT(vr::controllers::component)
