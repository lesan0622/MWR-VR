#pragma once

// MWR story rigs that throw the body around (cargoship rope descent and
// helicopter ride, crashes, falls, the coup car): the native player is linked
// to a script entity (gentity tagInfo +0x208) with weapons disabled (PS+0x390
// bit 0x80). The camera then follows the authored eye with no room-scale
// offset, and the VR arms give way to the native rig.
namespace vr::gameplay::scripted_motion
{
	int linked_parent() noexcept; // Linked script entity, or -1.
	bool active() noexcept;
}
