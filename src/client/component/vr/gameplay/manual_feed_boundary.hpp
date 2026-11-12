#pragma once
#include "physical_reload_runtime.hpp"
#include "cylinder_runtime.hpp"
#include "tube_runtime.hpp"
#include "break_action_runtime.hpp"
#include "launcher_runtime.hpp"
#include "weapon_carry_runtime.hpp"
#include "independent_fire_runtime.hpp"
#include "underbarrel_runtime.hpp"
#include "official_cheats.hpp"
#include "view_aimed_weapon.hpp"

namespace vr::gameplay::weapons::manual_feed
{
	// One set of native detours, distinct feed authorities. Calls are sequential
	// copied snapshots; no family lock is held while entering another family.
	inline void set_boundary_ready(unsigned bit) noexcept
	{ physical_reload::set_boundary_ready(bit); cylinder::set_boundary_ready(bit); tube::set_boundary_ready(bit); break_action::set_boundary_ready(bit); launcher::set_boundary_ready(bit); }
	inline bool blocks_reload(const void* ps,int side) noexcept
	{ return underbarrel::blocks_native(ps) || physical_reload::blocks_reload(ps,side) || cylinder::blocks_reload(ps,side) || tube::blocks_reload(ps,side) || break_action::blocks_reload(ps,side) || launcher::blocks_reload(ps,side); }
	inline bool allow_fire(const void* ps,int command,int side) noexcept
	{
		if(cheats::transitioning() && native_ammunition::local_role(ps)>=0)return false;
		if(underbarrel::blocks_native(ps))return false;
		if (independent_fire::owns_native(ps)) return false; // Both simulation and prediction, including mouse attack.
		// View-aimed guns (AC-130, lock launchers, the C4 detonator) are never a VR
		// hold. A carry hold left from the player's own weapons refused them
		// ("carry hold") whenever its token differed (headset: only 40 mm fired).
		if (carry::active() && native_ammunition::local_role(ps)>=0 && !view_aimed::weapon(view_aimed::native_weapon(ps)))
		{
			const auto owner=carry::current_hold();
			if (side || !owner.can_fire() || native_ammunition::observe(ps).weapon!=owner.weapon) return false;
		}
		return physical_reload::allow_fire(ps,command,side) && cylinder::allow_fire(ps,command,side) && tube::allow_fire(ps,command,side) && break_action::allow_fire(ps,command,side) && launcher::allow_fire(ps,command,side);
	}
	// Diagnostics only: the first gate allow_fire would refuse with.
	inline const char* deny_reason(const void* ps,int command,int side) noexcept
	{
		if(cheats::transitioning() && native_ammunition::local_role(ps)>=0)return "cheat transition";
		if(underbarrel::blocks_native(ps))return "underbarrel";
		if(independent_fire::owns_native(ps))return "independent fire";
		if(carry::active() && native_ammunition::local_role(ps)>=0 && !view_aimed::weapon(view_aimed::native_weapon(ps)))
		{
			const auto owner=carry::current_hold();
			if(side || !owner.can_fire() || native_ammunition::observe(ps).weapon!=owner.weapon)return "carry hold";
		}
		if(!physical_reload::allow_fire(ps,command,side))return "physical reload";
		if(!cylinder::allow_fire(ps,command,side))return "cylinder";
		if(!tube::allow_fire(ps,command,side))return "tube";
		if(!break_action::allow_fire(ps,command,side))return "break action";
		if(!launcher::allow_fire(ps,command,side))return "launcher";
		return "allowed";
	}
	inline void consumed(const void* ps,int command,std::uint32_t weapon,bool alternate,int amount,int side,
		const native_ammunition::snapshot& before,const native_ammunition::snapshot& after,bool sustained=false) noexcept
	{
		physical_reload::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
		cylinder::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
		tube::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
		break_action::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
		launcher::consumed(ps,command,weapon,alternate,amount,side,before,after,sustained);
	}
}
