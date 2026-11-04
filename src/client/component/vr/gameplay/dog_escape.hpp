#pragma once
#include "../controller_input.hpp"

// MWR dogs (animscripts/dog/dog_combat): a dog that jumps the player waits for
// meleebuttonpressed() inside a short window after its lunge (the star prompt)
// to snap its neck. VR has no native melee button, so while the player is
// attacked by a dog a fresh Grip or Trigger press on either hand is held as
// the native melee press for a few commands.
namespace vr::gameplay::dog_escape
{
	bool attacked() noexcept;
	// Command thread: true while the escape press should hold BUTTON_MELEE.
	bool melee(const controller_input::frame&, controller_input::clock::time_point now) noexcept;
}
