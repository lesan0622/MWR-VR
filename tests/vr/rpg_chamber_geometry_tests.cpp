#include <iostream>
#include "component/vr/gameplay/pistol_chamber_geometry.hpp"
#include "component/vr/gameplay/launcher_feed.hpp"
#include "loader/mwr_port.hpp"
namespace hands=vr::gameplay::hands;

int main()
{
	using namespace vr::gameplay::weapons;
	using namespace vr::gameplay::weapons::physical_reload;
	using vr::hand;
	unsigned failed{};
	const auto check=[&](bool value,const char* why){if(!value){++failed;std::cerr<<why<<'\n';}};
	check(mwr_port::is_enabled("class vr::gameplay::weapons::launcher::component") &&
		mwr_port::is_enabled("class vr::gameplay::weapons::launcher::presenter_component"),"both RPG feed and presenter must pass the native component gate");
	const hands::vec bore{6.2562f,0,2.74967f};
	const std::array<hands::vec,3> hood{{{1.6673f,-.4f,2.6f},{1.6673f,.4f,2.6f},{1.6673f,0,3.15f}}};
	const auto rear=barrel_back_edge(hood,bore);
	check(rear && std::abs(*rear-1.6673f)<1e-5f,"barrel breech must come from the upper bore-plane intersection");
	auto side=hood;for(auto& v:side)v[1]+=1;
	check(!barrel_back_edge(side,bore),"off-axis frame surfaces must not move the chamber rearward");
	auto lower=hood;for(auto& v:lower)v[2]-=1;
	check(!barrel_back_edge(lower,bore),"frame and trigger geometry below the bore must be ignored");
	for(const float length:{.98f,1.28f,1.6f})
	{
		const float rim=.05f*length,travel=length-rim;
		const float center=pistol_chamber_center(*rear,length,rim);
		check(std::abs(center-.5f*length+rim-*rear)<1e-5f,"closed round body must start inside the barrel breech");
		check(std::abs(center-travel+.5f*length-*rear)<1e-5f,"at a rimless stroke the complete nose must reach the barrel breech");
		check(std::abs(center-travel-.5f*length+rim-(*rear-travel))<1e-5f,"the rimless rear must reach the moving port's rear edge");
	}
	launcher_profile p;p.loading=launcher_loading::rocket;
	for(auto hand:{hand::left,hand::right})
	{
		launcher::rocket_hold held;ammunition::projection native{0,3};
		const auto commit=[&](ammunition::projection next){native=next;return true;};
		check(launcher::draw(p,held,hand,native,commit) && native.loaded==0 && native.reserve==2,"drawing must escrow exactly one native round");
		check(!launcher::seated(held,{0,0,0},1),"a newly drawn overlapping rocket cannot load");
		check(!launcher::seated(held,{.2f,0,0},-1),"a reversed rocket cannot arm insertion");
		check(!launcher::seated(held,{.2f,0,0},1) && launcher::seated(held,{0,0,0},1),"front approach followed by seating must load");
		check(!launcher::settle(held,native,true,[](auto){return false;}) && held.actor==hand && native.reserve==2,"a failed native write must retain escrow");
		check(launcher::settle(held,native,true,commit) && native.loaded==1 && native.reserve==2 && held.actor==hand::none,"seating must transfer escrow into the tube");
		native.loaded=0; // Native firing consumes the loaded round.
		check(launcher::draw(p,held,hand,native,commit) && launcher::settle(held,native,false,commit) && native.loaded==0 && native.reserve==2,"cancelled loading must refund exactly once");
		check(launcher::settle(held,native,false,commit) && native.reserve==2,"repeated cleanup must not duplicate rockets");
	}
	std::cout<<"RPG feed and pistol chamber geometry failures="<<failed<<'\n';return failed?1:0;
}
