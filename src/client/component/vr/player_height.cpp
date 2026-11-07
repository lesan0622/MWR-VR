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

// vr_playerHeight (cm) is entered in the launcher or measured in game: stand
// upright and hold the menu/recenter button for 3 s (recenter fires at 1 s),
// or run vr_measure_height. The HMD's floor height comes from the OpenXR stage
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

		void apply()
		{
			body_scale = scale_for(height ? height->current.value : reference_cm);
		}
		void pulse(bool accepted)
		{
			const auto frame = controller_input::latest();
			for (int hand = 0; hand < 2; ++hand)
				controller_haptics::request(hand, accepted ? controller_haptics::pulse{.30f, 160.f, .8f, frame.reference_generation, frame.sampled_at}
					: controller_haptics::pulse{.08f, 60.f, .35f, frame.reference_generation, frame.sampled_at});
		}
		void measure()
		{
			const auto eye = floor_height();
			const auto cm = eye ? measured_cm(*eye) : std::nullopt;
			if (!cm)
			{
				++rejected;pulse(false);
				console::info(eye ? "[VR height] measurement rejected: eye %.2f m above the floor (stand upright)\n"
					: "[VR height] measurement unavailable: the VR runtime reports no floor height\n", eye.value_or(0.f));
				return;
			}
			++measurements;last_eye_meters = *eye;last_measured_cm = *cm;
			command::execute(std::format("set {} {}", settings::player_height.name, int(*cm)), true);
			apply();pulse(true);
			console::info("[VR height] measured %.0f cm (eye %.2f m above the floor)\n", *cm, *eye);
		}
		// The menu/recenter button held for 3 s measures once per hold. Watched
		// here, not in the menu gesture: the 1 s recenter resets menu input state.
		void watch_hold()
		{
			using namespace std::chrono_literals;
			static controller_input::clock::time_point since{},seen{};
			static bool held{},fired{};
			const auto frame = controller_input::latest();
			const auto now = controller_input::clock::now();
			const auto& button = frame.menu_recenter;
			if (!frame.focused || !button.active || now < frame.sampled_at || now - frame.sampled_at > 500ms)
			{
				if (held && now - seen > 500ms) held = fired = false;
				return;
			}
			seen = now;
			if (!button.down) {held = fired = false;return;}
			if (!held) {held = true;fired = false;since = frame.sampled_at;}
			if (!fired && frame.sampled_at - since >= 3s) {fired = true;measure();}
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
		return std::format("[VR height] vr_playerHeight={} body_scale={:.3f} floor_eye_m={} last_measured_cm={} measurements={} rejected={} waist_below_body_m={:.3f}\n",
			height ? height->current.value : NAN, body_scale.load(), eye ? std::format("{:.3f}", *eye) : "unavailable",
			last_measured_cm.load(), measurements.load(), rejected.load(), waist_below_body.load());
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			const auto& setting = settings::player_height;
			height = dvars::register_float(setting.name, setting.default_value, setting.min, setting.max, game::DVAR_FLAG_SAVED,
				"Player height in cm; scales body equipment slots and physical crouch/prone thresholds");
			apply();
			scheduler::loop(apply, scheduler::pipeline::main, 250ms);
			scheduler::loop(sample_layout, scheduler::pipeline::server, 250ms);
			scheduler::loop(watch_hold, scheduler::pipeline::main);
			// Runs in place: the VR options menu (Engine.ExecNow) reads the new value right after.
			command::add("vr_measure_height", [] {measure();});
			command::add("vr_height_status", [] {console::print_text(console::con_type_info, status());});
		}
	};
}

REGISTER_COMPONENT(vr::player_height::component)
