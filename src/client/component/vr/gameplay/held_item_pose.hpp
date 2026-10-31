#pragma once
#include "hands/position_offset.hpp"
#include "../controller_input.hpp"
#include "../head_pose_bridge.hpp"
#include "../settings.hpp"
#include "game/game.hpp"

// MWR: no render-time hand skeleton record exists, so props placed at the hand
// solve and shifted by the view-origin change lag behind the tracked hand. Held
// props (C4 detonator, pistol magazine, knife, grenades) are placed at submit
// time from the latest tracked visual wrist instead.
namespace vr::gameplay::held_item
{
	inline hands::position_offsets alignment_offsets() noexcept
	{
		hands::position_offsets offsets;
		const auto setting = [](const char* key, float fallback)
		{
			const auto* d = game::Dvar_FindVar(key);
			return d ? d->current.value : fallback;
		};
		offsets = {setting(vr::settings::active_hand_alignment()[0].name, offsets.inward_meters),
		           setting(vr::settings::active_hand_alignment()[1].name, offsets.back_meters),
		           setting(vr::settings::active_hand_alignment()[2].name, offsets.up_meters)};
		return offsets;
	}
	// The visual wrist target (controller aim rotation, calibrated grip point).
	inline bool visual_wrist(const controller_input::frame& input, int hand, hands::anchor& out) noexcept
	{
		head_pose_bridge::spatial_frame body;
		return hand >= 0 && hand <= 1 && head_pose_bridge::get_spatial_frame(body) &&
			body.generation == input.reference_generation &&
			hands::tracked_wrist(input, body, {}, hand, alignment_offsets(), out);
	}
	inline bool visual_wrist(int hand, hands::anchor& out) noexcept
	{
		return visual_wrist(controller_input::latest_interaction(), hand, out);
	}
}
