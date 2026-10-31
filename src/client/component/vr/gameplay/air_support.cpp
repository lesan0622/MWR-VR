#include <std_include.hpp>
#include "air_support.hpp"
#include "native_action_slots.hpp"
#include "weapon_carry_runtime.hpp"
#include "../head_pose_bridge.hpp"
#include "../digital_button_gate.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/scripting/execution.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/native_memory.hpp>

namespace vr::gameplay::weapons::reload_boundary {std::string fire_gate_status();}
namespace vr::controllers {std::string fire_input_status();}
namespace vr::gameplay::air_support
{
	namespace
	{
		using vec = head_pose_bridge::vector3;
		constexpr std::string_view device = "cobra_air_support";
		std::atomic<int> pointer{-1};
		int gesture_hand{-1};
		std::array<controller_input::digital_press_gate, 2> grips{};
		std::string previous_weapon;
		// The VR carry stows the held gun when the designator is selected. Draw it
		// back into the same hand once the designator is put away or fired.
		struct remembered {weapons::carry::identity id{};vr::hand hand{vr::hand::none};};
		remembered restore{};
		bool was_active{};
		std::chrono::steady_clock::time_point ended{};
		std::atomic_uint64_t restores{};
		std::atomic_uint64_t selections{}, cancels{}, views{};
		std::atomic<const char*> reason{"waiting"};
		bool running{true};
		const char* weapon_name(std::uint32_t token) noexcept
		{
			const auto index = token & 0x1ff;
			if (!index) return nullptr;
			const auto* definition = game::weapon_defs[index];
			const char* name{};
			return definition && utils::native_memory::read_bytes(&name, &definition->szInternalName, sizeof(name)) ? name : nullptr;
		}
		std::uint32_t current_weapon() noexcept
		{
			std::uint32_t token{};
			utils::native_memory::read_bytes(&token, reinterpret_cast<const std::byte*>(&game::g_entities[0]) + 0x80, sizeof(token));
			return token & 0x1ff;
		}
		struct slot {int index{-1};std::uint32_t weapon{};};
		// MWR action slots: types ps+0x1DE0, weapons ps+0x1DF0 (native_action_slots).
		slot designator_slot() noexcept
		{
			const auto* ps = reinterpret_cast<const std::byte*>(game::g_entities[0].client);
			if (!ps) return {};
			for (int i = 0; i < 4; ++i)
			{
				std::uint32_t type{}, weapon{};
				if (!utils::native_memory::read_bytes(&type, ps + 0x1de0 + i * 4, 4) ||
					!utils::native_memory::read_bytes(&weapon, ps + 0x1df0 + i * 4, 4)) return {};
				const auto* name = type == 1 ? weapon_name(weapon) : nullptr;
				if (name && std::string_view(name) == device) return {i, weapon};
			}
			return {};
		}
		bool selected() noexcept
		{
			const auto* name = weapon_name(current_weapon());
			return name && std::string_view(name) == device;
		}
		// The hand at the side of the head, ear height, as a radio handset.
		bool at_ear(const head_pose_bridge::spatial_frame& frame, const controller_input::frame& input, int h) noexcept
		{
			head_pose_bridge::world_pose hand;
			if (!input.grip[h].valid || !head_pose_bridge::tracking_to_world(frame, input.grip[h].tracking, hand)) return false;
			vec d{};
			for (int i = 0; i < 3; ++i) d[i] = (hand.position[i] - frame.head_position[i]) / frame.units_per_meter;
			float lateral{}, forward{};
			for (int i = 0; i < 3; ++i) {forward += d[i] * frame.head_yaw_axis[0][i];lateral += d[i] * frame.head_yaw_axis[1][i];}
			const float side = h == 0 ? lateral : -lateral; // Left hand on the left side.
			return side >= .05f && side <= .25f && std::abs(forward) <= .15f && std::abs(d[2]) <= .15f;
		}
		void tick()
		{
			if (!running || !game::CL_IsCgameInitialized() || !game::g_entities[0].client) {pointer = -1;return;}
			const auto s = designator_slot();
			const bool active = selected();
			if (!s.weapon && !active) {pointer = -1;gesture_hand = -1;reason = "no air support slot";return;}
			const auto input = controller_input::latest_interaction();
			head_pose_bridge::spatial_frame frame;
			const bool tracked = input.focused && head_pose_bridge::get_spatial_frame(frame) && frame.generation == input.reference_generation;
			for (int h = 0; h < 2; ++h)
			{
				if (!grips[h].consume(input.squeeze[h]) || !tracked || !at_ear(frame, input, h)) continue;
				if (!active && s.weapon)
				{
					const auto* name = weapon_name(current_weapon());
					previous_weapon = name ? name : "";
					gesture_hand = h;
					restore = {};
					for (const auto& held : weapons::carry::held_instances())
						if (held.id && vr::valid_hand(held.owner.rear)) restore = {held.id, held.owner.rear};
					equipment::action_slots::request(unsigned(s.index), 1, s.weapon);
					++selections;reason = "designator requested";
				}
				else if (active)
				{
					try
					{
						const scripting::entity player{game::scr_entref_t{0, 0}};
						if (!previous_weapon.empty()) player.call("switchtoweapon", {previous_weapon});
						++cancels;reason = "designator put away";
					}
					catch (...) {reason = "put away rejected";}
				}
			}
			if (was_active && !active) ended = std::chrono::steady_clock::now();
			was_active = active;
			if (!active && restore.id && vr::valid_hand(restore.hand))
			{
				if (std::chrono::steady_clock::now() - ended > 3s) restore = {};
				else if (weapons::carry::hand_has_weapon(restore.hand)) restore = {}; // Already back in hand.
				else
				{
					for (const auto& item : weapons::carry::visible_instances(false))
						if (item.id == restore.id && weapons::carry::ordinary_slot(item.at))
						{
							if (weapons::carry::debug_draw(item.at, restore.hand)) {++restores;restore = {};}
							break;
						}
				}
			}
			if (active && gesture_hand < 0) gesture_hand = 0; // Selected natively (keyboard).
			pointer = active ? gesture_hand : -1;
			if (active) reason = "designating";
		}
	}
	int pointing_hand() noexcept {return pointer.load();}
	bool apply_view(const controller_input::frame& input, float* angles, const float* delta_angles) noexcept
	{
		const int h = pointer.load();
		head_pose_bridge::spatial_frame frame;
		if (h < 0 || !input.focused || !input.aim[h].valid || !head_pose_bridge::get_spatial_frame(frame) ||
			frame.generation != input.reference_generation) return false;
		// Pointing direction (OpenXR -Z) in the HMD tracking reference, H2 axes.
		const auto& r = frame.reference.orientation;const auto& o = input.aim[h].tracking.orientation;
		vec t{}, local{};
		for (int i = 0; i < 3; ++i) t[i] = -o[i][2];
		for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) local[i] += r[j][i] * t[j];
		const vec forward{-local[2], -local[0], local[1]};
		const float horizontal = std::hypot(forward[0], forward[1]);
		if (!(horizontal > .05f)) return false;
		const float pitch = std::atan2(forward[2], horizontal) * 57.29577951308232f;
		const float yaw = std::atan2(forward[1], forward[0]) * 57.29577951308232f;
		if (!head_pose_bridge::apply_game_view_local(angles, delta_angles, pitch, yaw)) return false;
		++views;
		return true;
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			scheduler::loop(tick, scheduler::pipeline::server);
			scripting::on_level_start([] {pointer = -1;gesture_hand = -1;previous_weapon.clear();grips = {};});
			command::add("vr_air_support_status", [] {
				console::info("[VR air support] slot=%d active=%d pointer=%d selections=%llu cancels=%llu views=%llu restores=%llu reason=%s\n",
					designator_slot().index, int(selected()), pointer.load(), selections.load(), cancels.load(), views.load(), restores.load(), reason.load());
				const auto gate = weapons::reload_boundary::fire_gate_status() + controllers::fire_input_status();
				console::info("%s", gate.c_str());
			});
		}
		void pre_destroy() override {running = false;}
	};
}
REGISTER_COMPONENT(vr::gameplay::air_support::component)
