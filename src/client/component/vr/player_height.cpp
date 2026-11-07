#include <std_include.hpp>
#include "player_height.hpp"
#include "settings.hpp"
#include "controller_haptics.hpp"
#include "controller_input.hpp"
#include "head_pose_bridge.hpp"
#include "gameplay/weapon_holsters.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "game/dvars.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

// vr_playerHeight (cm) is entered in the launcher or measured in game: Options >
// VR "Measure" or a 3 s menu/recenter button hold (recenter fires at 1 s) arms
// it, then a T-pose with both triggers pulled measures height, shoulders and
// arm length (user design); vr_measure_height measures at once. The HMD's floor height comes from the OpenXR stage
// space or the OpenVR standing universe; runtimes without a floor keep the
// entered value.
namespace vr::player_height
{
	namespace
	{
		game::dvar_t* height{};
		std::atomic<float> last_measured_cm{NAN},last_eye_meters{NAN};
		std::atomic_uint64_t measurements{},rejected{};
		std::atomic<float> waist_below_body{NAN};
		game::dvar_t* arm{};
		game::dvar_t* measured{}; // Successful measurements this session; the VR menu polls it.
		std::string last_arms{"not measured"};

		void apply()
		{
			body_scale = scale_for(height ? height->current.value : reference_cm);
			arm_scale = arm ? arm->current.value : 1.f;
		}
		void pulse(bool accepted)
		{
			const auto frame = controller_input::latest();
			for (int hand = 0; hand < 2; ++hand)
				controller_haptics::request(hand, accepted ? controller_haptics::pulse{.30f, 160.f, .8f, frame.reference_generation, frame.sampled_at}
					: controller_haptics::pulse{.08f, 60.f, .35f, frame.reference_generation, frame.sampled_at});
		}
		void set_dvar(const char* name, float value)
		{
			command::execute(std::format("set {} {:.3f}", name, value), true);
		}
		// T-pose (arms straight out to the sides at shoulder height): the hands
		// give the shoulder height below the eyes, and the span gives the arm.
		// Shoulder half width follows height (joint centres, not the shoulder
		// edge); the arm is the rest of each half span.
		struct tpose_sample {float span{}, drop{};};
		std::optional<tpose_sample> tpose(const controller_input::frame& frame)
		{
			head_pose_bridge::tracking_reference reference{};
			if (!head_pose_bridge::get_tracking_reference(reference) || reference.generation != frame.reference_generation) return {};
			std::array<head_pose_bridge::vector3, 2> hands{};
			for (int hand = 0; hand < 2; ++hand)
				if (!controller_input::interaction_ready(frame.grip[hand]) ||
					!head_pose_bridge::tracking_position(reference, frame.grip[hand].tracking.position_meters, hands[hand])) return {};
			// Recenter-local metres: x forward, y left, z up.
			const auto& head = reference.head_position_meters;
			const float span = std::hypot(hands[0][0] - hands[1][0], hands[0][1] - hands[1][1]);
			const float drop = head[2] - (hands[0][2] + hands[1][2]) * .5f;
			const float side0 = hands[0][1] - head[1], side1 = head[1] - hands[1][1]; // Left hand +y, right hand -y.
			const float forward = std::max(std::abs(hands[0][0] - head[0]), std::abs(hands[1][0] - head[0]));
			const bool valid = span > .9f && span < 2.4f && std::abs(hands[0][2] - hands[1][2]) < .15f && drop > .05f && drop < .45f &&
				side0 > .35f && side1 > .35f && forward < .35f;
			last_arms = std::format("{}: span {:.2f} m, hands {:.2f} m below the eyes, level difference {:.2f} m, forward {:.2f} m",
				valid ? "T-pose" : "not a T-pose", span, drop, std::abs(hands[0][2] - hands[1][2]), forward);
			if (!valid) return {};
			return tpose_sample{span, drop};
		}
		void apply_arms(const tpose_sample& pose, float cm)
		{
			const float half_width = std::clamp(shoulder_half_width_per_height * cm / 100.f, .10f, .35f);
			const float wrist_reach = pose.span * .5f - half_width - grip_to_wrist_m;
			set_dvar("vr_shoulderDown", std::clamp(pose.drop, .05f, .45f));
			set_dvar("vr_shoulderHalfWidth", half_width);
			const float model = model_arm_units.load() / head_pose_bridge::get_status().world_scale;
			std::string result = "arm model unknown (hands not drawn yet)";
			if (std::isfinite(model) && model > .2f && wrist_reach > .2f)
			{
				const float scale = std::clamp(wrist_reach / model, min_arm_scale, max_arm_scale);
				set_dvar("vr_armScale", scale);
				result = std::format("arm {:.2f} m / model {:.2f} m -> vr_armScale {:.3f}", wrist_reach, model, scale);
			}
			last_arms += std::format("; shoulders {:.2f} m below the eyes, half width {:.2f} m, {}", pose.drop, half_width, result);
		}
		// require_tpose: the armed (menu / X-hold) measurement only accepts a T-pose.
		bool measure(bool require_tpose)
		{
			const auto frame = controller_input::latest();
			const auto eye = floor_height();
			const auto cm = eye ? measured_cm(*eye) : std::nullopt;
			if (!cm)
			{
				++rejected;pulse(false);
				console::info(eye ? "[VR height] measurement rejected: eye %.2f m above the floor (stand upright)\n"
					: "[VR height] measurement unavailable: the VR runtime reports no floor height\n", eye.value_or(0.f));
				return false;
			}
			const auto pose = tpose(frame);
			if (!pose && require_tpose)
			{
				++rejected;pulse(false);
				console::info("[VR height] %s (hold both arms straight out to the sides)\n", last_arms.c_str());
				return false;
			}
			++measurements;last_eye_meters = *eye;last_measured_cm = *cm;
			command::execute(std::format("set {} {}", settings::player_height.name, int(*cm)), true);
			if (pose) apply_arms(*pose, *cm);
			if (measured) command::execute(std::format("set vr_heightMeasured {}", measurements.load()), true);
			apply();pulse(true);
			console::info("[VR height] measured %.0f cm (eye %.2f m above the floor); %s\n", *cm, *eye, last_arms.c_str());
			return true;
		}
		// Armed by the menu button or a 3 s X hold: stand in a T-pose and pull
		// both triggers. Both must be released first, so the press that armed
		// it (or a held weapon trigger) never measures.
		controller_input::clock::time_point armed_until{};
		bool triggers_released{};
		void arm_measurement()
		{
			using namespace std::chrono_literals;
			const auto frame = controller_input::latest();
			armed_until = controller_input::clock::now() + 20s;
			triggers_released = false;
			for (int hand = 0; hand < 2; ++hand)
				controller_haptics::request(hand, {.06f, 90.f, .35f, frame.reference_generation, frame.sampled_at});
			console::info("[VR height] armed: stand in a T-pose and pull both triggers (20 s)\n");
		}
		void watch_triggers(const controller_input::frame& frame, controller_input::clock::time_point now)
		{
			if (armed_until == controller_input::clock::time_point{}) return;
			if (now > armed_until)
			{
				armed_until = {};
				console::info("[VR height] measurement timed out\n");
				return;
			}
			const auto& left = frame.trigger[0];
			const auto& right = frame.trigger[1];
			if (!left.active || !right.active) return;
			if (!left.down && !right.down) {triggers_released = true;return;}
			if (!triggers_released || !left.down || !right.down) return;
			triggers_released = false;
			if (measure(true)) armed_until = {};
		}
		// The menu/recenter button held for 3 s arms the measurement once per hold.
		// Watched here, not in the menu gesture: the 1 s recenter resets menu input state.
		void watch_hold()
		{
			using namespace std::chrono_literals;
			static controller_input::clock::time_point since{},seen{};
			static bool held{},fired{};
			const auto frame = controller_input::latest();
			const auto now = controller_input::clock::now();
			const auto& button = frame.menu_recenter;
			if (!frame.focused || now < frame.sampled_at || now - frame.sampled_at > 500ms)
			{
				if (held && now - seen > 500ms) held = fired = false;
				return;
			}
			watch_triggers(frame, now);
			if (!button.active) return;
			seen = now;
			if (!button.down) {held = fired = false;return;}
			if (!held) {held = true;fired = false;since = frame.sampled_at;}
			if (!fired && frame.sampled_at - since >= 3s) {fired = true;arm_measurement();}
		}
		// Status witness: applied default waist holster depth below the body anchor, physical metres.
		void sample_layout()
		{
			head_pose_bridge::spatial_frame frame{};
			if (!head_pose_bridge::get_spatial_frame(frame) || !(frame.units_per_meter > 0)) return;
			if (const auto slots = gameplay::weapons::carry::locate_holsters(frame); slots.valid)
				waist_below_body = (head_pose_bridge::body_slots_frame(frame).head_position[2] - slots.centers[0][2]) / frame.units_per_meter;
		}
	}
	std::string status()
	{
		const auto eye = floor_height();
		// Applied waist holster depth below the body anchor, physical metres.
		return std::format("[VR height] vr_playerHeight={} body_scale={:.3f} floor_eye_m={} last_measured_cm={} measurements={} rejected={} waist_below_body_m={:.3f}\n"
			"[VR height] vr_armScale={:.3f} model_arm_units={:.2f} last_arms: {}\n",
			height ? height->current.value : NAN, body_scale.load(), eye ? std::format("{:.3f}", *eye) : "unavailable",
			last_measured_cm.load(), measurements.load(), rejected.load(), waist_below_body.load(),
			arm_scale.load(), model_arm_units.load(), last_arms);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			const auto& setting = settings::player_height;
			height = dvars::register_float(setting.name, setting.default_value, setting.min, setting.max, game::DVAR_FLAG_SAVED,
				"Player height in cm; scales body equipment slots and physical crouch/prone thresholds");
			arm = dvars::register_float("vr_armScale", 1.f, min_arm_scale, max_arm_scale, game::DVAR_FLAG_SAVED,
				"Drawn arm length relative to the arm model; a T-pose height measurement sets it");
			apply();
			scheduler::loop(apply, scheduler::pipeline::main, 250ms);
			scheduler::loop(sample_layout, scheduler::pipeline::server, 250ms);
			scheduler::loop(watch_hold, scheduler::pipeline::main);
			measured = dvars::register_int("vr_heightMeasured", 0, 0, 1000000, game::DVAR_FLAG_NONE,
				"Successful height measurements this session (VR options menu refresh)");
			// vr_measure_height: measure now (height, plus arms in a T-pose).
			// vr_measure_height arm: wait for a T-pose and both triggers (VR options menu).
			command::add("vr_measure_height", [](const command::params& params) {
				if (params.size() > 1 && std::string_view(params.get(1)) == "arm") arm_measurement();
				else (void)measure(false);
			});
			command::add("vr_height_status", [] {console::print_text(console::con_type_info, status());});
		}
	};
}

REGISTER_COMPONENT(vr::player_height::component)
