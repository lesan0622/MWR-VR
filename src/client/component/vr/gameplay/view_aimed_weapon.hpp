#pragma once
#include "game/game.hpp"
#include <utils/native_memory.hpp>
#include <string_view>

// MWR AC-130 (ac130_105mm/_40mm/_25mm and their _alt variants): the player is
// script-linked to the gunship and these weapons fire along the native view.
// No VR hand holds them, so the hand-muzzle contract does not apply.
namespace vr::gameplay::view_aimed
{
	inline bool weapon(std::uint32_t token) noexcept
	{
		const auto index = token & 0x1ff;
		if (!index) return false;
		const auto* definition = game::weapon_defs[index];
		const char* name{};
		if (!definition || !utils::native_memory::read_bytes(&name, &definition->szInternalName, sizeof(name)) || !name)
			return false;
		std::array<char, 8> prefix{};
		return utils::native_memory::read_bytes(prefix.data(), name, 6) && std::string_view(prefix.data(), 6) == "ac130_";
	}
	// The local player's current native weapon (gentity weapon, +0x80).
	inline bool active() noexcept
	{
		if (!game::CL_IsCgameInitialized()) return false;
		std::uint32_t token{};
		return utils::native_memory::read_bytes(&token, reinterpret_cast<const std::byte*>(&game::g_entities[0]) + 0x80, sizeof(token)) &&
		       weapon(token);
	}
}
