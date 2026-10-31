#pragma once
#include "hands/pose_solver.hpp"
#include "../controller_input.hpp"
#include "../hand.hpp"
#include <span>

namespace vr::gameplay::hands {struct interaction_rig;}
namespace vr::gameplay::c4_detonator
{
	// The hand forced to hold the maps/_c4 detonator; no gun support may claim it.
	bool holds(vr::hand) noexcept;
	// Hands service, after reload items: authored detonator wrist/fingers on the holder.
	void present(const hands::interaction_rig&,const hands::rig&,const controller_input::frame&,
		const std::array<hands::anchor,2>& targets,std::span<hands::bone>,unsigned occupied,unsigned visible)noexcept;
}
