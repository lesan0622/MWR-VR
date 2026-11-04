#include <std_include.hpp>
#include "dog_escape.hpp"
#include "../digital_button_gate.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/scripting/execution.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

namespace vr::gameplay::dog_escape
{
	namespace
	{
		std::atomic_bool attacked_now{};
		std::atomic_uint64_t presses{}, polls{};
		std::array<controller_input::digital_press_gate, 2> grips{}, triggers{};
		controller_input::clock::time_point held_until{};
		bool running{true};
		// Server: dog_combat sets level.player.attacked_by_dog when the dog jumps
		// and clears it when that dog dies (player_view can outlive the sequence).
		void poll()
		{
			if (!running || !game::CL_IsCgameInitialized() || !game::g_entities[0].client) {attacked_now = false;return;}
			try
			{
				const scripting::entity player{game::scr_entref_t{0, 0}};
				const auto by_dog = player.get("attacked_by_dog");
				attacked_now = by_dog.is<int>() && by_dog.as<int>() != 0;
				++polls;
			}
			catch (...) {attacked_now = false;}
		}
	}
	bool attacked() noexcept {return attacked_now.load();}
	bool melee(const controller_input::frame& input, controller_input::clock::time_point now) noexcept
	{
		if (!attacked_now.load() || !input.focused) {grips = {};triggers = {};return false;}
		bool pressed{};
		for (int h = 0; h < 2; ++h)
		{
			pressed |= grips[h].consume(input.squeeze[h]);
			pressed |= triggers[h].consume(input.trigger[h]);
		}
		// meleebuttonpressed() is sampled by a 0.05 s script loop: hold for 150 ms.
		if (pressed) {held_until = now + std::chrono::milliseconds(150);++presses;}
		return now < held_until;
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			scheduler::loop(poll, scheduler::pipeline::server);
			command::add("vr_dog_status", [] {
				console::info("[VR dog] attacked=%d presses=%llu polls=%llu\n", int(attacked_now.load()), presses.load(), polls.load());
			});
		}
		void pre_destroy() override {running = false;}
	};
}
REGISTER_COMPONENT(vr::gameplay::dog_escape::component)
