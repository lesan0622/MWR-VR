#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"
#include "game/ui_scripting/execution.hpp"

#include "command.hpp"
#include "console.hpp"
#include "camera.hpp"
#include "gui/gui.hpp"
#include "vr/gameplay/ladder_runtime.hpp"
#include "vr/gameplay/scripted_motion.hpp"
#include "vr/gameplay/javelin_screen.hpp"
#include "vr/gameplay/fixed_sniper.hpp"
#include "vr/gameplay/barrett_gun.hpp"
#include "vr/engine_stereo_renderer.hpp"
#include "vr/head_pose_bridge.hpp"

#include <utils/hook.hpp>
#include <utils/concurrency.hpp>

namespace camera
{
	namespace
	{
		utils::hook::detour sub_140781090_hook;

		game::dvar_t* cl_free_move_scale = nullptr;
		game::dvar_t** cg_paused = nullptr;
		
		using cb_type = std::optional<std::function<void()>>;
		utils::concurrency::container<cb_type> callback;

		float angle_normalize(const float angle)
		{
			const auto value = ((angle * 0.0027777778f) - std::floorf(angle * 0.0027777778f)) * 360.f;
			auto result = value - 360.f;

			if ((value - 360.f) < 0.f)
			{
				result = value;
			}

			return result;
		}

		void vec_cross(float* a1, float* a2, float* a3)
		{
			a3[0] = (a2[2] * a1[1]) - (a2[1] * a1[2]);
			a3[1] = (a1[2] * a2[0]) - (a2[2] * a1[0]);
			a3[2] = (a2[1] * a1[0]) - (a1[1] * a2[0]);
		}

		float camera_origin[3]{};

		auto using_camera_override = false;
		auto using_camera_angles_override = false;

		float camera_origin_override[3]{};
		float camera_angles_override[3]{};

		void paused_free_move()
		{
			// MWR: cg_paused has not been rebound; free-move stays disabled.
			if (cg_paused == nullptr || (*cg_paused)->current.integer != 2)
			{
				return;
			}

			constexpr auto local_client_num = 0;
			const auto ps = game::CG_GetPredictedPlayerState(local_client_num);
			const auto cmd_number = game::CL_GetCurrentCmdNumber(local_client_num);

			game::usercmd_s cmd{};
			if (!game::CL_GetUserCmd(local_client_num, cmd_number, &cmd))
			{
				return;
			}

			callback.access([](cb_type& cb)
			{
				if (cb.has_value())
				{
					auto& fn = cb.value();
					fn();
				}
			});

			if (using_camera_angles_override)
			{
				game::cgs->refdefViewAngles[0] = camera_angles_override[0];
				game::cgs->refdefViewAngles[1] = camera_angles_override[1];
				game::cgs->refdefViewAngles[2] = camera_angles_override[2];
			}
			else
			{
				game::cgs->refdefViewAngles[0] = angle_normalize((cmd.angles[0] * 0.000021457672f) + ps->delta_angles[0]);
				game::cgs->refdefViewAngles[1] = angle_normalize((cmd.angles[1] * 0.000021457672f) + ps->delta_angles[1]);
				game::cgs->refdefViewAngles[2] = 0;
			}

			const auto game_time = game::CG_GetGameTime(local_client_num);

			static auto saved_game_time = 0;
			static auto saved_ms = 0;

			if (saved_game_time != game_time)
			{
				saved_game_time = game_time;
				saved_ms = game::Sys_Milliseconds();
			}

			const auto now = game::Sys_Milliseconds();
			const auto diff = now - saved_ms;
			saved_ms = game::Sys_Milliseconds();

			auto scale = 1.f;
			if ((cmd.buttons & 0x6) != 0)
			{
				scale = 10.f;
			}

			scale *= cl_free_move_scale->current.value;

			game::AnglesToAxis(game::cgs->refdefViewAngles, game::refdef->axis);

			float v1[3] = {0.f, 0.f, 1.f};
			float v2[3] = {game::refdef->axis[0][0], game::refdef->axis[0][1], game::refdef->axis[0][2]};
			float v3[3]{};

			vec_cross(v1, v2, v3);
			vec_cross(v3, v1, v2);

			if (cmd.forwardmove)
			{
				ps->origin[0] += scale * (game::refdef->axis[0][0] * (static_cast<float>(cmd.forwardmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
				ps->origin[1] += scale * (game::refdef->axis[0][1] * (static_cast<float>(cmd.forwardmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
				ps->origin[2] += scale * (game::refdef->axis[0][2] * (static_cast<float>(cmd.forwardmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
			}

			if (cmd.rightmove)
			{
				ps->origin[0] += scale * (v3[0] * (static_cast<float>(-1 * cmd.rightmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
				ps->origin[1] += scale * (v3[1] * (static_cast<float>(-1 * cmd.rightmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
				ps->origin[2] += scale * (v3[2] * (static_cast<float>(-1 * cmd.rightmove) * 0.2f) * (static_cast<float>(diff) * 0.05f));
			}

			if ((cmd.buttons & 0x1) != 0)
			{
				ps->origin[2] += scale * 10.f;
			}

			if ((cmd.buttons & 0x800) != 0)
			{
				ps->origin[2] -= scale * 10.f;
			}

			if (using_camera_override)
			{
				game::refdef->org[0] = camera_origin_override[0];
				game::refdef->org[1] = camera_origin_override[1];
				game::refdef->org[2] = camera_origin_override[2];
			}
			else
			{
				game::refdef->org[0] = ps->origin[0];
				game::refdef->org[1] = ps->origin[1];
				game::refdef->org[2] = ps->origin[2] + ps->viewHeightCurrent;
			}

			camera_origin[0] = game::refdef->org[0];
			camera_origin[1] = game::refdef->org[1];
			camera_origin[2] = game::refdef->org[2];
		}

		// MWR: there is no rebound equivalent of H2's set_viewpos_now call
		// (0x1403ACB2D). Hook CG_DrawActiveFrame's CG_CalcViewValues call
		// (0x140186B9E -> 0x140189AB0) instead: the native view is final when it
		// returns and every later consumer (FX camera publication at 0x140238840,
		// scene owner 0x1401A32F0) reads the HMD-applied refdef. The H2 campaign
		// sequence, fixed sniper, mounted, ladder, notebook, javelin and laser
		// camera adapters are MW2-specific and are not invoked here.
		constexpr std::uintptr_t calc_view_values_call_site = 0x140186B9E;
		constexpr std::uintptr_t calc_view_values = 0x140189AB0;
		constexpr std::uintptr_t camera_lod_helper = 0x140593AA0;
		constexpr std::uintptr_t camera_primary_globals = 0x14F79D2A0;

		void set_viewpos_now_stub(const int local_client_num)
		{
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			auto* const a1 = reinterpret_cast<void*>(static_cast<std::uintptr_t>(local_client_num));
			vr::engine_stereo_renderer::observe_camera_state(
				vr::engine_stereo_renderer::camera_observation_source::set_viewpos_now,
				vr::engine_stereo_renderer::camera_observation_stage::enter,
				a1, nullptr, 0.0f, caller);
			utils::hook::invoke<void>(calc_view_values, local_client_num);
			paused_free_move();
			const auto* const ps = game::CG_GetPredictedPlayerState(local_client_num);
			// Physical ladder: preview the held-hand pull between server samples.
			if (ps)
				vr::gameplay::ladders::apply_camera_origin(game::refdef->org, ps->viewHeightCurrent,
					vr::gameplay::ladders::owned_carrier(), ps->commandTime);
			// Story rigs that throw the body around (rope descents, crashes): the
			// authored eye with no room-scale offset, centring the player.
			vr::game_view::camera_request request{};
			if (vr::gameplay::scripted_motion::active())
				request.policy.translation = vr::game_view::head_translation::fixed;
			// Fixed sniper (flat scope view): the native turret camera, no head pose
			// (original Overlord thermal_scope camera request).
			if (const auto sniper = vr::gameplay::fixed_sniper::current(ps).epoch)
				request = {vr::game_view::camera_profiles::fixed_scope, sniper | (1ull << 63)};
			// Held Barrett: the barrel steers the native angles, so the camera holds
			// its entry heading and the head turns and moves freely.
			else if (const auto barrett = vr::gameplay::barrett_gun::epoch())
				request = {{vr::game_view::camera_owner::script, vr::game_view::head_rotation::free,
					vr::game_view::head_translation::tracked}, barrett | (1ull << 61)};
			if (ps && vr::head_pose_bridge::apply_camera(game::refdef->org, game::refdef->axis,
				ps->commandTime, game::cgs->refdefViewAngles, request))
			{
				camera_origin[0] = game::refdef->org[0];
				camera_origin[1] = game::refdef->org[1];
				camera_origin[2] = game::refdef->org[2];
				// Javelin CLU at the eye: only the scene/HUD camera moves to the
				// launcher (tracking and hand space stay HMD-owned), as on H2.
				vr::gameplay::weapons::javelin_screen::apply_camera(game::refdef->org, game::refdef->axis,
					!request.policy.owns_rotation(), &game::refdef->fovX);
				// MWR: CG_CalcViewValues copies the view origin into refdef.viewOffset
				// (+0x58, the origin viewmodel DObj matrices and tag queries are relative
				// to) before this hook applies the HMD pose. H2 recomputed it after the
				// pose (0x1403B07C0), so hands and the scene shared the tracked head.
				auto* const view_offset = reinterpret_cast<float*>(reinterpret_cast<std::byte*>(&game::refdef->org[0]) - 0x20 + 0x58);
				for (int i = 0; i < 3; ++i) view_offset[i] = game::refdef->org[i];
			}
			vr::engine_stereo_renderer::observe_camera_state(
				vr::engine_stereo_renderer::camera_observation_source::set_viewpos_now,
				vr::engine_stereo_renderer::camera_observation_stage::leave,
				a1, nullptr, 0.0f, caller);
		}

		void sub_140781090_stub(void* a1, void* a2, float a3)
		{
			const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
			vr::engine_stereo_renderer::observe_camera_state(
				vr::engine_stereo_renderer::camera_observation_source::camera_helper,
				vr::engine_stereo_renderer::camera_observation_stage::before_original,
				a1, a2, a3, caller);
			sub_140781090_hook.invoke<void>(a1, a2, a3);
			vr::engine_stereo_renderer::observe_camera_state(
				vr::engine_stereo_renderer::camera_observation_source::camera_helper,
				vr::engine_stereo_renderer::camera_observation_stage::after_original,
				a1, a2, a3, caller);

			if (is_free_move_enabled() || vr::head_pose_bridge::camera_was_applied())
			{
				const auto lod_origin = reinterpret_cast<float*>(camera_primary_globals);
				lod_origin[0] = game::refdef->org[0];
				lod_origin[1] = game::refdef->org[1];
				lod_origin[2] = game::refdef->org[2];
				vr::engine_stereo_renderer::observe_camera_state(
					vr::engine_stereo_renderer::camera_observation_source::camera_helper,
					vr::engine_stereo_renderer::camera_observation_stage::after_mod,
					a1, a2, a3, caller);
			}
		}
	}

	void enable_free_move()
	{
		if (cg_paused && *cg_paused) (*cg_paused)->current.integer = 2;
	}

	void disable_free_move()
	{
		if (cg_paused && *cg_paused) (*cg_paused)->current.integer = 0;
	}

	bool is_free_move_enabled()
	{
		return cg_paused && *cg_paused && (*cg_paused)->current.integer == 2;
	}

	void set_camera_position(const float x, const float y, const float z)
	{
		camera_origin_override[0] = x;
		camera_origin_override[1] = y;
		camera_origin_override[2] = z;
	}

	void set_camera_angles(const float x, const float y, const float z)
	{
		camera_angles_override[0] = x;
		camera_angles_override[1] = y;
		camera_angles_override[2] = z;
	}

	void set_using_origin_override(bool value)
	{
		using_camera_override = value;
	}

	void set_using_angles_override(bool value)
	{
		using_camera_angles_override = value;
	}

	ui_scripting::table get_camera_position()
	{
		ui_scripting::table res;
		res["x"] = camera_origin[0];
		res["y"] = camera_origin[1];
		res["z"] = camera_origin[2];
		return res;
	}

	void set_callback(const ui_scripting::script_value& callback_)
	{
		callback.access([=](cb_type& cb)
		{
			cb = [=]()
			{
				try
				{
					const auto state = *game::hks::lua_state;
					const ui_scripting::table globals = state->globals.v.table;
					const auto pcall = globals["pcall"];
					const auto results = pcall(callback_);

					if (!results[0].as<bool>())
					{
						const auto error = results[1].as<std::string>();
						console::error("Error executing camera callback: %s\n", error.data());
					}
				}
				catch (const std::exception& e)
				{
					console::error("Error executing camera callback: %s\n", e.what());
				}
			};
		});
	}

	void clear_lua()
	{
		callback.access([=](cb_type& cb)
		{
			cb.reset();
		});
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			cl_free_move_scale = dvars::register_float("cl_freemoveScale", 1.f, 0.0001f, 100.f,
				game::DVAR_FLAG_SAVED, "Scale how fast you move in cl_freemove mode");

			utils::hook::call(calc_view_values_call_site, set_viewpos_now_stub);
			sub_140781090_hook.create(camera_lod_helper, sub_140781090_stub);
		}
	};
}

REGISTER_COMPONENT(camera::component)
