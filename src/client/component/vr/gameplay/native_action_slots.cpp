#include <std_include.hpp>
#include "native_action_slots.hpp"
#include "native_ammunition.hpp"
#include <utils/native_memory.hpp>
#include "component/scheduler.hpp"
#include "game/game.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::equipment::action_slots
{
	namespace
	{
		// MWR CL key-binding dispatcher (H2 0x1403CF1E0); whitelist 19..26 are the
		// +/-actionslot 1..4 cases of its jump table at 0x1401A92CC.
		constexpr std::uintptr_t execute_binding_address=0x1401A8350;
		bool native_binding_ready()noexcept
		{
			static const bool ready=[] {
				// CL_ExecuteKey bindings 19..26 call the original slot down/up
				// handlers. These names are whitelist bindings, not registered
				// console commands. The down handler sets native NVG input 0x40000.
				constexpr std::array<std::uint32_t,8> cases{0x1a8645,0x1a8653,0x1a8661,0x1a8672,
					0x1a8683,0x1a8694,0x1a86a5,0x1a86b6};
				constexpr std::array<std::uint8_t,9> down{0x33,0xd2,0x8b,0xcb,0xe8,0x72,0x4c,0xfe,0xff};
				constexpr std::array<std::uint8_t,9> up{0x33,0xd2,0x8b,0xcb,0xe8,0x14,0x4e,0xfe,0xff};
				std::array<std::uint32_t,8> actual_cases{};std::array<std::uint8_t,9> a{},b{};
				return utils::native_memory::read_bytes(actual_cases.data(),reinterpret_cast<const void*>(0x1401A9314),sizeof(actual_cases)) && actual_cases==cases &&
					utils::native_memory::read_bytes(a.data(),reinterpret_cast<const void*>(0x1401A8645),a.size()) && a==down &&
					utils::native_memory::read_bytes(b.data(),reinterpret_cast<const void*>(0x1401A8653),b.size()) && b==up &&
					bool(utils::hook_validation::validate_executable_target(reinterpret_cast<const void*>(execute_binding_address)));
			}();
			return ready;
		}
	}
	void request(unsigned index,unsigned type,unsigned weapon,std::function<bool()> authorized)
	{
		if(!scheduler::is_executing(scheduler::pipeline::server) || index>=4 ||
			(type!=1 && type!=3) || (type==1 && (!weapon || weapon>=512)))return;
		const auto at=GetTickCount64(),timeline=weapons::native_ammunition::timeline();const auto* player=game::g_entities[0].client;
		scheduler::once([index,type,weapon,at,timeline,player,authorized=std::move(authorized)]{
			if(!native_binding_ready() || (authorized && !authorized()))return;
			const auto* paused=game::Dvar_FindVar("cl_paused");
			if(!player || !game::CL_IsCgameInitialized() || !game::SV_Loaded() || game::g_entities[0].client!=player ||
				timeline!=weapons::native_ammunition::timeline() || GetTickCount64()-at>250 || !paused || paused->current.integer || *game::keyCatchers)return;
			unsigned current_type{},current_weapon{};
			const auto* bytes=reinterpret_cast<const std::byte*>(player);
			if(!utils::native_memory::read_bytes(&current_type,bytes+0x1de0+index*4,4) || current_type!=type ||
				(type==1 && (!utils::native_memory::read_bytes(&current_weapon,bytes+0x1df0+index*4,4) || current_weapon!=weapon)))return;
			const auto* table=reinterpret_cast<const char* const*>(0x14115ADF0);
			const auto press="+actionslot "+std::to_string(index+1),release="-actionslot "+std::to_string(index+1);
			const unsigned down=19+index*2,up=down+1;
			if(!table[down] || !table[up] || std::string_view(table[down])!=press || std::string_view(table[up])!=release)return;
			// The native dispatcher performs both notifyoncommand delivery and
			// the actual slot action. A separate notification would duplicate it.
			utils::hook::invoke<void>(execute_binding_address,0,down,0,0u);
			scheduler::once([up]{utils::hook::invoke<void>(execute_binding_address,0,up,0,0u);},scheduler::pipeline::main,50ms);
		},scheduler::pipeline::main);
	}
}
