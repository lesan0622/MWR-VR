#pragma once
#include "../belt_profile.hpp"
#include "../weapon_profile.hpp"
#include "../physical_reload_profile.hpp"

#include <span>
#include <string_view>

namespace vr::gameplay::weapons::mwr_rebase
{
	// MWR port: a gun that exists in both MW2 and MWR keeps the original
	// Overlord recipe for everything measured relative to the hand or the
	// magazine (grasps, fingers, magazine-in-wrist, contacts, rules), and takes
	// its bone-anchored rest poses from the MWR viewmodel bind pose.
	//
	// Both games ship replicas of the same real gun, so receiver geometry is
	// close (M16 tag_clip differs by ~1 mm). Values tied to the magazine follow
	// the tag_clip delta. Values tied to the operated part follow the slide
	// delta only when the MWR bone is the same physical part.
	struct target
	{
		std::string_view id;       // Unique recipe id, e.g. "mwr_m16".
		std::string_view receiver; // MWR viewmodel receiver model.
		std::string_view magazine_bone, slide_bone, bullets_bone;
		const char* magazine_model{}; // MWR world clip model.
		hands::anchor magazine_bind, slide_bind; // MWR gun-local native idle poses (inches).
		bool slide_same_part{};
		// The MWR receiver is the asset the MW2 recipe was authored on (same bone
		// names, parents and bind): keep its bolt/catch/bullet contracts.
		bool keep_mechanism{};
		// No MWR world clip model: cut the magazine from the receiver mesh.
		bool magazine_from_receiver{};
		// Keep-mode exceptions where the MWR receiver lacks an MW2 bone.
		bool drop_bullet_parents{}; // e.g. Desert Eagle j_bullet01 under tag_bullets.
		bool drop_belt_bridge{};    // e.g. RPD optic rail bridge (MW2 j_rail, MWR tag_rail).
		bool keep_handle_catch{};   // HK lock-open notch (MP5K recipe on MWR MP5/G3).
		bool receiver_parented_bullets{}; // Round stand-in parented to j_gun (MWR Skorpion has no round bone).
		bool controller_magazine{}; // Held magazine +Z follows the controller (measured grasp alone misaligns it).
		int magazine_capacity{};     // Native capacity when it differs from the source recipe.
		std::span<const std::string_view> additional_bullet_bones{};
		// Support-hand magazine grasp measured from the MWR reload animation
		// (replaces the MW2 grasp set, which was fitted to MW2's magazine mesh).
		const hands::anchor* magazine_in_wrist{};
		std::span<const joint_pose> magazine_fingers{};
		bool (*native_family)(std::string_view) noexcept {};
		const char* (*sound_key)(mechanics::effect) noexcept {}; // Optional override.
		const belt_feed::profile* belt{}; // MWR feed cover/belt poses replacing the source recipe's.
		const char* cycle_notetrack{}; // MWR notetrack split for rear/close when the source's is absent.
		// Explicit MWR action sounds (notetrack + part), replacing the source's rear/close cues.
		sound_reference rear{}, close{};
	};

	// Builds once at static initialization; storage is never released because
	// profile registrations keep pointers into it.
	reload_profile rebase(const reload_profile& source, const target& t);

	// Moves an original hand profile (wrists, fingers, aiming) onto an MWR
	// receiver. MW2 attachment contracts and equip-animation names are dropped.
	// keep_parts retains equip-rest part poses (only valid on identical receivers).
	profile adopt(const profile& source, std::string_view id, std::string_view receiver,
		const reload_profile* reload, bool keep_parts, std::span<const std::string_view> drop_parts = {});

	// MWR builds viewmodels from a receiver plus attachment models (optic,
	// reticle, laser, foregrip, silencer). Accept attachments that hang off the
	// receiver or an earlier attachment; they never move the authored grips.
	profile_match bind_attachments(const profile& selected, std::span<const hands::model_definition> models,
		const hands::model_definition& receiver, const hands::rig& rig,
		std::span<const hands::bone_definition> bones) noexcept;
}
