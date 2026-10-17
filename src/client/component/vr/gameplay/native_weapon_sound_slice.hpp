#pragma once
#include "weapon_sound_reference.hpp"
#include <string>

namespace vr::gameplay::weapons::native_weapon_sound_slice
{
	bool initialize();
	bool play(short entity, const float* origin, const char* alias, sound_part part,const sound_window* window=nullptr);
	// MWR port tool: decode one alias into captures/_sounds/<file>.wav; returns a status line.
	std::string dump_alias(short entity, const float* origin, const char* alias);
}
