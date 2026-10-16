#include <std_include.hpp>
#include "weapon_capture.hpp"
#include "native_animation_query.hpp"
#include "hands/pose_math.hpp"
#include "native_scripted_control.hpp"
#include "native_carry.hpp"
#include "weapon_carry_runtime.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
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
			const auto dir = directory(record_weapon);
			std::string file;
			do file = std::format("{}/record_{:03}.jsonl", dir, ++record_index);
			while (utils::io::file_exists(file)); // Never overwrite an earlier capture.
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
			// MWR port diagnostic: compare the server and predicted player states
			// used by scripted-control admission (weapon flags, entity flags, life).
			command::add("vr_capture_ps", []
			{
				const auto dump = [](const char* label, const void* ps)
				{
					if (!ps) { console::info("[VR capture] %s ps=null\n", label); return; }
					const auto* b = static_cast<const std::uint8_t*>(ps);
					std::uint32_t flags{}, eflags{}, weapon{};
					int health{};
					std::memcpy(&flags, b + 0x390, 4);
					std::memcpy(&eflags, b + 0x58, 4);
					std::memcpy(&weapon, b + 0x38c, 4);
					std::memcpy(&health, b + 0x1dc, 4);
					console::info("[VR capture] %s ps=%p pm_type=%u weapflags=0x%08X eflags=0x%08X weapon=%u health=%d\n",
						label, ps, b[2], flags, eflags, weapon, health);
					std::string owned;
					for (int i = 0; i < 15; ++i)
					{
						std::uint32_t token{};
						std::memcpy(&token, b + 0x2cc + i * 4, 4);
						if (token) owned += std::format(" {}:{}", token, weapon_name(token));
					}
					console::info("[VR capture] %s inventory:%s\n", label, owned.c_str());
				};
				dump("server", game::g_entities[0].client);
				bool dead{};
				const void* server = game::g_entities[0].client;
				console::info("[VR capture] server contract=%d life=%d dead=%d allowed=%d owns_body=%d owns_arms=%d airport=%d\n",
					scripted_control::native_contract(), player_life::read(server, dead), dead,
					scripted_control::allowed(server), sequences::owns_body(server), sequences::owns_arms(server),
					sequences::airport::opening::active());
				console::info("[VR capture] carry native=%s active=%d\n", weapons::native_carry::status(), weapons::carry::active());
				dump("predicted", game::CL_IsCgameInitialized() ? game::CG_GetPredictedPlayerState(0) : nullptr);
			});
			// MWR port: bind poses of every loaded weapon receiver, used to rebase
			// the original MW2 recipes onto the MWR viewmodels offline.
			command::add("vr_capture_weapon_models", []
			{
				std::string out;
				const auto model_json = [](const game::XModel* m)
				{
					if (!m || !m->name) return std::string("null");
					std::string bones;
					for (unsigned i = 0; i < m->numBones; ++i)
					{
						const auto parent = i < m->numRootBones || !m->parentList ? -1 :
							static_cast<int>(i) - static_cast<int>(m->parentList[i - m->numRootBones]);
						const auto& b = m->baseMat[i];
						bones += std::format("{}[\"{}\",{},[{:.6f},{:.6f},{:.6f}],[{:.7f},{:.7f},{:.7f},{:.7f}]]", i ? "," : "",
							escape(scr_string(m->boneNames[i])), parent, b.trans[0], b.trans[1], b.trans[2],
							b.quat[0], b.quat[1], b.quat[2], b.quat[3]);
					}
					const auto& v = m->bounds;
					return std::format("{{\"name\":\"{}\",\"bounds\":[[{:.4f},{:.4f},{:.4f}],[{:.4f},{:.4f},{:.4f}]],\"bones\":[{}]}}",
						escape(m->name), v.midPoint[0], v.midPoint[1], v.midPoint[2], v.halfSize[0], v.halfSize[1], v.halfSize[2], bones);
				};
				int count{};
				for (unsigned i = 1; i < 512; ++i)
				{
					const auto* def = game::weapon_defs[i];
					if (!def) break; // Loaded definitions are contiguous; the table ends at the first gap.
					game::WeaponDef copy{};
					if (!utils::native_memory::read_bytes(&copy, def, sizeof(copy))) break;
					if (!copy.szInternalName || !copy.gunModel || !copy.gunModel[0]) continue;
					out += std::format("{{\"index\":{},\"name\":\"{}\",\"gun\":{},\"clip\":{}}}\n", i, escape(def->szInternalName),
						model_json(def->gunModel[0]), model_json(def->worldClipModel));
					++count;
				}
				const auto* map = game::Dvar_FindVar("mapname");
				const auto file = directory("_models") + "/" + (map ? map->current.string : "unknown") + ".jsonl";
				utils::io::write_file(file, out);
				console::info("[VR capture] wrote %d weapon receivers to %s\n", count, file.c_str());
			});
			// Select an owned weapon by name (after `give`), for capture sessions.
			command::add("vr_capture_select", [](const command::params& params)
			{
				if (params.size() < 2) return;
				const std::string wanted = params.get(1);
				for (unsigned i = 1; i < 512; ++i)
				{
					const auto* def = game::weapon_defs[i];
					if (!def) break;
					if (!def->szInternalName || wanted != def->szInternalName) continue;
					scheduler::once([wanted, i]
					{
						// Console give is not admitted in MWR SP; grant the definition directly.
						game::Weapon weapon{};
						weapon.data = i;
						auto* ps = game::g_entities[0].client;
						if (ps && game::G_GivePlayerWeapon(ps, weapon, 0, 0, 0, 0))
							game::G_InitializeAmmo(ps, weapon, 0);
						const bool ok = weapons::native_carry::select(i);
						console::info("[VR capture] select %s (%u) -> %d (%s)\n", wanted.c_str(), i, ok,
							weapons::native_carry::status());
					}, scheduler::pipeline::server);
					return;
				}
				console::info("[VR capture] select %s: not loaded\n", wanted.c_str());
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
