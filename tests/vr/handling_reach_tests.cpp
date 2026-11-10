// Reuse the authoritative input/native-ammunition fixture, without running the
// unrelated legacy storage fixtures in this focused regression executable.
#define main existing_reload_suite_main
#include "physical_reload_tests.cpp"
#undef main
#include "component/vr/gameplay/weapons/g36c/reload_profile.hpp"

int main()
{
	unsigned failures{}, checked{};
	const auto check=[&](bool value,const char* text) {if(!value){++failures;std::cerr<<text<<'\n';}};
	button_magazine_tests::run<fixture>(check);
	magazine_feedback_tests::run<fixture>(check);
	manual_magazine_reload_tests::run<fixture>(check);
	unsigned angles{};
	for(const auto* d:w::reload_profiles)if(d->magazine_contacts && p::attached_magazine_radius(d->interaction)>0)
	for(int actor=0;actor<2;++actor)for(float units:{20.f,39.37007874f,80.f})for(unsigned axis=0;axis<3;++axis)for(unsigned angle=0;angle<12;++angle)
	{
		using namespace vr::gameplay::hands;using namespace vr::gameplay::hands::pose_math;
		const anchor gun{{12,-5,31},normalize(quat{.2f,.3f,.1f,.8f})};
		const float half=angle*3.14159265f/12;
		quat rotation{0,0,0,std::cos(half)};rotation[axis]=std::sin(half);rotation=normalize(rotation);
		const auto grasp=w::select_magazine_grip(*d,rotation,actor,{1,0,0,0},false,false,0,std::nullopt,true);
		const auto& box=*d->magazine_contacts;
		const auto center=scale(add(box.grab_low,box.grab_high),.5f);
		const anchor wrist{compose(gun,{center,{0,0,0,1}}).position,multiply(gun.rotation,rotation)};
		p::geometry g;w::sample_attached_magazine(*d,g,gun,wrist,wrist.rotation,{0,0,0,1},grasp,compose(wrist,grasp.in_wrist),units,false);
		check(g.magazine.valid && g.magazine.grip_distance<.00001f,"rotating wrist at magazine body rejected contact");
		auto far=wrist;far.position[0]+=units*3;
		w::sample_attached_magazine(*d,g,gun,far,far.rotation,{0,0,0,1},grasp,compose(far,grasp.in_wrist),units,false);
		check(g.magazine.grip_distance>p::attached_magazine_radius(d->interaction),"distant wrist acquired magazine");
		++angles;
	}
	for(const auto* d:w::reload_profiles)
	{
		const auto& interaction=d->interaction;
		if(interaction.motion==p::action_motion::charging_handle)
		{
			const float extra=interaction.preserve_handle_acquisition?0.f:.02f;
			check(std::abs(p::slide_acquisition_radius(interaction)-interaction.slide_radius-extra)<.00001f,"charging-handle reach policy mismatch");
		}
		if(d->ammunition.release!=w::mechanics::magazine_release::button || interaction.support_magazine_catch)continue;
		for(auto rear:{vr::hand::left,vr::hand::right})
		{
			fixture f(d);f.owner.rear=rear;f.step();
			const auto total=w::mechanics::total_rounds(f.state);
			f.geometry.magazine.grip_distance=interaction.button_magazine_radius+.01f;
			f.trigger(true);
			check(f.control.magazine_grabbed() && f.state.magazine_inserted,"expanded magazine reach did not acquire seated magazine");
			f.geometry.hand_in_gun={0,0,-.08f};f.geometry.seated_hand_distance=.08f;f.step();
			check(f.state.magazine_inserted && f.state.magazine_hand==vr::hand::none,"pre-grasp bypassed release button");
			f.button(true);
			check(f.state.magazine_hand==vr::hand(1-int(rear)) && !f.state.magazine_inserted,"release did not transfer pre-grasped magazine");
			check(w::mechanics::total_rounds(f.state)==total,"pre-grasp/release changed ammunition budget");
			++checked;
		}
	}
	const auto& g36=w::g36c::physical.interaction;
	check(g36.preserve_handle_acquisition && p::slide_acquisition_radius(g36)==g36.slide_radius,"G36 handle reach changed");
	for(auto rear:{vr::hand::left,vr::hand::right})
	{
		fixture handle(&w::m4::physical);handle.owner.rear=rear;handle.step();
		handle.geometry.slide_distance=handle.tuning->slide_radius+.01f;handle.trigger(true);
		check(handle.control.slide_held(),"expanded M4 handle reach did not acquire");
		fixture outside(&w::m4::physical);outside.owner.rear=rear;outside.step();
		outside.geometry.slide_distance=outside.tuning->slide_radius+.021f;outside.trigger(true);
		check(!outside.control.slide_held(),"handle acquired outside expanded reach");
		fixture unchanged(&w::g36c::physical);unchanged.owner.rear=rear;unchanged.step();
		unchanged.geometry.slide_distance=g36.slide_radius+.01f;unchanged.trigger(true);
		check(!unchanged.control.slide_held(),"G36 acquired outside original reach");
	}
	check(checked>0,"no button-magazine cases exercised");
	std::cout<<"handling cases="<<checked<<" rotation cases="<<angles<<" failures="<<failures<<'\n';return failures?1:0;
}
