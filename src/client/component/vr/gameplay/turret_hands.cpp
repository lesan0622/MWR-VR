#include <std_include.hpp>
#include "turret_hands.hpp"
#include "barrett_gun.hpp"
#include "bar_hand_pose.hpp"
#include "empty_hand_pose.hpp"
#include "held_item_pose.hpp"
#include "view_aimed_weapon.hpp"
#include "hands/native_rig.hpp"
#include "../h2/entrypoints.hpp"
#include "hands/native_schema.hpp"
#include "../head_pose_bridge.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/scripting/execution.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/native_memory.hpp>

namespace vr::gameplay::turret_hands
{
	namespace
	{
		using namespace hands;
		using clock = std::chrono::steady_clock;
		struct grips
		{
			int entity{-1};
			vec origin{};
			std::array<vec, 3> axis{}; // Anchor forward, left, up.
			vec grip{}; // Left grip centre in the anchor frame; the right mirrors it.
			clock::time_point at{};
		};
		std::mutex mutex;
		grips published;
		// Turret models with fitted grips (Overlord also keeps one profile per
		// mounted gun); other turrets keep the native view without hands.
		// weapon_minigun: the anchor is the rendered barrel bone (mg01 ->
		// tag_aim_animated, which carries j_spin and tag_flash); its tag_butt hangs
		// off the separate tag_dummy/tag_aim chain. Grip fitted by eye in the mock
		// on both spade grips at 6 and 20 degrees of depression.
		struct anchor_profile {std::string_view model, tag;vec grip;};
		// airlift: the Seaknight's door Mk19 is part of the helicopter model; its
		// tag_barrel faces the gun forward and parents the rear trigger between the
		// spade grips (handlele/handleri are the charging handles on the bolt).
		constexpr std::array<anchor_profile, 2> anchors{{
			{"weapon_minigun", "tag_aim_animated", {-14.3f, 3.2f, -.2f}},
			{"vehicle_ch46e_opened_door_interior_mk19", "tag_barrel", {-20.f, 4.5f, 4.f}},
		}};
		struct tag_cache {const void* models{};unsigned count{};std::uint32_t tag{};vec grip{};} cache;
		game::dvar_t* enabled{};
		std::array<game::dvar_t*, 3> offsets{};
		const char* reason = "not sampled";
		std::atomic_uint64_t samples{}, presented{}, unconstrained{}, inactive{};
		struct diagnostic {std::array<vec, 2> contact{}, wrist{};vec offset{};unsigned held{};} last;

		// Spade grip centre; the dvars adjust the profile grip (inches).
		vec grip_centre(const grips& g, unsigned h) noexcept
		{
			const float f = g.grip[0] + (offsets[0] ? offsets[0]->current.value : 0.f);
			const float l = g.grip[1] + (offsets[1] ? offsets[1]->current.value : 0.f);
			const float u = g.grip[2] + (offsets[2] ? offsets[2]->current.value : 0.f);
			return add(g.origin, add(scale(g.axis[0], f), add(scale(g.axis[1], h ? -l : l), scale(g.axis[2], u))));
		}

		// The anchor's script string, scanned from the model's own bone names.
		const tag_cache& anchor_tag(const native_object& object) noexcept
		{
			if (!object.models || !object.model_count) {cache = {};return cache;}
			if (cache.models == object.models && cache.count == object.model_count) return cache;
			cache = {object.models, object.model_count};
			for (const auto& a : anchors)
				for (unsigned m = 0; m < object.model_count; ++m)
				{
					const auto* model = object.models[m];
					if (!model || !model->name || !model->boneNames || a.model != model->name) continue;
					for (unsigned b = 0; b < model->numBones; ++b)
					{
						const auto* name = game::SL_ConvertToString(model->boneNames[b]);
						if (name && a.tag == name) {cache.tag = model->boneNames[b];cache.grip = a.grip;return cache;}
					}
				}
			return cache;
		}

		// Server pipeline: the server turret DObj answers gettagorigin/gettagangles
		// from a full skeleton. The client DObj's tag query (CG_DObjGetWorldTagMatrix
		// 0x14014D720 on cg_entities 0x14215D500) left uncomputed parents stale, so
		// its grips sank below the rendered spade grips as the gun pitched, and it
		// faulted at 0x140167D40 while a save load held its briefing.
		void sample()
		{
			const auto clear = [](const char* why) {reason = why;const std::lock_guard lock(mutex);published.entity = -1;};
			if (!enabled || !enabled->current.enabled) return clear("disabled");
			if (!game::CL_IsCgameInitialized() || !head_pose_bridge::get_status().enabled) return clear("no cgame or head tracking");
			if (!view_aimed::mounted() || view_aimed::active() || barrett_gun::epoch()) return clear("not on a plain turret");
			const auto* ps = game::CG_GetPredictedPlayerState(0);
			std::uint16_t entity{};
			if (!ps || !utils::native_memory::read_bytes(&entity, reinterpret_cast<const std::byte*>(ps) + 0x1e, sizeof(entity)) ||
				!entity || entity >= 2048) return clear("no turret entity");
			const auto* object = reinterpret_cast<const native_object*>(vr::h2::sp::server_entity_dobj(&game::g_entities[entity]));
			if (!object) return clear("turret has no server DObj");
			const auto& found = anchor_tag(*object);
			if (!found.tag) return clear("no grip profile for this turret model");
			const auto* name = game::SL_ConvertToString(found.tag);
			if (!name) return clear("grip anchor name unavailable");
			scripting::vector origin, angles;
			try
			{
				const scripting::entity turret{game::scr_entref_t{entity, 0}};
				origin = turret.call("gettagorigin", {std::string(name)}).as<scripting::vector>();
				angles = turret.call("gettagangles", {std::string(name)}).as<scripting::vector>();
			}
			catch (const std::exception&) {return clear("anchor tag query failed");}
			grips g{int(entity), {origin[0], origin[1], origin[2]}, {}, found.grip, clock::now()};
			for (const float x : g.origin) if (!std::isfinite(x) || std::abs(x) > 1e6f) return clear("anchor origin not finite");
			float axis[3][3]{};
			const float euler[3]{angles[0], angles[1], angles[2]};
			game::AnglesToAxis(euler, axis);
			for (unsigned r = 0; r < 3; ++r) g.axis[r] = {axis[r][0], axis[r][1], axis[r][2]};
			for (const auto& row : g.axis) for (const float x : row) if (!std::isfinite(x)) return clear("anchor axis not finite");
			reason = "grips sampled";
			++samples;
			const std::lock_guard lock(mutex);
			published = g;
		}

		grips latest() noexcept
		{
			const std::lock_guard lock(mutex);
			return published;
		}
		bool fresh(const grips& g) noexcept
		{return g.entity > 0 && clock::now() - g.at < std::chrono::milliseconds(150);}

		// Fingers wrap an upright bar (thumb up): the bar runs from pinky to index
		// along the gun's up axis and the fingers close toward the muzzle.
		anchor on_upright_bar(const bar_grip::binding& source, vec centre, const grips& g) noexcept
		{
			const auto forward = unit(g.axis[0]);
			auto across = sub(g.axis[2], scale(forward, dot(g.axis[2], forward)));
			across = unit(across);
			const auto normal = unit(cross(forward, across));
			const auto rotation = normalize(multiply(from_axis({forward, across, normal}), conjugate(source.frame_in_wrist)));
			return {sub(centre, rotate(rotation, source.centre_in_wrist)), rotation};
		}
	}

	bool active() noexcept
	{
		if (!view_aimed::mounted()) return false;
		const auto g = latest();
		return fresh(g);
	}

	void present(const interaction_rig& parts, const rig& r, const controller_input::frame& input,
		const std::array<vec, 2>& shoulders, const std::array<vec, 3>& axes, vec offset,
		std::span<bone> solved, unsigned visible) noexcept
	{
		const auto g = latest();
		if (!fresh(g) || !view_aimed::mounted()) {++inactive;return;}
		const unsigned bound = unsigned(parts.bar_grips[0].valid) | (unsigned(parts.bar_grips[1].valid) << 1);
		unsigned held{};
		for (unsigned h = 0; h < 2; ++h)
			if (input.grip[h].valid) held |= 1u << h;
		held &= visible & bound;
		if (!held) return;
		// The turret eye rides the gun (tag_player is on its aim chain), and the
		// solver origin does not follow it: a world grip less the view offset kept
		// the hands fixed in view while the gun pitched. Like the Barrett, place
		// each grip relative to that hand's world visual wrist and its tracked solve.
		std::array<anchor, 2> contacts{};
		for (unsigned h = 0; h < 2; ++h)
			if (held & (1u << h))
			{
				anchor wrist;
				const int bone = r.arms[h].wrist;
				if (!held_item::visual_wrist(input, int(h), wrist) || bone < 0 || bone >= r.count) {held &= ~(1u << h);continue;}
				contacts[h] = on_upright_bar(parts.bar_grips[h], grip_centre(g, h), g);
				contacts[h].position = add(solved[bone].position, sub(contacts[h].position, wrist.position));
			}
		if (!held) return;
		if (!bar_grip::constrain(r, solved, contacts, held, shoulders, axes)) {++unconstrained;return;}
		{
			const std::lock_guard lock(mutex);
			last.held = held;last.offset = offset;
			for (unsigned h = 0; h < 2; ++h)
			{
				last.contact[h] = add(contacts[h].position, offset);
				if (r.arms[h].wrist >= 0) last.wrist[h] = add(solved[r.arms[h].wrist].position, offset);
			}
		}
		for (unsigned h = 0; h < 2; ++h)
			if (held & (1u << h))
				empty_hand::apply(r, parts.library, native_hand_schema::definition, vr::hand(h),
					{true, empty_hand::gesture::fist, bar_grip::wrap}, solved);
		++presented;
	}

	std::string status()
	{
		const auto g = latest();
		diagnostic d;
		{const std::lock_guard lock(mutex);d = last;}
		auto out = std::format("[VR turret hands] reason={} entity={} fresh={} samples={} presented={} unconstrained={} inactive={} anchor=({:.1f},{:.1f},{:.1f}) forward=({:.2f},{:.2f},{:.2f})\n",
			reason, g.entity, fresh(g), samples.load(), presented.load(), unconstrained.load(), inactive.load(),
			g.origin[0], g.origin[1], g.origin[2], g.axis[0][0], g.axis[0][1], g.axis[0][2]);
		for (unsigned h = 0; h < 2; ++h)
			out += std::format("[VR turret hands] hand={} held={} contact=({:.1f},{:.1f},{:.1f}) wrist=({:.1f},{:.1f},{:.1f}) miss={:.2f}\n",
				h, (d.held >> h) & 1, d.contact[h][0], d.contact[h][1], d.contact[h][2], d.wrist[h][0], d.wrist[h][1], d.wrist[h][2],
				length(sub(d.contact[h], d.wrist[h])));
		return out;
	}

	class component final : public component_interface
	{
		void post_unpack() override
		{
			enabled = dvars::register_bool("vr_turretHands", true, game::DVAR_FLAG_SAVED,
				"Mounted turrets: VR hands held on the gun's rear grips");
			const char* names[3]{"vr_turretGripForward", "vr_turretGripLeft", "vr_turretGripUp"};
			for (unsigned i = 0; i < 3; ++i)
				offsets[i] = dvars::register_float(names[i], 0.f, -24.f, 24.f, game::DVAR_FLAG_SAVED,
					"Turret hand grip adjustment (forward, left, up), inches");
			scheduler::loop(sample, scheduler::pipeline::server);
			::command::add("vr_turret_hands_status", [] {console::info("%s", status().c_str());});
		}
	};
}

REGISTER_COMPONENT(vr::gameplay::turret_hands::component)
