#pragma once

#include <string_view>

// MWR (H1 SP) port gate.
//
// Overlord was written against MW2CR (H2 SP) and most components carry fixed
// H2 addresses. Every source file still compiles, but a component only
// registers when its native addresses have been rebound to h1_sp64_ship.exe and
// it is listed here. Unlisted components keep their H2 constants and must never
// run against the MWR image.
namespace mwr_port
{
	inline constexpr std::string_view enabled_components[] =
	{
		// Boot core (addresses rebound to h1_sp64_ship.exe)
		"arxan",
		"steam_proxy",
		"game_module",
		"scheduler",
		"command",
		"console",
		"exception",
		"thread_names",
		"dvars",
		"logger",
		"filesystem",
		// Phase 2: VR runtime, native stereo and head tracking
		"d3d11",
		"camera",
		"vr",
		"vr::engine_stereo_renderer",
		// Phase 3: VR menu panels and native HUD capture
		"input",
		"vr::native_menu",
		"vr::native_hud_capture",
		"vr::menu_backdrop",
		"vr::native_waypoints",
		"vr::controllers",
		// "vr::gameplay::mwr_weapon_pose" (simplified rigid viewmodel) is replaced by the original hands solver.
		"vr::gameplay::hands",
		"vr::gameplay::weapons",
		"vr::gameplay::weapon_capture",
	};

	// typeid names look like "class vr::gameplay::foo::component".
	constexpr std::string_view component_id(std::string_view name)
	{
		if (name.starts_with("class ")) name.remove_prefix(6);
		else if (name.starts_with("struct ")) name.remove_prefix(7);

		constexpr std::string_view suffix = "::component";
		if (name.ends_with(suffix)) name.remove_suffix(suffix.size());
		return name;
	}

	constexpr bool is_enabled(const std::string_view type_name)
	{
		const auto id = component_id(type_name);
		for (const auto& entry : enabled_components)
		{
			if (entry == id) return true;
		}

		return false;
	}
}
