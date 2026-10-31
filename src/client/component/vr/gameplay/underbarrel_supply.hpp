#pragma once
#include "underbarrel_feed.hpp"
#include <cmath>

namespace vr::gameplay::weapons::underbarrel
{
	// Choose only for a new waist draw. Existing escrow keeps its identity until
	// the player explicitly exchanges it through the compared reserve transfer.
	inline bool prefer_secondary_supply(const state& ammo,float travel,float stroke,bool primary_needed) noexcept
	{
		if(primary_needed || !valid(ammo) || ammo.held || !ammo.reserve)return false;
		switch(ammo.id.type)
		{
		case kind::m203:
			// Empty and opened (the open latch holds until the barrel is back within
			// 10% of closed, and insertion only needs the latch). Requiring 90% travel
			// gave the rifle magazine whenever the barrel slid back a little on
			// release (user, headset: "the empty launcher is not detected").
			return !ammo.loaded && ammo.open && !ammo.spent && std::isfinite(travel) &&
				std::isfinite(stroke) && stroke>0 && stroke<=.5f && travel>=0 && travel<=stroke;
		case kind::gp25:return !ammo.loaded;
		case kind::shotgun:return ammo.loaded-int(ammo.chamber)<shotgun_tube_capacity;
		default:return false;
		}
	}
	inline bool select_secondary_supply(bool smart,bool preferred,bool grip_down) noexcept
	{return grip_down!=(smart && preferred);}
}
