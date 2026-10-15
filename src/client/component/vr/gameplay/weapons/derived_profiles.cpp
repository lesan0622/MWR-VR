#include <std_include.hpp>
#include "derived_profiles.hpp"
#include "../hands/pose_math.hpp"

#include <deque>
#include <format>
#include <mutex>

namespace vr::gameplay::weapons::derived
{
	namespace
	{
		using namespace hands;

		// Hands are "idle" when both wrists keep the same gun-local pose for this
		// many consecutive native skeletons (one per solved frame).
		constexpr int stable_samples = 24;
		constexpr float stable_units = 0.08f;   // Native units (inches).
		constexpr float stable_dot = 0.9998f;   // ~2.3 degrees.
		constexpr float max_reach_units = 40.f; // Wrist-to-gun sanity bound.

		struct entry
		{
			std::string key, receiver, id;
			std::vector<std::string> names;
			std::vector<joint_pose> fingers;
			profile value{};
			std::array<anchor, 2> last{};
			int stable{};
			bool final{};
			std::uint64_t samples{};
		};

		std::mutex mutex;
		std::deque<entry> entries; // Stable addresses: profiles are referenced by bindings.

		std::string assembly_key(std::span<const model_definition> models)
		{
			std::string key;
			for (const auto& model : models)
			{
				key.append(model.name);
				key.push_back('|');
			}
			return key;
		}

		bool finger_joint(std::string_view name) noexcept
		{
			for (const auto prefix : {"j_index_", "j_mid_", "j_ring_", "j_pinky_", "j_thumb_", "j_pinkypalm_",
				"j_ringpalm_", "j_webbing_"})
				if (name.starts_with(prefix))
					return true;
			return false;
		}

		bool same(const anchor& a, const anchor& b) noexcept
		{
			const auto d = sub(a.position, b.position);
			float dot{};
			for (int i = 0; i < 4; ++i)
				dot += a.rotation[i] * b.rotation[i];
			return length(d) < stable_units && std::abs(dot) > stable_dot;
		}

		entry* find_locked(const std::string& key) noexcept
		{
			for (auto& e : entries)
				if (e.key == key)
					return &e;
			return nullptr;
		}
	}

	const profile* find(std::span<const model_definition> models) noexcept
	{
		const auto key = assembly_key(models);
		const std::lock_guard lock(mutex);
		const auto* e = find_locked(key);
		return e && e->final ? &e->value : nullptr;
	}

	bool observe(std::span<const model_definition> models, const rig& r, std::span<const bone_definition> bones,
		std::span<const bone> native) noexcept
	{
		if (r.count <= 0 || r.gun < 0 || r.gun >= r.count || native.size() < static_cast<size_t>(r.count) ||
			bones.size() < static_cast<size_t>(r.count))
			return false;
		for (const auto& arm : r.arms)
			if (arm.wrist < 0 || arm.wrist >= r.count)
				return false;
		const auto gun = inverse(pose_math::as_anchor(native[r.gun]));
		std::array<anchor, 2> wrists{};
		for (int h = 0; h < 2; ++h)
		{
			wrists[h] = compose(gun, pose_math::as_anchor(native[r.arms[h].wrist]));
			if (length(wrists[h].position) > max_reach_units)
				return false;
		}

		const auto key = assembly_key(models);
		const std::lock_guard lock(mutex);
		auto* e = find_locked(key);
		if (!e)
		{
			e = &entries.emplace_back();
			e->key = key;
			for (const auto& model : models)
				if (r.gun >= model.begin && r.gun < model.begin + model.count)
					e->receiver = std::string(model.name);
			e->id = "derived:" + e->receiver;
		}
		if (e->final)
			return false;
		++e->samples;
		e->stable = same(wrists[0], e->last[0]) && same(wrists[1], e->last[1]) ? e->stable + 1 : 0;
		e->last = wrists;
		if (e->stable < stable_samples)
			return false;

		// Parent-local finger rotations from the same idle skeleton, as the
		// authored idle_fingers tables were extracted from idle frame 0.
		std::vector<std::pair<int, quat>> joints;
		for (int i = 0; i < r.count; ++i)
		{
			const auto parent = bones[i].parent;
			if (!finger_joint(bones[i].name) || parent < 0 || parent >= r.count)
				continue;
			joints.emplace_back(i, normalize(multiply(conjugate(normalize(native[parent].rotation)),
				normalize(native[i].rotation))));
		}
		if (joints.empty() || joints.size() > 64)
			return false;
		e->names.reserve(joints.size());
		for (const auto& [index, rotation] : joints)
			e->names.emplace_back(bones[index].name);
		for (size_t j = 0; j < joints.size(); ++j)
			e->fingers.push_back({e->names[j], joints[j].second});

		e->value = profile{
			.id = e->id,
			.receiver = e->receiver,
			.variant = "native_idle",
			.aiming = aim_rule::two_hand,
			.wrists = wrists,
			.authored_rear = 1,
			.acquire_meters = profile_defaults::acquire_meters,
			.release_meters = profile_defaults::release_meters,
			.blend_seconds = profile_defaults::blend_seconds,
			.fingers = e->fingers,
			.equip_rest = {},
			.suppress_equip = [](std::string_view) { return false; },
		};
		e->final = true;
		return true;
	}

	std::string status()
	{
		const std::lock_guard lock(mutex);
		std::string out;
		for (const auto& e : entries)
		{
			out += std::format("derived_profile receiver={} final={} samples={} stable={} fingers={} "
				"support=[{:.2f},{:.2f},{:.2f}] rear=[{:.2f},{:.2f},{:.2f}]\n",
				e.receiver, e.final, e.samples, e.stable, e.fingers.size(),
				e.last[0].position[0], e.last[0].position[1], e.last[0].position[2],
				e.last[1].position[0], e.last[1].position[1], e.last[1].position[2]);
		}
		return out;
	}
}
