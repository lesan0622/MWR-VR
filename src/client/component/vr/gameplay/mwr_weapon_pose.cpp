#include <std_include.hpp>
#include "mwr_weapon_pose.hpp"
#include "../controller_input.hpp"
#include "../head_pose_bridge.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

// MWR hands/weapon presentation and aim. The native viewmodel (weapon and
// arms) is placed at the right controller and the player's shots use the
// controller direction. This is the MWR replacement for H2 Overlord's
// per-weapon skeleton solver, which depends on H2 skinning boundaries and
// measured MW2 weapon recipes.
namespace vr::gameplay::mwr_weapon_pose
{
	namespace
	{
		// CG viewmodel placement 0x14018DA50 converts the computed hand quaternion
		// (+0x00) into cg viewModelAxis 0x142149420 and copies the origin
		// (+0x10) to 0x142149444 right after this QuatToAxis call.
		constexpr std::uintptr_t quat_to_axis_call = 0x14018DB85;
		constexpr std::uintptr_t quat_to_axis = 0x140413490;
		// G weapon fire parameters (H2 0x140518880): forward +0x00, right +0x0C,
		// up +0x18, muzzle origin +0x24, weapon +0x3C. Called from FireWeapon.
		constexpr std::uintptr_t fire_parameters = 0x1402EF730;
		constexpr std::array<std::uintptr_t, 2> fire_parameter_calls{0x1402EFA41, 0x1402F3161};
		// cg viewModelAxis[3] + origin, right-hand viewmodel cpose and DObj, and
		// CG_DObjGetWorldTagMatrix(pose, obj, tag, axis[3][3], origin) as used for
		// tag_camera by 0x140185820. scr_const.tag_weapon_right is set by 0x1403019AC.
		constexpr std::uintptr_t view_model_axis = 0x142149420, view_model_origin = 0x142149444;
		constexpr std::uintptr_t right_pose = 0x1421584A0, right_object = 0x14215B4B0;
		constexpr std::uintptr_t world_tag_matrix = 0x14014D720;
		constexpr std::uintptr_t tag_weapon_right = 0x146663818;

		game::dvar_t* enabled{};
		game::dvar_t* offset_forward{};
		game::dvar_t* offset_left{};
		game::dvar_t* offset_up{};
		game::dvar_t* aim_pitch{};
		game::dvar_t* auto_grip{};
		game::dvar_t* show_arms{};
		int applied_arms = -1;
		std::array<float, 3> measured_grip{};
		std::atomic_uint64_t grip_measurements{}, grip_failures{};
		bool hooks_installed{};
		std::atomic_uint64_t placements{}, placement_misses{}, shots{}, shot_misses{};
		std::array<float, 6> last_native{}, last_hand{}; // Diagnostics: native/VR origin and forward.

		using vec = head_pose_bridge::vector3;

		// Right controller in game world: grip position, aim orientation.
		bool right_hand(head_pose_bridge::world_pose& output) noexcept
		{
			if (!enabled || !enabled->current.enabled) return false;
			const auto input = controller_input::latest_interaction();
			const auto now = controller_input::clock::now();
			if (!input.focused || input.sequence == 0 || now < input.sampled_at ||
				now - input.sampled_at > std::chrono::milliseconds(150))
				return false;
			const auto& grip = input.grip[1];
			const auto& aim = input.aim[1];
			if (!grip.valid || !aim.valid) return false;
			head_pose_bridge::spatial_frame frame{};
			if (!head_pose_bridge::get_spatial_frame(frame) || frame.generation != input.reference_generation)
				return false;
			head_pose_bridge::tracking_pose pose{grip.tracking.position_meters, aim.tracking.orientation};
			if (!head_pose_bridge::tracking_to_world(frame, pose, output)) return false;
			// Optional downward barrel tilt relative to the controller aim ray.
			const float pitch = aim_pitch ? aim_pitch->current.value * 0.017453292f : 0.f;
			if (pitch != 0.f)
			{
				const float c = std::cos(pitch), s = std::sin(pitch);
				const vec f = output.axis[0], u = output.axis[2];
				for (int i = 0; i < 3; ++i)
				{
					output.axis[0][i] = f[i] * c - u[i] * s;
					output.axis[2][i] = u[i] * c + f[i] * s;
				}
			}
			return true;
		}

		bool gameplay_active() noexcept
		{
			return game::CL_IsCgameInitialized() && *game::keyCatchers == 0;
		}

		// Right-hand weapon tag relative to the previous frame's viewmodel placement.
		// The skeleton follows the placement rigidly, so this is the grip offset in
		// viewmodel space, including the current animation (idle, reload, recoil).
		bool measure_grip(vec& output) noexcept
		{
			const auto object = *reinterpret_cast<void* const*>(right_object);
			const auto tag = *reinterpret_cast<const std::uint32_t*>(tag_weapon_right);
			if (!object || !tag) return false;
			std::array<float, 9> tag_axis{};
			vec tag_origin{};
			if (!utils::hook::invoke<int>(world_tag_matrix, reinterpret_cast<void*>(right_pose), object, tag,
				tag_axis.data(), tag_origin.data()))
				return false;
			const auto* axis = reinterpret_cast<const float*>(view_model_axis);
			const auto* origin = reinterpret_cast<const float*>(view_model_origin);
			const vec delta{tag_origin[0] - origin[0], tag_origin[1] - origin[1], tag_origin[2] - origin[2]};
			float length{};
			for (int row = 0; row < 3; ++row)
			{
				output[row] = delta[0] * axis[row * 3] + delta[1] * axis[row * 3 + 1] + delta[2] * axis[row * 3 + 2];
				length += output[row] * output[row];
			}
			return std::isfinite(length) && length > 1.f && length < 60.f * 60.f;
		}

		// The viewmodel moves rigidly, so its shoulders no longer meet a body.
		// Optionally hide both arm hierarchies (this also hides the hands).
		void update_arms() noexcept
		{
			const int wanted = show_arms->current.enabled ? 1 : 0;
			if (wanted == applied_arms) return;
			applied_arms = wanted;
			for (const auto* bone : {"j_shoulder_le", "j_shoulder_ri"})
			{
				const auto name = utils::hook::invoke<std::uint32_t>(0x1403C16F0, bone);
				utils::hook::invoke<void>(wanted ? 0x140196240 : 0x140192F30, 0, name);
			}
		}

		void quat_to_axis_stub(float* quat_origin, float (*axis)[3])
		{
			update_arms();
			vec grip{};
			const bool measured = auto_grip && auto_grip->current.enabled && measure_grip(grip);
			if (measured) { measured_grip = grip; ++grip_measurements; }
			else ++grip_failures;
			utils::hook::invoke<void>(quat_to_axis, quat_origin, axis);
			head_pose_bridge::world_pose hand{};
			if (!quat_origin || !gameplay_active() || !right_hand(hand))
			{
				++placement_misses;
				return;
			}
			// The viewmodel origin is the native view point; the model's right hand
			// sits forward/right/down of it. Offset back so the grip meets the hand.
			last_native = {quat_origin[4], quat_origin[5], quat_origin[6], axis[0][0], axis[0][1], axis[0][2]};
			last_hand = {hand.position[0], hand.position[1], hand.position[2], hand.axis[0][0], hand.axis[0][1], hand.axis[0][2]};
			const vec offset = measured ? grip :
				vec{offset_forward->current.value, offset_left->current.value, offset_up->current.value};
			for (int row = 0; row < 3; ++row)
				for (int i = 0; i < 3; ++i)
					axis[row][i] = hand.axis[row][i];
			for (int i = 0; i < 3; ++i)
				quat_origin[4 + i] = hand.position[i] - hand.axis[0][i] * offset[0] - hand.axis[1][i] * offset[1] -
					hand.axis[2][i] * offset[2];
			++placements;
		}

		void fire_parameters_stub(game::gentity_s* entity, float* parameters, int event_parameter)
		{
			utils::hook::invoke<void>(fire_parameters, entity, parameters, event_parameter);
			head_pose_bridge::world_pose hand{};
			if (!parameters || entity != &game::g_entities[0] || !gameplay_active() || !right_hand(hand))
			{
				++shot_misses;
				return;
			}
			// Bullets leave the eye point the native code chose but follow the
			// controller ray, aimed at where the barrel ray meets far range.
			// This avoids starting traces inside nearby walls from the hand.
			constexpr float range = 8192.f;
			vec target{}, direction{};
			float length{};
			for (int i = 0; i < 3; ++i)
				target[i] = hand.position[i] + hand.axis[0][i] * range;
			for (int i = 0; i < 3; ++i)
			{
				direction[i] = target[i] - parameters[9 + i];
				length += direction[i] * direction[i];
			}
			length = std::sqrt(length);
			if (!std::isfinite(length) || length < 1.f)
			{
				++shot_misses;
				return;
			}
			for (int i = 0; i < 3; ++i)
			{
				parameters[0 + i] = direction[i] / length; // forward
				parameters[3 + i] = -hand.axis[1][i];      // right
				parameters[6 + i] = hand.axis[2][i];       // up
			}
			++shots;
		}

		bool check_call(std::uintptr_t site, std::uintptr_t target)
		{
			std::array<std::uint8_t, 5> bytes{0xE8}, mask{0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
			const auto displacement = static_cast<std::int32_t>(target - site - 5);
			std::memcpy(bytes.data() + 1, &displacement, sizeof(displacement));
			return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(site),
				{bytes.data(), mask.data(), bytes.size()}));
		}
	}

	bool active() noexcept
	{
		head_pose_bridge::world_pose hand{};
		return hooks_installed && gameplay_active() && right_hand(hand);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			enabled = dvars::register_bool("vr_weaponHand", true, game::DVAR_FLAG_SAVED,
				"Place the weapon/arms viewmodel at the right controller and aim shots with it");
			offset_forward = dvars::register_float("vr_weaponOffsetForward", 14.f, -50.f, 50.f, game::DVAR_FLAG_SAVED,
				"Viewmodel right-hand grip position forward of the view origin (units)");
			offset_left = dvars::register_float("vr_weaponOffsetLeft", -6.f, -50.f, 50.f, game::DVAR_FLAG_SAVED,
				"Viewmodel right-hand grip position left of the view origin (units)");
			offset_up = dvars::register_float("vr_weaponOffsetUp", -6.f, -50.f, 50.f, game::DVAR_FLAG_SAVED,
				"Viewmodel right-hand grip position above the view origin (units)");
			auto_grip = dvars::register_bool("vr_weaponAutoGrip", true, game::DVAR_FLAG_SAVED,
				"Align the viewmodel's right-hand weapon tag with the controller (else use vr_weaponOffset*)");
			show_arms = dvars::register_bool("vr_weaponArms", true, game::DVAR_FLAG_SAVED,
				"Show the viewmodel arms and hands (off shows only the weapon)");
			aim_pitch = dvars::register_float("vr_weaponAimPitch", 0.f, -45.f, 45.f, game::DVAR_FLAG_SAVED,
				"Extra downward barrel tilt relative to the controller aim ray (degrees)");
			command::add("vr_weapon_status", []
			{
				console::info("[VR weapon] hooks=%d enabled=%d placements=%llu misses=%llu shots=%llu shot_misses=%llu\n",
					hooks_installed, enabled->current.enabled, placements.load(), placement_misses.load(),
					shots.load(), shot_misses.load());
				console::info("[VR weapon] native origin=(%.1f %.1f %.1f) fwd=(%.2f %.2f %.2f) hand=(%.1f %.1f %.1f) fwd=(%.2f %.2f %.2f)%c",
					last_native[0], last_native[1], last_native[2], last_native[3], last_native[4], last_native[5],
					last_hand[0], last_hand[1], last_hand[2], last_hand[3], last_hand[4], last_hand[5], 10);
				console::info("[VR weapon] grip tag measured=%llu failed=%llu offset fwd/left/up=(%.1f %.1f %.1f)%c",
					grip_measurements.load(), grip_failures.load(), measured_grip[0], measured_grip[1], measured_grip[2], 10);
			});

			// CG "hidepartvm"/"unhidepartvm" server commands (0x14016DD90) call these
			// with an SL string from SL_GetString 0x1403C16F0.
			command::add("vr_weapon_hidepart", [](const command::params& params)
			{
				if (params.size() < 2) return;
				const auto name = utils::hook::invoke<std::uint32_t>(0x1403C16F0, params.get(1));
				utils::hook::invoke<void>(0x140192F30, 0, name);
			});
			command::add("vr_weapon_showpart", [](const command::params& params)
			{
				if (params.size() < 2) return;
				const auto name = utils::hook::invoke<std::uint32_t>(0x1403C16F0, params.get(1));
				utils::hook::invoke<void>(0x140196240, 0, name);
			});

			bool valid = check_call(quat_to_axis_call, quat_to_axis);
			for (const auto site : fire_parameter_calls)
				valid = valid && check_call(site, fire_parameters);
			if (!valid)
			{
				console::error("[VR weapon] MWR viewmodel/fire call-site validation failed; weapon hand disabled\n");
				return;
			}
			utils::hook::call(quat_to_axis_call, quat_to_axis_stub);
			for (const auto site : fire_parameter_calls)
				utils::hook::call(site, fire_parameters_stub);
			hooks_installed = true;
		}
	};
}

REGISTER_COMPONENT(vr::gameplay::mwr_weapon_pose::component)
