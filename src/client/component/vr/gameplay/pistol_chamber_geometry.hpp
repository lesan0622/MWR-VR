#pragma once
#include "hands/pose_math.hpp"
#include <optional>

namespace vr::gameplay::weapons::physical_reload
{
	// MWR pistol bind meshes use +X down the barrel. Intersect a barrel triangle
	// with the vertical bore plane, retaining only the half above the bore.
	// This finds the real rear of the barrel hood instead of the case-exit tag.
	inline std::optional<float> barrel_back_edge(const std::array<hands::vec,3>& triangle,hands::vec bore) noexcept
	{
		std::array<hands::vec,6> points{};unsigned count{};
		for(unsigned i=0;i<3;++i)
		{
			const auto a=triangle[i],b=triangle[(i+1)%3];
			if(std::abs(a[1]-bore[1])<1e-6f)points[count++]=a;
			if((a[1]-bore[1])*(b[1]-bore[1])<0)
			{
				const float t=(bore[1]-a[1])/(b[1]-a[1]);
				points[count++]=hands::add(a,hands::scale(hands::sub(b,a),t));
			}
		}
		std::optional<float> rear;
		for(unsigned i=0;i<count;++i)
		{
			const auto a=points[i];
			if(a[2]>=bore[2] && (!rear || a[0]<*rear))rear=a[0];
			for(unsigned j=i+1;j<count;++j)
			{
				const auto b=points[j];if((a[2]-bore[2])*(b[2]-bore[2])>=0)continue;
				const float x=a[0]+(b[0]-a[0])*(bore[2]-a[2])/(b[2]-a[2]);
				if(!rear || x<*rear)rear=x;
			}
		}
		return rear;
	}
	inline float pistol_chamber_center(float barrel_rear,float length,float rim) noexcept
	{
		// At rest the case body starts at the barrel breech. At travel L-rim,
		// the nose reaches that breech and the entire rimless span is behind it.
		return barrel_rear+.5f*length-rim;
	}
}
