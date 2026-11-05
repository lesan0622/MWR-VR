#pragma once
#include "body_equipment.hpp"
#include <cstring>
#include <span>

namespace vr::gameplay::equipment
{
	inline constexpr std::array item_slots{slot::tactical,slot::lethal};
	// H2 selected secondary/primary offhand, not the currently primed offhand at
	// 0x3b0. Verified in native selection code and live frag/flash inventory.
	inline std::array<std::uint32_t,2> selected_chest_items(std::span<const std::byte> ps) noexcept
	{
		std::array<std::uint32_t,2> out{};
		if(ps.size()<0x3bc)return out;
		for(unsigned i=0;i<out.size();++i)
		{
			std::memcpy(&out[i],ps.data()+(i==0 ? 0x388:0x384),4); // MWR offhand secondary/primary (H2 0x3b8/0x3b4)
			if(out[i]>511)out[i]=0;
		}
		return out;
	}
	inline bool show_chest_item(int loaded,int reserve) noexcept
	{return loaded>=0 && loaded<=1001 && reserve>=0 && reserve<=1000000 && (loaded>0 || reserve>0);}
	inline anchor stowed_chest_item(const chest_slots& chest,slot where,vec model_center) noexcept
	{
		const auto& point=chest.anchors[unsigned(where)];
		// World equipment is upright along model +Z. Knife slot basis has blade
		// +X downward: rotate the item back into body forward/left/up axes.
		const auto rotation=normalize(multiply(point.rotation,quat{0,-.70710678f,0,.70710678f}));
		return {sub(point.position,rotate(rotation,model_center)),rotation};
	}
	// MWR claymore (action-slot item, sniperescape): on the torso, centered below
	// the grenade row. Its grab box (14 cm high) clears the tactical box above it.
	inline constexpr float claymore_drop=.32f;
	inline anchor claymore_chest_anchor(const head_pose_bridge::spatial_frame& frame,const chest_slots& chest) noexcept
	{
		auto point=chest.anchors[unsigned(slot::tactical)];
		const auto body=head_pose_bridge::body_slots_frame(frame);
		point.position=sub(point.position,scale(body.head_yaw_axis[2],claymore_drop*body.units_per_meter));
		return point;
	}
	inline anchor stowed_claymore(const head_pose_bridge::spatial_frame& frame,const chest_slots& chest,vec model_center) noexcept
	{
		const auto point=claymore_chest_anchor(frame,chest);
		const auto rotation=normalize(multiply(point.rotation,quat{0,-.70710678f,0,.70710678f}));
		return {sub(point.position,rotate(rotation,model_center)),rotation};
	}
	inline float claymore_grab_distance(const head_pose_bridge::spatial_frame& frame,const chest_slots& chest,vec palm) noexcept
	{
		if(!chest.valid)return INFINITY;
		const auto body=head_pose_bridge::body_slots_frame(frame);const auto u=body.units_per_meter;
		const body_reach_volume box{claymore_chest_anchor(frame,chest).position,scale(vec{-.12f,-.12f,-.07f},u),scale(vec{.12f,.12f,.07f},u),body.head_yaw_axis,0};
		return box.box_distance(palm);
	}
}
