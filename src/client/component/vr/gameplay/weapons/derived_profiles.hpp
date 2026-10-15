#pragma once
#include "../weapon_profile.hpp"
#include "../hands/rig_builder.hpp"

#include <span>
#include <string>

namespace vr::gameplay::weapons::derived
{
	// MWR weapons have no authored Overlord recipe yet. Authored profiles take
	// their wrist anchors and finger rotations from the weapon's native idle
	// animation (e.g. M4: h2_wpn_asl_m4a1_idle frame 0). Derived profiles take
	// the same data from the live native skeleton once the hands are still
	// relative to the gun (idle), keyed by the exact viewmodel assembly.
	// Mechanics (reload, bolt, magazine) are not derived; they remain authored.

	// Assembly-keyed lookup used by select_profile after authored recipes.
	[[nodiscard]] const profile* find(std::span<const hands::model_definition> models) noexcept;

	// Native (pre-IK) skeleton observation for an assembly without a profile.
	// Returns true when a new profile has just been finalized.
	bool observe(std::span<const hands::model_definition> models, const hands::rig& rig,
		std::span<const hands::bone_definition> bones, std::span<const hands::bone> native) noexcept;

	[[nodiscard]] std::string status();
}
