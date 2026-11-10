#pragma once
#include <array>
#include <cmath>

namespace vr::placement_compensation
{
	using origin = std::array<float,3>;
	// Solved bones are relative to solve_origin. Native drawing adds placement.
	// Add their SAME-scene difference to render bones only; gameplay is unchanged.
	inline origin delta(const origin& solve_origin,const origin& placement,bool allow_distant=false) noexcept
	{
		origin result{};float energy{};
		for(unsigned i=0;i<3;++i)
		{
			result[i]=solve_origin[i]-placement[i];
			if(!std::isfinite(solve_origin[i]) || !std::isfinite(placement[i]) || !std::isfinite(result[i]))return {};
			energy+=result[i]*result[i];
		}
		const float limit=allow_distant?2048.f:64.f;
		return std::isfinite(energy) && energy<=limit*limit ? result : origin{};
	}
}
