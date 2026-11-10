#include "component/vr/placement_compensation.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#ifdef MWR_NATIVE_PLACEMENT_TEST
#include <Windows.h>
#include "component/vr/engine_stereo_view.hpp"
#include <cstring>
#endif

int main()
{
	using namespace vr::placement_compensation;
	origin previous{};unsigned stale_errors{};
	for(unsigned frame=0;frame<1000;++frame)
	{
		// Moving origin, variable camera/placement separation and stop/start.
		const float t=frame*.03f;
		origin placement{200+50*std::sin(t),-100+30*std::cos(t*.7f),80+3*std::sin(t*2)};
		origin solve{placement[0]+2*std::sin(t*1.3f),placement[1]+std::cos(t),placement[2]+.5f*std::sin(t*3)};
		const auto correction=delta(solve,placement);
		for(origin target:{origin{210,22,85},origin{170,-30,40},origin{202,-10,15}})
		for(unsigned i=0;i<3;++i)
		{
			const float local=target[i]-solve[i];
			assert(std::abs(local+correction[i]+placement[i]-target[i])<.0001f);
			if(std::abs(local+previous[i]+placement[i]-target[i])>.001f)++stale_errors;
		}
		previous=correction;
	}
	assert(stale_errors>100);
	assert((delta({1,2,3},{1,2,3})==origin{}));
	assert((delta({64,0,0},{})==origin{64,0,0}));
	assert((delta({65,0,0},{})==origin{}));
	assert((delta({280,0,0},{},true)==origin{280,0,0}));
	assert((delta({2049,0,0},{},true)==origin{}));
	assert((delta({std::numeric_limits<float>::quiet_NaN(),0,0},{})==origin{}));
	assert((delta({}, {0,std::numeric_limits<float>::infinity(),0})==origin{}));
#ifdef MWR_NATIVE_PLACEMENT_TEST
	// Standalone process only: map a synthetic native placement block at the
	// verified ABI address and exercise the production reader, without a game.
	auto* memory=VirtualAlloc(reinterpret_cast<void*>(0x151C20000),0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
	assert(memory==reinterpret_cast<void*>(0x151C20000));
	for(unsigned n=0;n<100;++n)
	{
		const origin source{100.f+n,200.f-n,300.f+n};
		std::memcpy(reinterpret_cast<void*>(0x151C277F0),source.data(),sizeof(source));
		const origin solve{source[0]+1,source[1]+2,source[2]+3};
		assert((vr::engine_stereo_view::placement_lag(solve)==origin{1,2,3}));
	}
	std::memset(reinterpret_cast<void*>(0x151C277F0),0,sizeof(origin));
	vr::engine_stereo_view::allow_far_placement(false);
	assert((vr::engine_stereo_view::placement_lag({280,0,0})==origin{}));
	vr::engine_stereo_view::allow_far_placement(true);
	assert((vr::engine_stereo_view::placement_lag({280,0,0})==origin{280,0,0}));
	assert(VirtualFree(memory,0,MEM_RELEASE));
	std::cout<<"Production native placement reader: 100 updates and mounted guard passed\n";
#endif
	std::cout<<"1000 moving frames: current placement cancels exactly; stale correction errors="<<stale_errors<<"; bounds/invalid-input tests passed\n";
}
