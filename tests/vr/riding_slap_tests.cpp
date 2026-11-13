// Riding vehicle: the slap swing-speed gate uses tracking space, so the
// vehicle's own world speed (jeepride truck 11-22 m/s) cannot reject a slap.
#include "component/vr/gameplay/handle_catch.hpp"
#include <cassert>

namespace
{
	struct quality { bool position_valid{true}, position_tracked{true}; };
	struct tracking { std::array<double,3> position_meters{}; };
	struct pose { bool valid{true}; tracking tracking; quality quality; };
}

int main()
{
	namespace p=vr::gameplay::weapons::physical_reload;
	using clock=std::chrono::steady_clock;
	const p::impact_profile impact{.025f,.07f,.5f,.025f,{0,-1,0},.5f,true};
	const auto run=[&](bool tracked)
	{
		p::hand_slap_sweep<1> sweep;
		const auto t0=clock::now();
		bool any=false;
		for(int i=0;i<4;++i)
		{
			const float dt=.011f*i;
			const p::vec world{20.f*dt,-2.f*dt,0}; // truck 20 m/s + hand 2 m/s
			pose hand; hand.tracking.position_meters={0,-2.f*dt,0};
			const auto position=tracked?p::swing_position(hand,world):world;
			p::slap_observation seen;
			sweep.update(impact,8.f,{p::vec{0,.06f-.022f*i,0}},position,t0+std::chrono::microseconds(int(dt*1e6f)),.05f,&seen);
			if(i>0)any|=seen.world_speed_ok;
		}
		return any;
	};
	assert(!run(false)); // old world-space gate: truck speed exceeds max_speed
	assert(run(true));
	pose lost; lost.quality.position_tracked=false;
	const p::vec fallback{1,2,3};
	assert(p::swing_position(lost,fallback)==fallback);
	return 0;
}
