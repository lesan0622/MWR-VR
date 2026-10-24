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
	// The local player's current native weapon (gentity weapon, +0x80) is an AC-130 gun.
	inline bool gunship() noexcept
	{
		if (!game::CL_IsCgameInitialized()) return false;
		std::uint32_t token{};
		return utils::native_memory::read_bytes(&token, reinterpret_cast<const std::byte*>(&game::g_entities[0]) + 0x80, sizeof(token)) &&
		       weapon(token);
	}
	// The player works a native gun with the view: a mounted turret (playerState
	// e_flags 0x3000, e.g. the sniperescape Barrett) or a vehicle turret with no
	// weapon of their own (0x100000, e.g. the airlift Mk19). Riding with one's own
	// weapon (jeepride) stays in normal VR.
	inline bool mounted() noexcept
	{
		if (!game::CL_IsCgameInitialized()) return false;
		const auto* ps = game::CG_GetPredictedPlayerState(0);
		std::uint32_t flags{}, token{};
		if (!ps || !utils::native_memory::read_bytes(&flags, reinterpret_cast<const std::byte*>(ps) + offsetof(game::playerState_s, e_flags), sizeof(flags)))
			return false;
		if (flags & 0x3000) return true;
		return (flags & 0x100000) &&
		       utils::native_memory::read_bytes(&token, reinterpret_cast<const std::byte*>(&game::g_entities[0]) + 0x80, sizeof(token)) &&
		       !(token & 0x1ff);
	}
	// Presented as the native flat view (the AC-130 cabin or theater) and aimed with
	// the stick. Turrets stay in stereo VR, aimed by the head like any native view.
	inline bool active() noexcept { return gunship(); }
}
