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
	std::cout<<"handling cases="<<checked<<" failures="<<failures<<'\n';return failures?1:0;
}
