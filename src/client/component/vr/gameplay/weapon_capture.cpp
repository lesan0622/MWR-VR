#include <std_include.hpp>
#include "weapon_capture.hpp"
#include "native_animation_query.hpp"
#include "hands/pose_math.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"

#include <utils/io.hpp>
#include <utils/native_memory.hpp>

#include <format>
#include <mutex>

namespace vr::gameplay::weapon_capture
{
	namespace
	{
		using namespace hands;

		std::mutex mutex;
		bool snapshot_requested{};
		bool recording{};
		std::string record_header, record_body;
		std::string record_weapon;
		std::uint64_t record_frames{};
		const void* last_tree{};
		std::uint32_t record_index{};

		std::string escape(std::string_view text)
		{
			std::string out;
			for (const char c : text)
			{
				if (c == '"' || c == '\\') out.push_back('\\');
				if (static_cast<unsigned char>(c) >= 0x20) out.push_back(c);
			}
			return out;
		}

		std::string scr_string(game::scr_string_t value)
		{
			if (!value) return {};
			const char* text = game::SL_ConvertToString(value);
			return text ? std::string(text) : std::string{};
		}

		std::string pose_json(const anchor& a)
		{
			return std::format("[{:.6f},{:.6f},{:.6f},{:.7f},{:.7f},{:.7f},{:.7f}]", a.position[0], a.position[1],
				a.position[2], a.rotation[0], a.rotation[1], a.rotation[2], a.rotation[3]);
		}

		std::string weapon_name(std::uint32_t weapon)
		{
			const auto index = weapon & 0x1ff;
			const auto* def = index ? game::weapon_defs[index] : nullptr;
			return def && def->szInternalName ? std::string(def->szInternalName) : std::format("weapon_{}", index);
		}

		std::string directory(const std::string& name)
		{
			const auto dir = "overlord-mwr/captures/" + name;
			utils::io::create_directory(dir);
			return dir;
		}

		std::string anim_json(const game::XAnimParts* parts)
		{
			if (!parts || !parts->name) return "null";
			std::string notifies;
			if (parts->notify)
				for (unsigned i = 0; i < parts->notifyCount; ++i)
					notifies += std::format("{}{{\"name\":\"{}\",\"time\":{:.5f}}}", i ? "," : "",
						escape(scr_string(parts->notify[i].name)), parts->notify[i].time);
			return std::format("{{\"name\":\"{}\",\"frames\":{},\"framerate\":{:.4f},\"notify\":[{}]}}",
				escape(parts->name), parts->numframes, parts->framerate, notifies);
		}

		// WeaponDef facts used by reload recipes (names, capacities, timers,
		// animations and their notetracks, notetrack sound map).
		std::string weapon_json(std::uint32_t weapon)
		{
			const auto index = weapon & 0x1ff;
			const auto* def = index ? game::weapon_defs[index] : nullptr;
			if (!def) return "null";
			const auto model = [](const game::XModel* m) { return m && m->name ? escape(m->name) : std::string{}; };
			std::string anims;
			if (def->szXAnims)
				for (int i = 0; i < 190; ++i)
					if (def->szXAnims[i] && def->szXAnims[i]->name)
						anims += std::format("{}\"{}\":{}", anims.empty() ? "" : ",", i, anim_json(def->szXAnims[i]));
			std::string sounds;
			if (def->notetrackSoundMapKeys && def->notetrackSoundMapValues)
				for (int i = 0; i < 36; ++i)
					if (def->notetrackSoundMapKeys[i])
						sounds += std::format("{}\"{}\":\"{}\"", sounds.empty() ? "" : ",",
							escape(scr_string(def->notetrackSoundMapKeys[i])),
							escape(scr_string(def->notetrackSoundMapValues[i])));
			const auto& t = def->stateTimers;
			return std::format(
				"{{\"token\":{},\"name\":\"{}\",\"gun_models\":[\"{}\",\"{}\"],\"hand_model\":\"{}\","
				"\"world_clip_model\":\"{}\",\"weap_type\":{},\"weap_class\":{},\"clip_size\":{},"
				"\"reload_ammo_add\":{},\"segmented_reload\":{},\"no_partial_reload\":{},"
				"\"timers_ms\":{{\"reload\":{},\"reload_empty\":{},\"reload_add\":{},\"reload_start\":{},"
				"\"reload_end\":{},\"rechamber\":{},\"raise\":{},\"drop\":{}}},"
				"\"anims\":{{{}}},\"notetrack_sounds\":{{{}}}}}",
				weapon, escape(def->szInternalName ? def->szInternalName : ""),
				model(def->gunModel ? def->gunModel[0] : nullptr), model(def->gunModel ? def->gunModel[1] : nullptr),
				model(def->handModel), model(def->worldClipModel), static_cast<int>(def->weapType),
				static_cast<int>(def->weapClass), def->clipSize, def->reloadAmmoAdd, def->segmentedReload,
				def->noPartialReload, t.reloadTime, t.reloadEmptyTime, t.reloadAddTime, t.reloadStartTime,
				t.reloadEndTime, t.rechamberTime, t.raiseTime, t.dropTime, anims, sounds);
		}

		std::string assembly_json(std::uint32_t weapon, std::span<const model_definition> models, const rig& r,
			std::span<const bone_definition> bones, std::span<const bone> native)
		{
			std::string model_list, bone_list;
			for (const auto& m : models)
				model_list += std::format("{}{{\"name\":\"{}\",\"begin\":{},\"count\":{}}}", model_list.empty() ? "" : ",",
					escape(m.name), m.begin, m.count);
			const auto gun = inverse(pose_math::as_anchor(native[r.gun]));
			for (int i = 0; i < r.count; ++i)
				bone_list += std::format("{}{{\"name\":\"{}\",\"parent\":{},\"weapon\":{},\"bind\":{},\"idle_gun_local\":{}}}",
					i ? "," : "", escape(bones[i].name), bones[i].parent, r.weapon_bones[i] ? 1 : 0,
					pose_json(pose_math::as_anchor(bones[i].bind)),
					pose_json(compose(gun, pose_math::as_anchor(native[i]))));
			return std::format(
				"{{\"units\":\"native (inches)\",\"frame\":\"gun-local unless named bind\",\"rig\":{{\"gun\":{},"
				"\"weapon_tag\":{},\"muzzle\":{},\"left_wrist\":{},\"right_wrist\":{}}},\"models\":[{}],"
				"\"weapon\":{},\"bones\":[{}]}}\n",
				r.gun, r.weapon_tag, r.muzzle, r.arms[0].wrist, r.arms[1].wrist, model_list, weapon_json(weapon),
				bone_list);
		}

		void flush_recording()
		{
			if (record_weapon.empty() || !record_frames) return;
			const auto file = std::format("{}/record_{:03}.jsonl", directory(record_weapon), ++record_index);
			utils::io::write_file(file, record_header + record_body);
			console::info("[VR capture] wrote %llu frames to %s\n", record_frames, file.c_str());
		}
	}

	bool wants() noexcept
	{
		const std::lock_guard lock(mutex);
		return snapshot_requested || recording;
	}

	void observe(const void* tree, std::uint32_t weapon, std::span<const model_definition> models, const rig& r,
		std::span<const bone_definition> bones, std::span<const bone> native) noexcept
	{
		if (r.count <= 0 || r.gun < 0 || r.gun >= r.count || native.size() < static_cast<size_t>(r.count))
			return;
		const std::lock_guard lock(mutex);
		last_tree = tree;
		if (snapshot_requested)
		{
			snapshot_requested = false;
			const auto name = weapon_name(weapon);
			const auto file = directory(name) + "/assembly.json";
			utils::io::write_file(file, assembly_json(weapon, models, r, bones, native));
			console::info("[VR capture] wrote %s\n", file.c_str());
		}
		if (!recording) return;
		if (record_weapon.empty())
		{
			record_weapon = weapon_name(weapon);
			std::string names;
			for (int i = 0; i < r.count; ++i)
				names += std::format("{}\"{}\"", i ? "," : "", escape(bones[i].name));
			record_header = std::format("{{\"bones\":[{}],\"gun\":{}}}\n", names, r.gun);
		}
		const auto clips = native_animation::sample(tree);
		std::string clip_list;
		for (const auto& c : clips.leaves())
			clip_list += std::format("{}[\"{}\",{:.4f},{:.4f}]", clip_list.empty() ? "" : ",", escape(c.name), c.time,
				c.weight);
		const auto gun = inverse(pose_math::as_anchor(native[r.gun]));
		std::string pose_list;
		for (int i = 0; i < r.count; ++i)
			pose_list += (i ? "," : "") + pose_json(compose(gun, pose_math::as_anchor(native[i])));
		record_body += std::format("{{\"t\":{},\"clips\":[{}],\"gun_world\":{},\"poses\":[{}]}}\n",
			std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
			clip_list, pose_json(pose_math::as_anchor(native[r.gun])), pose_list);
		++record_frames;
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			command::add("vr_capture_weapon", []
			{
				const std::lock_guard lock(mutex);
				snapshot_requested = true;
				console::info("[VR capture] assembly snapshot requested (next solved viewmodel frame)\n");
			});
			command::add("vr_capture_record", [](const command::params& params)
			{
				const bool on = params.size() < 2 || std::atoi(params.get(1)) != 0;
				const std::lock_guard lock(mutex);
				if (on)
				{
					recording = true;
					record_weapon.clear();
					record_header.clear();
					record_body.clear();
					record_frames = 0;
					console::info("[VR capture] recording native skeleton frames\n");
				}
				else
				{
					recording = false;
					flush_recording();
					record_body.clear();
				}
			});
		}
	};
}

REGISTER_COMPONENT(vr::gameplay::weapon_capture::component)
