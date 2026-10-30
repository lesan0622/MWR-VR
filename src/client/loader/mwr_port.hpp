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
		"scripting", // Builtin calls, entity fields and level lifetime (no GSC loading/Lua).
		// Phase 2: VR runtime, native stereo and head tracking
		"d3d11",
		"camera",
		"vr",
		"vr::engine_stereo_renderer",
		// Phase 3: VR menu panels and native HUD capture
		"input",
		"vr::native_menu",
		"vr::native_hud_capture",
		"vr::narrative_ui", // Subtitles, titles and fades on their own VR panel.
		"vr::menu_backdrop",
		"vr::native_waypoints",
		"vr::directional_ui", // World-anchored HUD ink: VR item labels and objective markers.
		"vr::gunship_cabin", // AC-130 played on a fire-control station screen (data/ac130_cabin).
		"vr::objective_markers", // Current objective marked in the world (compass star in VR).
		"vr::controllers",
		// "vr::gameplay::mwr_weapon_pose" (simplified rigid viewmodel) is replaced by the original hands solver.
		"vr::gameplay::hands",
		"vr::gameplay::weapons",
		"vr::gameplay::weapon_capture",
		"scene_models",
		"fastfiles::mwr_unload", // Zone unload boundary only: retires runtime models/parts before assets free.
		"vr::gameplay::weapons::reload_boundary",
		"vr::gameplay::weapons::physical_reload",
		"vr::gameplay::weapons::physical_reload::presentation_component",
		"vr::gameplay::weapons::native_closed_bolt",
		"vr::gameplay::weapons::feedback", // Weapon interaction sounds (MWR aliases) and haptics.
		"vr::gameplay::weapons::independent_fire", // Carried guns fire from the hand muzzle (native PM fire is suppressed).
		"vr::gameplay::weapons::tube", // Pump/lever tube feeds (physical pump and shell loading).
		"vr::gameplay::weapons::tube::presenter_component",
		"vr::gameplay::interaction_coordinator",
		"vr::gameplay::reload_items",
		"vr::gameplay::falling_item_presentation",
		"vr::gameplay::hand_interaction",
		"vr::gameplay::weapons::carry",
		"vr::gameplay::hands::empty_native",
		"vr::gameplay::interaction",
		"vr::gameplay::interaction::hud_component", // VR item labels ("Grip to pick up") replace the stock cursor hint.
		"vr::controller_calibration",
		"vr::gameplay::weapons::viewmodel_visibility::visibility_component",
		"vr::gameplay::grenades", // Physical frag/flash/smoke: chest grasp, pin, throw (native G_FireGrenade).
		"vr::gameplay::equipment", // Physical knife melee (native trace + G_Damage).
		"vr::gameplay::equipment::chest_items",
		"vr::gameplay::equipment::nightvision", // Hand-to-goggles gesture presses the native NVG action slot.
		"vr::gameplay::weapons::optics", // Same-eye scope magnifier (no auxiliary scene view).
		"vr::gameplay::weapons::chambering_guide", // Yellow pulse on the action to rack.
		"vr::gameplay::npc_collision", // Closer player/NPC contact (15 -> 9 unit radius); walls keep native clearance.
		"vr::gameplay::ladders", // Physical ladder grips (vr_physicalLadders 0 keeps the native ladder).
		"vr::gameplay::ammo_counter", // Held gun's loaded/reserve count above its wrist.
		"vr::stereo_postfx", // Camera motion blur off in stereo (per-eye reprojection smears a rectangle).
		"vr::gameplay::weapons::javelin_screen", // Javelin CLU display at the eye; lock gate via playerads.
		"vr::gameplay::weapons::launcher::native_component", // Javelin lock projection from the launcher (Target_IsInRect).
		"vr::gameplay::fixed_sniper", // sniperescape Barrett: hand-drag/stick aim, flat native scope view.
		"vr::gameplay::barrett_gun", // sniperescape Barrett held as a VR rifle (physical scope); replaces fixed_sniper there.
		"vr::gameplay::weapon_laser", // Weapon lasers (NVG IR) from the VR muzzle; native beam 0x140159560 replaced.
		"vr::gameplay::weapons::underbarrel", // M203 (MWR viewmodel j_m203_slide/j_grenade): grip, slide, load, fire (FireWeapon launcher 0x1402F3B10).
		"vr::gameplay::c4_detonator", // maps/_c4 detonator: trigger press sends the player's "detonate".
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
