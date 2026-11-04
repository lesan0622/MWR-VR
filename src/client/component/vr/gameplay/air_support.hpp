#pragma once
#include "../controller_input.hpp"

// MWR village_assault Cobra air support (maps/air_support_shared): the native
// "cobra_air_support" action-slot weapon marks where getplayerangles points
// from geteye and calls the chopper on "weapon_fired". VR: the hand raised to
// the ear (radio) with Grip selects it; that hand then points the mark and
// either trigger fires it. The same gesture again puts it away.
namespace vr::gameplay::air_support
{
	// Hand that points while the native designator is the current weapon; -1 otherwise.
	int pointing_hand() noexcept;
	// Claymore: hand to the floor + Grip selects it; the plant needs one native attack.
	bool plant_attack_requested() noexcept;
	// FinishMove: the pointing hand's direction replaces the head in the native view.
	bool apply_view(const controller_input::frame&, float* angles, const float* delta_angles) noexcept;
}
