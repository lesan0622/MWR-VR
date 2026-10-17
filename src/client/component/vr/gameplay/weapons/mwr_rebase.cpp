#include <std_include.hpp>
#include "mwr_rebase.hpp"
#include "../part_grip_pose.hpp"
#include "../charging_handle_bolt.hpp"
#include "../hands/pose_solver.hpp"
#include "../belt_profile.hpp"

namespace vr::gameplay::weapons::mwr_rebase
{
	namespace
	{
		using hands::vec;

		// Registrations keep raw pointers into rebased storage for the process
		// lifetime, so allocations are intentionally never freed.
		template <class T, class... Args> T* keep(Args&&... args)
		{
			return new T(std::forward<Args>(args)...);
		}

		template <class T> std::span<const T> keep_span(std::span<const T> source, auto&& transform)
		{
			if (source.empty())
				return {};
			auto* values = keep<std::vector<T>>(source.begin(), source.end());
			for (auto& value : *values)
				transform(value);
			return *values;
		}

		vec shifted(vec value, vec shift) noexcept
		{
			return hands::add(value, shift);
		}

		void shift_grip(part_grip_pose& grip, vec shift)
		{
			grip.wrist.position = shifted(grip.wrist.position, shift);
			if (grip.opposite_pose)
				grip.opposite_pose->wrist.position = shifted(grip.opposite_pose->wrist.position, shift);
			if (grip.symmetry_center)
				grip.symmetry_center = keep<vec>(shifted(*grip.symmetry_center, shift));
		}
	}

	reload_profile rebase(const reload_profile& source, const target& t)
	{
		auto p = source;
		// Identical receivers keep the original rest poses: they were measured on
		// the same asset from its idle animation, while the bind pose can sit
		// inches away (Dragunov bolt: bind x=7.40, idle x=9.92).
		const auto magazine_shift = t.keep_mechanism ? vec{} : hands::sub(t.magazine_bind.position, source.magazine_rest.position);
		const auto slide_shift = t.keep_mechanism || !t.slide_same_part ? vec{} :
		    hands::sub(t.slide_bind.position, source.slide_rest.position);

		p.id = t.id;
		if (t.magazine_capacity > 0)
			p.ammunition.magazine_capacity = t.magazine_capacity;
		if (t.native_family)
			p.native_family = t.native_family;
		p.magazine_bone = t.magazine_bone;
		p.slide_bone = t.slide_bone;
		p.bullets_bone = t.bullets_bone;
		p.additional_bullet_bones = t.additional_bullet_bones;
		p.magazine_model = t.magazine_model;

		// Bone-anchored rest poses come from the MWR bind pose.
		if (!t.keep_mechanism)
		{
			p.magazine_rest = t.magazine_bind;
			p.slide_rest = t.slide_bind;
		}

		// Gun-local magazine features follow tag_clip.
		p.well.position = shifted(source.well.position, magazine_shift);
		if (source.magazine_contacts)
		{
			auto* contacts = keep<magazine_contact_profile>(*source.magazine_contacts);
			contacts->grab_low = shifted(contacts->grab_low, magazine_shift);
			contacts->grab_high = shifted(contacts->grab_high, magazine_shift);
			contacts->latch = shifted(contacts->latch, magazine_shift);
			if (contacts->second_latch)
			{
				auto* latch = keep<magazine_latch_profile>(*contacts->second_latch);
				latch->position = shifted(latch->position, magazine_shift);
				contacts->second_latch = latch;
			}
			p.magazine_contacts = contacts;
		}

		// Gun-local operated-part features follow the slide only when the MWR
		// bone is the same physical part; otherwise the receiver geometry holds.
		p.slide_grab_low = shifted(source.slide_grab_low, slide_shift);
		p.slide_grab_high = shifted(source.slide_grab_high, slide_shift);
		p.slide_grips = keep_span(source.slide_grips, [&](part_grip_pose& g) { shift_grip(g, slide_shift); });
		p.knife_slide_grips =
		    keep_span(source.knife_slide_grips, [&](part_grip_pose& g) { shift_grip(g, slide_shift); });
		if (source.slide_capture)
		{
			auto* capture = keep<part_capture_halfspace>(*source.slide_capture);
			capture->maximum += hands::dot(capture->normal, slide_shift);
			p.slide_capture = capture;
		}

		// MW2 counted-fill topology and bolt partitions are keyed to MW2 mesh
		// hashes/receiver names; the MWR receiver cannot satisfy them.
		p.magazine_fills = {};
		p.bolt_partition = nullptr;
		const auto receiver = t.receiver.data(); // Targets name literals: NUL-terminated.
		// The triangle-mask contract describes the MW2 mesh; only identical receivers keep it.
		p.skinned_receiver = t.keep_mechanism && source.skinned_receiver ? receiver : nullptr;
		// Prefer the MWR world clip model: MWR receivers weight little of the
		// magazine mesh to tag_clip, so a receiver cut can render nearly empty.
		p.rigid_magazine_source = t.magazine_from_receiver ? receiver : nullptr;
		// World clip magazines take their counted rounds from this receiver.
		p.round_source = t.magazine_from_receiver ? nullptr : receiver;
		// A chamber cartridge is cut from the receiver magazine source; without one
		// (MWR world clip magazines) the MW2 chamber presentation cannot bind.
		if (!p.rigid_magazine_source)
			p.chamber_round = nullptr;
		if (t.receiver_parented_bullets)
			p.receiver_parented_bullets = true;
		if (t.magazine_from_receiver)
			p.magazine_model = receiver;
		// MW2 counted-fill recipes do not match MWR meshes (topology hash differs
		// even for the shared M9 receiver name); rounds come from bone subsets.
		if (!t.keep_mechanism)
		{
			p.magazine_body_bones = {};
			p.magazine_round_variants = {};
			p.rounds_per_variant = 0;
			p.animation_only_magazines = {};
			p.bullet_parents = {};
			p.action_detail_bone = {};
			p.handle_fold = nullptr;
			p.handle_child_of_bolt = false;
			if (p.bolt)
			{
				// The separately skinned internal bolt is an MW2 asset detail.
				if (p.ammunition.feed == mechanics::feed_type::open_bolt)
					p.concealed_bolt = true;
				p.bolt = nullptr;
			}
			if (p.handle_catch && !t.keep_handle_catch)
			{
				p.handle_catch = nullptr;
				p.ammunition.manual_catch = false;
				p.interaction.manual_catch = nullptr;
			}
		}
		else if (t.additional_bullet_bones.empty())
			p.additional_bullet_bones = source.additional_bullet_bones;
		if (t.drop_bullet_parents)
		{
			p.bullet_parents = {};
			p.additional_bullet_bones = t.additional_bullet_bones; // The parented rounds are the extras.
		}
		if (t.belt && p.interaction.belt)
		{
			// Keep the source's reach/push tuning only where the MWR profile has none.
			auto* belt = keep<belt_feed::profile>(*t.belt);
			belt->cover_radius = p.interaction.belt->cover_radius;
			belt->chain_radius = p.interaction.belt->chain_radius;
			belt->lay_radius = p.interaction.belt->lay_radius;
			p.interaction.belt = belt;
		}
		if (t.drop_belt_bridge && p.interaction.belt)
		{
			auto* belt = keep<belt_feed::profile>(*p.interaction.belt);
			belt->bridge = nullptr;
			belt->details = {}; // Details may reference the bridge.
			p.interaction.belt = belt;
			p.ammunition.belt_bridge = false;
		}

		if (t.magazine_in_wrist)
		{
			p.magazine_in_wrist = *t.magazine_in_wrist;
			if (!t.magazine_fingers.empty())
				p.magazine_fingers = t.magazine_fingers;
			p.magazine_grasps = {};
			p.magazine_default_pose = 0;
			p.interaction.magazine_pose_count = 1; // The single measured grasp.
		}

		// Sound keys are notetrack lookups resolved in the live WeaponDef map
		// (missing keys stay silent); MWR keeps the COD4 names MW2 inherited.
		if (t.sound_key)
		{
			p.sound_key = t.sound_key;
			// Split removal audio was resolved from the source's keys at build time.
			if (p.removal_sound.name)
				p.removal_sound.name = t.sound_key(mechanics::effect::magazine_out);
		}
		if (t.cycle_notetrack)
		{
			// MW2 notetrack names absent from the MWR WeaponDef sound map play nothing.
			for (auto* sound : {&p.rear_sound, &p.close_sound})
				if (sound->name && sound->kind == sound_reference_kind::notetrack)
					sound->name = t.cycle_notetrack;
		}
		return p;
	}

	profile adopt(const profile& source, std::string_view id, std::string_view receiver,
		const reload_profile* reload, bool keep_parts)
	{
		auto p = source;
		if (!keep_parts)
			p.equip_rest = {}; // Names MW2 receiver bones.
		p.id = id;
		p.receiver = receiver;
		p.variant = "mwr_rebased";
		p.reload = reload;
		p.suppress_equip = [](std::string_view) noexcept { return false; };
		p.viewmodel = {.visibility = part_visibility::surface};
		return p;
	}

	profile_match bind_attachments(const profile& selected, std::span<const hands::model_definition> models,
		const hands::model_definition& receiver, const hands::rig& rig,
		std::span<const hands::bone_definition> bones) noexcept
	{
		if (rig.count <= 0 || bones.size() != static_cast<size_t>(rig.count))
			return {nullptr, "MWR assembly rig unavailable"};
		for (const auto& model : models)
		{
			// Hand models precede the receiver in the assembly.
			if (&model == &receiver || model.count <= 0 || model.begin + model.count <= receiver.begin)
				continue;
			const int root = model.begin;
			if (root < receiver.begin + receiver.count || root >= rig.count)
				return {nullptr, "MWR attachment precedes its receiver"};
			const int parent = rig.parent[root];
			if (parent < receiver.begin || parent >= root || !rig.weapon_bones[parent])
				return {nullptr, "MWR attachment is not parented to the weapon"};
			// Attachments may have several roots (e.g. RPD bipod legs on receiver tags).
			for (int i = root; i < root + model.count && i < rig.count; ++i)
			{
				const int p = rig.parent[i];
				if (p < receiver.begin || p >= i || !rig.weapon_bones[p])
					return {nullptr, "MWR attachment has a detached bone"};
			}
		}
		return {&selected, "MWR shared recipe matched", {}, -1};
	}
}
