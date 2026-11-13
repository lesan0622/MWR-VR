#include <iostream>
#include "component/vr/controller_input.hpp"
#include "component/vr/gameplay/weapon_reload_profiles.hpp"
#include "component/vr/handedness.hpp"
#include "component/vr/gameplay/weapon_carry.hpp"
#include "component/vr/gameplay/mwr_reload_sound.hpp"
#include "component/vr/gameplay/weapons/mwr_actions.hpp"
#include "component/vr/menu_surface.hpp"
namespace w=vr::gameplay::weapons;

int main()
{
	unsigned failures{},chambers{},linkages{},notches{};
	const auto check=[&](bool ok,const char* why){if(!ok){++failures;std::cerr<<why<<'\n';}};
	using vr::hand;
	for(bool left:{false,true})for(bool hip:{false,true})
	{
		vr::handedness::initialize(left,hip);
		vr::controller_input::frame input;
		input.move={.2f,.8f};input.turn={-.6f,.1f};input.move_active=true;
		input.sprint.presses=7;input.jump.presses=9;
		input.menu_recenter.presses=13;input.primary[1].presses=17;
		input.secondary[0].presses=19;input.secondary[1].presses=23;
		input.trigger[0].presses=29;input.squeeze[1].presses=31;input.grip[0].valid=true;
		vr::handedness::controls(input);
		w::holding_state held;
		check(held.equipped(7).rear==(left?hand::left:hand::right),"default equipment ignored control hand");
		vr::menu_surface::pointer_owner pointer;
		pointer.update(input,false,vr::controller_input::clock::now());
		check(pointer.hand()==unsigned(left?0:1),"default menu pointer ignored control hand");
		check(input.move[0]==(left?-.6f:.2f) && input.turn[0]==(left?.2f:-.6f),"stick roles did not follow control hand");
		check(input.sprint.presses==(left?9:7) && input.jump.presses==(left?7:9),"stick clicks did not follow control hand");
		check(input.primary[left?0:1].presses==(left?13:17) && input.menu_recenter.presses==(left?17:13),"primary/menu roles did not follow control hand");
		check(input.secondary[0].presses==19 && input.secondary[1].presses==23 && input.trigger[0].presses==29 && input.squeeze[1].presses==31 && input.grip[0].valid,"physical hand inputs were swapped");
		w::carry::inventory inventory;
		std::array<w::carry::owned_instance,1> guns{{{{7,1},{.waist=true}}}};
		check(inventory.reconcile_instances(guns),"pistol inventory reconcile failed");
		check(inventory.find({7,1})->at==(hip?w::carry::location::left_waist:w::carry::location::right_waist),"holster side was coupled to control hand");
	}
	vr::handedness::initialize(false,false);
	for(const auto* d:w::reload_profiles)if(d->ammunition.feed==w::mechanics::feed_type::closed_bolt)
	for(auto rear:{hand::left,hand::right})
	{
		namespace m=w::mechanics;
		const auto& r=d->ammunition;
		m::state s{.weapon=7,.instance_generation=1,.revision=1,.reserve_rounds=2*r.magazine_capacity};
		check(m::from_native_automatic(r,r.magazine_capacity,s),"native ammo import failed");
		check(s.chamber_loaded && s.magazine_rounds==r.magazine_capacity-1,"native import duplicated the chamber cartridge");
		const auto initial=m::total_rounds(s);
		const auto act=[&](m::operation op,hand actor){
			auto tx=m::plan(r,s,{op,s.weapon,s.instance_generation,s.revision,rear,actor});
			check(bool(tx),"chamber transaction rejected");if(tx)s=tx.next;return tx;
		};
		const auto off=hand(1-int(rear));
		act(r.release==m::magazine_release::button?m::operation::release_button:m::operation::pull_magazine,
			r.release==m::magazine_release::button?rear:off);
		check(!s.magazine_inserted && s.chamber_loaded && m::native_ammo(s).loaded==1 && m::total_rounds(s)==initial,"removing magazine did not retain exactly one actual cartridge");
		const auto shot=act(m::operation::accepted_shot,rear);
		check(!s.chamber_loaded && m::native_ammo(s).loaded==0 && shot.rounds_spent==1 && m::total_rounds(s)==initial-1,"chamber-only shot did not consume exactly one cartridge");
		check(!m::plan(r,s,{m::operation::accepted_shot,s.weapon,s.instance_generation,s.revision,rear,rear}),"empty chamber fired twice");
		if(s.magazine_hand!=hand::none)act(m::operation::cancel_magazine,off);
		act(m::operation::cycle_action,off);
		check(!s.chamber_loaded && m::total_rounds(s)==initial-1,"cycling without magazine created ammunition");
		act(m::operation::draw_magazine,off);
		act(m::operation::insert_magazine,off);
		check(!s.chamber_loaded && s.magazine_rounds==r.magazine_capacity,"insertion fed a round without cycling");
		act(m::operation::cycle_action,off);
		check(s.chamber_loaded && s.magazine_rounds==r.magazine_capacity-1 && m::total_rounds(s)==initial-1,"cycling did not transfer one real round from magazine to chamber");
		act(r.release==m::magazine_release::button?m::operation::release_button:m::operation::pull_magazine,
			r.release==m::magazine_release::button?rear:off);
		if(s.magazine_hand!=hand::none)act(m::operation::cancel_magazine,off);
		act(m::operation::draw_magazine,off);act(m::operation::insert_magazine,off);
		check(s.chamber_loaded && s.magazine_rounds==r.magazine_capacity-int(!r.plus_one) &&
			m::total_rounds(s)==initial-1,"tactical reload did not preserve chamber separately from full magazine");
		++chambers;
	}
	for(const auto* key:{"weap_mp5_clipout_plr","weap_mp5_clipin_plr","weap_mp5_chamber_plr"})
	{
		w::sound_reference sound{key,w::sound_reference_kind::notetrack,w::sound_part::second};
		const auto sd=w::mwr_reload_sound("mp5_silencer",sound);
		check(std::string_view(sd.name).starts_with("weap_mp5sd_") && sd.part==sound.part,"MP5SD did not use its native reload map");
		check(w::mwr_reload_sound("mp5",sound).name==sound.name,"ordinary MP5 audio changed");
		check(w::mwr_reload_sound("mp5_silencer_fake-invalid",sound).name==sound.name,"invalid weapon admitted sound remapping");
	}
	for(const auto* d:w::reload_profiles)if(d->id=="mwr_mp5" || d->id=="mwr_mp5_wet" || d->id=="mwr_m4" || d->id=="mwr_m16")
	{
		check(w::valid_partition_profile(*d),"MWR bolt partition rejected");
		const auto stroke=d->interaction.slide_stroke;
		check(std::abs(w::displayed_internal_bolt(*d,stroke*.5f,0,false,-1)-stroke*.5f)<.0001f,"manual handle and bolt did not travel together");
		check(w::displayed_internal_bolt(*d,0,stroke,false,.02f)==0,"manual linkage synthesized a firing cycle");
		++linkages;
	}
	for(const auto* d:w::reload_profiles)if(d->id=="mwr_mp5" || d->id=="mwr_g3")
	{
		const auto end=w::handle_pose(d->slide_rest,d->handle_catch,d->interaction.slide_axis,d->interaction.slide_stroke*39.37007874f,0);
		check(std::abs(end.position[0]-(d->id=="mwr_mp5"?8.461279f:10.639433f))<.005f,"MWR handle stopped before native notch");
		++notches;
	}
	check(linkages==4 && notches==2,"MWR regression profiles were not registered");
	std::cout<<"handedness, holsters, MP5SD audio and "<<chambers<<" chamber ledgers: "<<failures<<" failures\n";
	return failures?1:0;
}
