#pragma once
#include "game/game.hpp"
#include <utils/native_memory.hpp>
#include <string_view>
#include <array>
#include <atomic>

// MWR AC-130 (ac130_105mm/_40mm/_25mm and their _alt variants): the player is
// script-linked to the gunship and these weapons fire along the native view.
// No VR hand holds them, so the hand-muzzle contract does not apply. Javelin
// and Stinger likewise fire natively from their ADS lock view.
namespace vr::gameplay::view_aimed
{
	inline bool weapon_name_starts(std::uint32_t token, std::string_view prefix) noexcept
	{
		const auto index = token & 0x1ff;
		if (!index || prefix.size() > 7) return false;
		const auto* definition = game::weapon_defs[index];
		const char* name{};
		if (!definition || !utils::native_memory::read_bytes(&name, &definition->szInternalName, sizeof(name)) || !name)
			return false;
		std::array<char, 8> text{};
		return utils::native_memory::read_bytes(text.data(), name, prefix.size()) && std::string_view(text.data(), prefix.size()) == prefix;
	}
	inline bool gunship_weapon(std::uint32_t token) noexcept { return weapon_name_starts(token, "ac130_"); }
	// Javelin and Stinger lock on through native ADS (CLU / lock box) and fire
	// natively along the view once locked.
	inline bool lock_launcher(std::uint32_t token) noexcept
	{
		return weapon_name_starts(token, "javelin") || weapon_name_starts(token, "stinger");
	}
	// Set by the controller while the held lock launcher's sight is at the eyes.
	inline std::atomic_bool launcher_sight{};
	// maps/_c4 detonator (bog_a ZPU): the script selects "c4" with an empty clip
	// and waits for the player's native "detonate" notify. No VR hand holds it.
	inline bool scripted_detonator(std::uint32_t token) noexcept
	{
		const auto index = token & 0x1ff;
		if (!index) return false;
		const auto* definition = game::weapon_defs[index];
		const char* name{};
		std::array<char, 4> text{};
		return definition && utils::native_memory::read_bytes(&name, &definition->szInternalName, sizeof(name)) && name &&
		       utils::native_memory::read_bytes(text.data(), name, 3) && std::string_view(text.data(), 3) == std::string_view("c4\0", 3);
	}
	// Native view-aimed fire: the gunship guns, lock launchers in their sight and
	// the scripted C4 detonator.
	// village_assault Cobra designator (air_support): no VR hand holds it either.
	inline bool air_support_designator(std::uint32_t token) noexcept { return weapon_name_starts(token, "cobra_a"); }
	// MWR playerState weapon +0x38C. The ammunition view is invalid for these devices.
	inline std::uint32_t native_weapon(const void* ps) noexcept
	{std::uint32_t token{};return ps && utils::native_memory::read_bytes(&token, static_cast<const std::byte*>(ps) + 0x38c, sizeof(token)) ? token : 0;}
	inline bool weapon(std::uint32_t token) noexcept { return gunship_weapon(token) || lock_launcher(token) || scripted_detonator(token) || air_support_designator(token); }
	// The local player's current native weapon (gentity weapon, +0x80) is an AC-130 gun.
	inline bool gunship() noexcept
	{
		if (!game::CL_IsCgameInitialized()) return false;
		std::uint32_t token{};
		return utils::native_memory::read_bytes(&token, reinterpret_cast<const std::byte*>(&game::g_entities[0]) + 0x80, sizeof(token)) &&
		       gunship_weapon(token);
	}
	// A lock launcher's sight is at the eyes: its native ADS view is presented flat.
	inline bool launcher_view() noexcept
	{
		if (!launcher_sight.load(std::memory_order_relaxed) || !game::CL_IsCgameInitialized()) return false;
		std::uint32_t token{};
		return utils::native_memory::read_bytes(&token, reinterpret_cast<const std::byte*>(&game::g_entities[0]) + 0x80, sizeof(token)) &&
		       lock_launcher(token);
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
	// Published by the fixed-sniper observer while the player works an admitted
	// fixed sniper turret (sniperescape Barrett).
	inline std::atomic_bool fixed_sniper_view{};
	// Presented as the native flat view: the AC-130 (cabin or theater, aimed with
	// the stick), a lock launcher's ADS view (theater, aimed by the head) and the
	// fixed sniper scope (theater, aimed by hand drag/stick). Other turrets stay
	// in stereo VR, aimed by the head like any native view.
	inline bool active() noexcept
	{return gunship() || launcher_view() || (fixed_sniper_view.load(std::memory_order_relaxed) && mounted());}
}
