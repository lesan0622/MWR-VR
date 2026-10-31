#pragma once
#include "hands/skeleton.hpp"
#include "hands/pose_solver.hpp"

#include <span>

namespace vr::gameplay::weapon_capture
{
	// Read-only native weapon capture for authoring MWR recipes, the in-game
	// counterpart of the offline model/animation analysis behind Overlord's
	// H2 recipes. Commands:
	//   vr_capture_weapon        assembly, bind pose and WeaponDef/XAnim data
	//   vr_capture_record 1|0    per-frame pre-IK skeleton (gun-local) + clips
	// Output: overlord-mwr/captures/<weapon>/.

	// True while a snapshot or recording is pending (avoids per-frame describe()).
	[[nodiscard]] bool wants() noexcept;

	// Hand solver hook: the complete native skeleton before any VR change.
	void observe(const void* tree, std::uint32_t weapon, std::span<const hands::model_definition> models,
		const hands::rig& rig, std::span<const hands::bone_definition> bones,
		std::span<const hands::bone> native) noexcept;
}
