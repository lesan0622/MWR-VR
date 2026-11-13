#pragma once
#include "native_weapon_family.hpp"
#include "weapon_sound_reference.hpp"

namespace vr::gameplay::weapons
{
	inline sound_reference mwr_reload_sound(std::string_view weapon,sound_reference sound) noexcept
	{
		// Silenced MP5s use the MP5SD map even on the dry receiver. Preserve
		// notetrack lookup, split timing and the actual playing weapon identity.
		if(sound.kind!=sound_reference_kind::notetrack || !sound.name || sound.notetrack_weapon ||
			!native_weapon_family(weapon,"mp5_silencer"))return sound;
		const std::string_view key=sound.name;
		if(key=="weap_mp5_clipout_plr")sound.name="weap_mp5sd_clipout_plr";
		else if(key=="weap_mp5_clipin_plr")sound.name="weap_mp5sd_clipin_plr";
		else if(key=="weap_mp5_chamber_plr")sound.name="weap_mp5sd_chamber_plr";
		return sound;
	}
}
