#include <std_include.hpp>
#include "mwr_rebase.hpp"
#include "../part_grip_pose.hpp"
#include "../charging_handle_bolt.hpp"
#include "../hands/pose_solver.hpp"

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
		const auto magazine_shift = hands::sub(t.magazine_bind.position, source.magazine_rest.position);
		const auto slide_shift =
		    t.slide_same_part ? hands::sub(t.slide_bind.position, source.slide_rest.position) : vec{};

		p.id = t.id;
		if (t.native_family)
			p.native_family = t.native_family;
		p.magazine_bone = t.magazine_bone;
		p.slide_bone = t.slide_bone;
		p.bullets_bone = t.bullets_bone;
		p.additional_bullet_bones = t.additional_bullet_bones;
		p.magazine_model = t.magazine_model;

		// Bone-anchored rest poses come from the MWR bind pose.
		p.magazine_rest = t.magazine_bind;
		p.slide_rest = t.slide_bind;

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
		p.skinned_receiver = source.skinned_receiver ? receiver : nullptr;
		p.rigid_magazine_source = t.magazine_from_receiver || source.rigid_magazine_source ? receiver : nullptr;
		if (t.magazine_from_receiver)
			p.magazine_model = receiver;
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
			if (p.handle_catch)
			{
				p.handle_catch = nullptr;
				p.ammunition.manual_catch = false;
				p.interaction.manual_catch = nullptr;
			}
		}
		else if (t.additional_bullet_bones.empty())
			p.additional_bullet_bones = source.additional_bullet_bones;

		// Sound keys are notetrack lookups resolved in the live WeaponDef map
		// (missing keys stay silent); MWR keeps the COD4 names MW2 inherited.
		if (t.sound_key)
			p.sound_key = t.sound_key;
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
			for (int i = root; i < root + model.count && i < rig.count; ++i)
				if (!hands::descendant(i, root, rig))
					return {nullptr, "MWR attachment has a detached bone"};
		}
		return {&selected, "MWR shared recipe matched", {}, -1};
	}
}
