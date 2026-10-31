#pragma once
#include "hands/pose_solver.hpp"
#include "../controller_input.hpp"
#include <array>
#include <cstdint>
#include <span>
namespace game {struct playerState_s;struct usercmd_s;}
namespace vr::gameplay::hands {struct interaction_rig;}

// MWR sniperescape Barrett (turret2, weapon m14_bipod_stand, hidden by the
// script). Instead of the flat native scope (fixed_sniper), the native M82
// world model rests on its bipod in front of the turret eye: the right grip
// steers it about the bipod within the turret arcs, the left grip may hold the
// handguard, and a physical scope lens magnifies. The rested barrel is the
// native view the script fires along (getplayerangles/geteye).
namespace vr::gameplay::barrett_gun
{
	bool enabled() noexcept; // vr_barrettFreeGun; off falls back to fixed_sniper.
	// Nonzero while mounted with the free gun; the camera request epoch.
	std::uint64_t epoch() noexcept;
	// FinishMove: barrel-aimed native view, right trigger fire, right stick zoom.
	bool command(const controller_input::frame&, bool gameplay, game::usercmd_s*, float* angles,
		const game::playerState_s*) noexcept;
	// Empty-hand solve: wrists on the gun's authored grips (vehicles::constrain_hands contract).
	void constrain_hands(std::array<hands::anchor, 2>& targets, const std::array<hands::quat, 2>& basis,
		hands::vec offset, std::uint64_t reference) noexcept;
	void present(const hands::interaction_rig&, const hands::rig&, std::span<hands::bone> solved,
		unsigned visible) noexcept;
	// Camera, before the HMD pose: the native turret eye places the bipod rest once per mount.
	void observe_native_eye(const float* origin) noexcept;
}
