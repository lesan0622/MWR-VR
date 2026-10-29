#include <std_include.hpp>
#include "c4_detonator.hpp"
#include "view_aimed_weapon.hpp"
#include "native_scripted_control.hpp"
#include "weapon_carry_runtime.hpp"
#include "carry_interaction.hpp"
#include "hands/pose_math.hpp"
#include "hands/position_offset.hpp"
#include "../settings.hpp"
#include "../head_pose_bridge.hpp"
#include "hands/service.hpp"
#include "hands/native_schema.hpp"
#include "hands/pose_mirror.hpp"
#include "hands/attachment_pose.hpp"
#include "campaign/cliffhanger/profile.hpp"
#include "../digital_button_gate.hpp"
#include "../engine_stereo_view.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/fastfiles.hpp"
#include "component/scene_models.hpp"
#include "component/scene_rigid_part.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/scripting/execution.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/native_memory.hpp>

// MWR maps/_c4 (bog_a ZPU). Planting gives the player the empty "c4" detonator
// and handle_detonation() waits for the player's "detonate" notify, which the
// VR input never produced natively (not even console +attack in a mock).
// While the player owns that detonator it is forced into the hand that does
// not hold the gun (a support grip there is released): the original H2
// Cliffhanger detonator pose/fingers on the MWR viewmodel_c4 detonator parts,
// placed from the same skin record as the hands (as held magazines are). A
// fresh trigger press on that hand sends "detonate", as designator_events
// sends "use_laser".
namespace vr::gameplay::c4_detonator
{
	namespace
	{
		using namespace hands;
		using namespace hands::pose_math;
		using clock = controller_input::clock;
		namespace authored = equipment::special::cliffhanger::authored;
		struct snapshot
		{
			vr::hand holder{vr::hand::none};
			std::uint64_t reference{};
			std::array<quat, 2> mirror{}, basis{};
			clock::time_point at{};
		};
		std::mutex mutex;
		snapshot published, submitted;
		std::atomic<int> holding{-1};
		vr::hand holder{vr::hand::none};
		bool detonated{};
		controller_input::digital_press_gate trigger{};
		std::unique_ptr<scene_models::rigid_part> part;
		std::atomic<game::XModel*> model{};
		game::XModel* attempted{};
		unsigned short lighting{};
		std::atomic_uint64_t detonations{}, releases{}, submitted_count{}, prepared{}, rejected{};
		std::atomic<const char*> reason{"not observed"}, model_reason{"not attempted"};
		bool fresh(clock::time_point at) noexcept {const auto now = clock::now();return now >= at && now - at <= 150ms;}
		const char* weapon_name(std::uint32_t token) noexcept
		{
			const auto index = token & 0x1ff;
			if (!index) return nullptr;
			const auto* definition = game::weapon_defs[index];
			const char* name{};
			return definition && utils::native_memory::read_bytes(&name, &definition->szInternalName, sizeof(name)) ? name : nullptr;
		}
		// MWR player-state inventory tokens (ps+0x2CC, 15 slots; vr_capture_ps).
		std::uint32_t detonator_token() noexcept
		{
			const auto* ps = reinterpret_cast<const std::byte*>(game::g_entities[0].client);
			if (!ps) return 0;
			for (unsigned i = 0; i < 15; ++i)
			{
				std::uint32_t token{};
				if (!utils::native_memory::read_bytes(&token, ps + 0x2cc + i * 4, sizeof(token))) return 0;
				if (token && view_aimed::scripted_detonator(token)) return token & 0x1ff;
			}
			return 0;
		}
		bool owns_detonator() noexcept {return detonator_token() != 0;}
		anchor attachment(unsigned h, const snapshot& s) noexcept
		{
			return h ? authored::detonator_attachment : pose_mirror::object_in_wrist({}, authored::detonator_attachment, s.mirror[h]);
		}
		anchor placement(const anchor& wrist, unsigned h, const snapshot& s, game::XModel* mesh) noexcept
		{
			// Rigid-part vertices keep viewmodel_c4 bind space; the authored root is tag_clip2.
			const auto b = part ? part->bind() : std::array<float, 7>{};
			const anchor bind_root{{b[0], b[1], b[2]}, normalize(quat{b[3], b[4], b[5], b[6]})};
			(void)mesh;
			return compose(compose(wrist, attachment(h, s)), inverse(bind_root));
		}
		void update()
		{
			if (!game::CL_IsCgameInitialized() || !game::g_entities[0].client) {holder = vr::hand::none;holding = -1;return;}
			const auto input = controller_input::latest_interaction();
			const bool owned = owns_detonator() && scripted_control::player_owns_arms(game::g_entities[0].client);
			if (!owned) detonated = false;
			if (!owned || detonated || !weapons::carry::active())
			{
				holder = vr::hand::none;holding = -1;trigger = {};
				reason = !owned ? "no detonator" : detonated ? "detonated" : "carry inactive";
			}
			else
			{
				if (holder == vr::hand::none)
				{
					// The hand that does not hold the gun; a support grip there is released.
					unsigned rear{};
					for (const auto& held : weapons::carry::held_instances())
						if (held.id && vr::valid_hand(held.owner.rear)) rear |= 1u << unsigned(held.owner.rear);
					holder = (rear & 2u) && !(rear & 1u) ? vr::hand::left : (rear & 1u) && !(rear & 2u) ? vr::hand::right : vr::hand::left;
					trigger = {};
				}
				for (const auto& held : weapons::carry::held_instances())
					if (held.id && held.owner.support == holder && weapons::carry::release_support(held.id)) ++releases;
				holding = int(holder);
				reason = "held";
				const auto h = unsigned(holder);
				const bool paused = [] {const auto* p = game::Dvar_FindVar("cl_paused");return !p || p->current.integer;}();
				const bool live = input.focused && fresh(input.sampled_at) && !paused && !*game::keyCatchers;
				if (!live) trigger = {};
				else if (trigger.consume(input.trigger[h]))
				{
					try
					{
						const scripting::entity player{game::scr_entref_t{0, 0}};
						scripting::notify(player, "detonate", {});
						++detonations;detonated = true;
					}
					catch (...) {++rejected;}
				}
			}
			const std::lock_guard lock(mutex);
			published.holder = holder;
			published.reference = input.reference_generation;
			published.at = input.sampled_at;
		}
		void refresh()
		{
			if (model.load()) return;
			if (!game::CL_IsCgameInitialized()) {model_reason = "cgame not initialized";return;}
			if (!scene_models::ready()) {model_reason = "scene models not ready";return;}
			// The detonator's own view model (viewmodel_c4), not a name lookup.
			const auto token = detonator_token();
			if (!token) {model_reason = "no detonator in inventory";return;}
			const auto* definition = game::weapon_defs[token];
			auto* source = definition && definition->gunModel ? definition->gunModel[0] : nullptr;
			if (!source) {model_reason = "detonator view model missing";return;}
			if (source == attempted) return;
			attempted = source;
			if (!source->boneNames || !source->baseMat || source->numBones != 8) {model_reason = "detonator view model is not the 8-bone viewmodel_c4";return;}
			int root = -1;std::vector<unsigned> bones;
			for (unsigned b = 0; b < source->numBones; ++b)
			{
				const auto* n = game::SL_ConvertToString(source->boneNames[b]);
				if (!n) continue;
				const std::string_view name{n};
				if (name == "tag_clip2") root = int(b);
				// Detonator body, safety bar, button, lever and antenna; not the C4 block (tag_clip).
				if (name == "tag_clip2" || name == "j_bar" || name == "j_button" || name == "j_handle_on" || name == "j_rubber_base" || name == "j_rubber_tip")
					bones.push_back(b);
			}
			if (root < 0) {model_reason = "viewmodel_c4 tag_clip2 missing";return;}
			auto next = std::make_unique<scene_models::rigid_part>();
			// Rigid groups first; the loaded MWR detonator keeps parts in skinned
			// surfaces, so then cut whole-bone triangles out of them.
			const char* failures[3]{};
			bool created = next->create(source, unsigned(root), bones);
			if (!created) {failures[0] = next->status();created = next->create_rigid_bones(source, unsigned(root), bones);}
			if (!created) {failures[1] = next->status();created = next->create_rigid_bone(source, unsigned(root));}
			if (!created) {failures[2] = next->status();model_reason = failures[1] ? failures[1] : failures[0];return;}
			const std::array<scene_models::runtime_model, 1> identity{{{next->model(), source}}};
			if (!scene_models::register_runtime_models(identity)) {model_reason = "detonator registration rejected";return;}
			part = std::move(next);model = part->model();model_reason = "ready";
		}
		void retire()
		{
			model = nullptr;attempted = nullptr;part.reset();
		}
		void submit()
		{
			snapshot s;
			{
				const std::lock_guard lock(mutex);
				s = published;
				submitted = s;
			}
			auto* mesh = model.load();
			if (!mesh || !vr::valid_hand(s.holder) || !fresh(s.at)) return;
			// MWR has no render-time hand record (as for held magazines): place the
			// detonator at the tracked visual wrist with the hand's wrist basis.
			const auto h = unsigned(s.holder);
			const auto input = controller_input::latest_interaction();
			head_pose_bridge::spatial_frame body;
			position_offsets offsets;
			const auto setting = [](const char* key, float fallback)
			{
				const auto* d = game::Dvar_FindVar(key);
				return d ? d->current.value : fallback;
			};
			offsets = {setting(vr::settings::active_hand_alignment()[0].name, offsets.inward_meters),
			           setting(vr::settings::active_hand_alignment()[1].name, offsets.back_meters),
			           setting(vr::settings::active_hand_alignment()[2].name, offsets.up_meters)};
			anchor wrist;
			if (!head_pose_bridge::get_spatial_frame(body) || body.generation != input.reference_generation ||
				!tracked_wrist(input, body, {}, int(h), offsets, wrist))
				return;
			wrist.rotation = normalize(multiply(wrist.rotation, s.basis[h]));
			const auto at = placement(wrist, h, s, mesh);
			game::GfxScaledPlacement p{};
			p.scale = 1;
			std::copy(at.position.begin(), at.position.end(), p.base.origin);
			std::copy(at.rotation.begin(), at.rotation.end(), p.base.quat);
			float white[]{1, 1, 1, 1};
			scene_models::submit(mesh, &p, scene_models::no_cast_shadow, &lighting, white, white, white, 8.f);
			++submitted_count;
		}
		scene_models::placement_result prepare(const scene_models::preparation& p, const void* entry,
			game::GfxPlacement& current, game::GfxPlacement& previous) noexcept
		{
			using result = scene_models::placement_result;
			std::uintptr_t handle{};
			if (!utils::native_memory::read_bytes(&handle, static_cast<const std::byte*>(entry) + 0x68, 8) ||
				handle != reinterpret_cast<std::uintptr_t>(&lighting))
				return result::unchanged;
			snapshot s, now;
			{
				const std::lock_guard lock(mutex);
				s = submitted;
				now = published;
			}
			if (!vr::valid_hand(s.holder) || s.holder != now.holder || s.reference != now.reference) return result::omit;
			std::array<float, 12> camera{};
			vec origin{};
			attachments::solved pose;
			if (!utils::native_memory::read_bytes(camera.data(), static_cast<const std::byte*>(p.record) + engine_stereo_view::h2_view_origin_offset, sizeof(camera)) ||
				!attachments::for_record(p.record, camera, pose))
				return result::unchanged; // MWR: keep the submitted tracked-wrist placement.
			if (pose.reference != s.reference ||
				!utils::native_memory::read_bytes(origin.data(), static_cast<const std::byte*>(p.record) + engine_stereo_view::h2_current_model_placement_origin_offset, sizeof(origin)))
				return result::omit;
			for (float x : origin) if (!std::isfinite(x) || std::abs(x) > 1e7f) return result::omit;
			const auto h = unsigned(s.holder);
			const auto at = placement({add(pose.wrists[h].position, origin), pose.wrists[h].rotation}, h, s, nullptr);
			std::copy(at.position.begin(), at.position.end(), current.origin);
			std::copy(at.rotation.begin(), at.rotation.end(), current.quat);
			previous = current;
			++prepared;
			return result::replace;
		}
	}
	bool holds(vr::hand h) noexcept {return vr::valid_hand(h) && holding.load() == int(h);}
	void present(const hands::interaction_rig& parts, const hands::rig& r, const controller_input::frame& input,
		const std::array<anchor, 2>& targets, std::span<bone> solved, unsigned, unsigned visible) noexcept
	{
		if (!parts.valid || r.count <= 0 || r.count > 256 || solved.size() < std::size_t(r.count)) return;
		snapshot s;
		{
			const std::lock_guard lock(mutex);
			published.basis = parts.basis;
			for (unsigned h = 0; h < 2; ++h) published.mirror[h] = parts.library.mirror_basis[r.arms[h].wrist];
			s = published;
		}
		if (!vr::valid_hand(s.holder) || !model.load() || !fresh(s.at) || s.reference != input.reference_generation) return;
		const auto h = unsigned(s.holder);
		if (!(visible & (1u << h)) || !input.grip[h].valid) return;
		// The original Cliffhanger detonator grip: controller-oriented wrist, authored fingers.
		move_part(r, r.arms[h].wrist, {solved[r.arms[h].wrist].position, normalize(multiply(targets[h].rotation, parts.basis[h]))}, solved);
		pose_mirror::fingers(r, parts.library, native_hand_schema::definition, authored::detonator_fingers, h, solved, h == 0);
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			scheduler::loop(update, scheduler::pipeline::server);
			scheduler::loop(refresh, scheduler::pipeline::main, 250ms);
			scene_models::on_submit(submit);
			scene_models::on_prepare_placement(prepare);
			fastfiles::on_pre_unload(retire);
			::command::add("vr_c4_status", []
			{
				console::info("[VR C4] reason=%s model_reason=%s holder=%d model=%d detonations=%llu support_releases=%llu submitted=%llu prepared=%llu rejected=%llu\n",
					reason.load(), model_reason.load(), holding.load(), model.load() != nullptr,
					static_cast<unsigned long long>(detonations.load()), static_cast<unsigned long long>(releases.load()),
					static_cast<unsigned long long>(submitted_count.load()), static_cast<unsigned long long>(prepared.load()),
					static_cast<unsigned long long>(rejected.load()));
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::c4_detonator::component)
