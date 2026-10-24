#include <std_include.hpp>
#include "scripted_motion.hpp"
#include "ladder_runtime.hpp"
#include "view_aimed_weapon.hpp"
#include "game/game.hpp"
#include <utils/native_memory.hpp>

namespace vr::gameplay::scripted_motion
{
	int linked_parent() noexcept
	{
		const void* tag{};
		const game::gentity_s* parent{};
		if (!utils::native_memory::read_bytes(&tag, reinterpret_cast<const std::byte*>(&game::g_entities[0]) + 0x208, sizeof(tag)) || !tag ||
		    !utils::native_memory::read_bytes(&parent, tag, sizeof(parent)) || !parent)
			return -1;
		const auto address = reinterpret_cast<std::uintptr_t>(parent), base = reinterpret_cast<std::uintptr_t>(game::g_entities.get());
		if (address < base || (address - base) % sizeof(game::gentity_s)) return -1;
		const auto index = (address - base) / sizeof(game::gentity_s);
		return index < game::ENTITYNUM_WORLD ? int(index) : -1;
	}
	bool active() noexcept
	{
		if (!game::CL_IsCgameInitialized() || view_aimed::gunship() || view_aimed::mounted()) return false;
		const int parent = linked_parent();
		if (parent < 0 || parent == ladders::owned_carrier()) return false;
		const auto* ps = game::CG_GetPredictedPlayerState(0);
		std::uint32_t weapon_flags{};
		return ps && utils::native_memory::read_bytes(&weapon_flags, reinterpret_cast<const std::byte*>(ps) + 0x390, sizeof(weapon_flags)) &&
		       (weapon_flags & 0x80);
	}
}
