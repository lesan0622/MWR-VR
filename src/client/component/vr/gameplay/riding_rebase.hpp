#pragma once
#include "hands/pose_solver.hpp"
#include "../head_pose_bridge.hpp"
#include "game/game.hpp"
#include <cmath>
#include <cstring>

// MWR addition (not in Overlord), riding only (user request): hand and muzzle
// world poses come from the rendered frame, while use lists, pickups and
// bullets run on the server. Linked to a moving parent (jeepride truck) the
// rendered view trails the server by speed x latency (~100 units at
// jeepride speed), so server queries need the render -> server shift.
namespace vr::gameplay::riding
{
	// Player entity linked to a parent: gentity tag info +0x208 -> parent.
	inline bool linked() noexcept
	{
		const void* tag{};
		std::memcpy(&tag, reinterpret_cast<const std::byte*>(&game::g_entities[0]) + 0x208, sizeof(tag));
		return tag && *static_cast<const void* const*>(tag);
	}
	// Server eye minus the frame's native view origin; zero unless riding.
	// Larger shifts (scripted views far from the eye) are not applied.
	inline hands::vec shift(const head_pose_bridge::spatial_frame& frame) noexcept
	{
		const auto* ps = reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
		if (!ps || !frame.has_native_origin || !linked()) return {};
		const hands::vec eye{ps->origin[0], ps->origin[1], ps->origin[2] + ps->viewHeightCurrent};
		const hands::vec native{frame.native_origin[0], frame.native_origin[1], frame.native_origin[2]};
		const auto delta = hands::sub(eye, native);
		const float distance = hands::length(delta);
		return std::isfinite(distance) && distance <= 300.f ? delta : hands::vec{};
	}
}
