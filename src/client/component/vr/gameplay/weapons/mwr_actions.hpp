#pragma once
#include "../physical_reload_profile.hpp"
#include "m16/reload_poses.hpp"

namespace vr::gameplay::weapons::mwr_actions
{
	// Native empty reload captures, 2026-11-10. The HK handles were previously
	// limited to the MW2 MP5K's 65 mm stroke, short of the MWR receiver notch.
	inline constexpr float mp5_stroke=.100330f,g3_stroke=.120136f;
	inline constexpr hands::vec mp5_axis{-.9997895f,0,-.0205170f};
	inline constexpr std::array<bolt_travel_sample,2> mp5_travel{{{0,0},{mp5_stroke,mp5_stroke}}};
	inline constexpr std::array<bolt_travel_sample,2> m4_travel{{{0,0},{.059f,.059f}}};
	inline constexpr std::array<bolt_travel_sample,2> m16_travel{{{0,0},{m16::handle_stroke_m,m16::handle_stroke_m}}};
	// Disconnected bolt meshes reviewed from the MWR source mesh. Exact
	// topology and bounds are checked before replacing the immutable rigid group.
	inline constexpr std::array<std::array<unsigned,2>,9> mp5_surfaces{{{4596,5858},{245,398},{84,66},{2762,4078},{138,130},{876,1132},{669,772},{524,624},{4122,5226}}};
	inline constexpr std::array<scene_models::surface_face_range,1> mp5_faces{{{0,890,904}}};
	inline constexpr partitioned_bolt mp5_bolt{
		{"viewmodel_mp5_sp",19,mp5_surfaces,mp5_faces,{4.081041f,-.801363f,4.924840f},{6.067664f,-.401661f,5.959110f}},
		{"j_gun",{{},{0,0,0,1}},mp5_travel,mp5_stroke,0,true}};
	inline constexpr partitioned_bolt mp5_wet_bolt=[] {auto p=mp5_bolt;p.mesh.source="viewmodel_mp5_sp_wet";return p;}();
	inline constexpr std::array<std::array<unsigned,2>,11> m4_surfaces{{{9809,12200},{414,604},{4167,4750},{10029,13222},{2454,3910},{784,954},{102,122},{215,242},{540,896},{608,570},{512,720}}};
	inline constexpr std::array<scene_models::surface_face_range,1> m4_faces{{{3,11056,11190}}};
	inline constexpr partitioned_bolt m4_bolt{
		{"viewmodel_m4a1_sp",26,m4_surfaces,m4_faces,{2.785364f,-.496752f,3.403120f},{6.177806f,-.071412f,4.214979f}},
		{"j_gun",{{},{0,0,0,1}},m4_travel,.059f,0,true}};
	inline constexpr charging_handle_bolt m16_bolt{
		"j_bolt",{{4.642004f,0,3.836105f},{0,0,0,1}},m16_travel,.078f,0,true};

	inline void configure(reload_profile& p,std::string_view receiver)
	{
		const bool mp5=receiver=="viewmodel_mp5_sp" || receiver=="viewmodel_mp5_sp_wet";
		if(mp5 || receiver=="viewmodel_g3")
		{
			const float stroke=mp5?mp5_stroke:g3_stroke;
			const float ratio=stroke/p.interaction.slide_stroke;
			p.interaction.slide_stroke=stroke;
			p.interaction.full_stroke*=ratio;
			p.interaction.locked_travel*=ratio;
			if(mp5)p.interaction.slide_axis=mp5_axis;
		}
		if(mp5)p.bolt_partition=receiver=="viewmodel_mp5_sp"?&mp5_bolt:&mp5_wet_bolt;
		else if(receiver=="viewmodel_m4a1_sp")p.bolt_partition=&m4_bolt;
		else if(receiver=="viewmodel_m16" || receiver=="viewmodel_m16a4_sp_gl")p.bolt=&m16_bolt;
	}
}
