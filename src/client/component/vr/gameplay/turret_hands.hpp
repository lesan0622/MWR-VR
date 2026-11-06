#pragma once
#include "hands/service.hpp"
#include "../controller_input.hpp"
#include <array>
#include <span>
#include <string>

// MWR mounted turrets (village_defend minigun: weapon_minigun) carry no hands of
// their own, unlike Overlord's MW2 turrets with authored viewhands. The VR empty
// hands are drawn and held on the gun's rear spade grips, placed from the
// rendered gun bone (the Overlord way: hands on the handles while the arms aim it).
namespace vr::gameplay::turret_hands
{
	// Mounted on a turret whose grips were resolved this frame (vr_turretHands).
	bool active() noexcept;
	void present(const hands::interaction_rig&, const hands::rig&, const controller_input::frame&,
		const std::array<hands::vec, 2>& shoulders, const std::array<hands::vec, 3>& axes, hands::vec offset,
		std::span<hands::bone>, unsigned visible) noexcept;
	std::string status();
}
