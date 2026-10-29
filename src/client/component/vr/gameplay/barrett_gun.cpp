#include <std_include.hpp>
#include "barrett_gun.hpp"
#include "fixed_sniper_policy.hpp"
#include "held_item_pose.hpp"
#include "optic_runtime.hpp"
#include "weapons/m82/poses.hpp"
#include "hands/service.hpp"
#include "hands/native_schema.hpp"
#include "hands/pose_math.hpp"
#include "../digital_button_gate.hpp"
#include "../head_pose_bridge.hpp"
#include "../engine_stereo_view.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/fastfiles.hpp"
#include "component/scene_models.hpp"
#include "component/scheduler.hpp"
#include "component/scripting.hpp"
#include "game/game.hpp"
#include "game/dvars.hpp"
#include "loader/component_loader.hpp"
#include <utils/native_memory.hpp>

// Mounted on sniperescape turret2 the script hides the turret and fires its
// simulated round from geteye() along getplayerangles() while the attack
// button is held (sniperescape_exchange::exchange_trace_converter). The barrel
// of the held M82 sets those angles; the native turret clamps them to its arcs.
namespace vr::gameplay::barrett_gun
{
	namespace
	{
		using namespace hands;
		using namespace hands::pose_math;
		using clock = controller_input::clock;
		constexpr float degrees = 57.295779513f;
		// H2 viewmodel M82 wrists/fingers (weapons/m82/poses.hpp), moved from its
		// tag_clip (5.409, 0.015, 3.619) to the MWR weapon_m82a1_sp tag_clip
		// (-5.471, 0, -1.852). Both are x-forward, z-up inch frames at tag_weapon.
		constexpr vec h2_to_mwr_clip{-10.880f, -0.015f, -5.471f};
		constexpr vec sight_on{-6.126f, 0.f, 2.271f}; // weapon_m82a1_sp tag_sight_on bind.
		constexpr float support_acquire_meters = .15f;
		game::dvar_t* enabled_dvar{};
		std::array<game::dvar_t*, 3> grip_dvars{};
		struct snapshot
		{
			std::uint64_t epoch{};
			int entity{-1};
			bool support{};
			clock::time_point at{};
		};
		std::mutex mutex;
		snapshot published;
		std::uint64_t next_epoch{};
		int last_entity{-1};
		bool support_down{};
		std::atomic<float> magnification{8.f};
		// Main owner: native assets, lens geometry from the scope's rear rim.
		struct assets
		{
			game::XModel* body{};
			game::XModel* scope{};
			std::uintptr_t reticle{};
			vec lens{}; // In weapon frame.
			float radius{}, clearance{};
		};
		std::mutex asset_mutex;
		assets loaded;
		bool searched{};
		std::array<unsigned short, 2> lighting{};
		controller_input::digital_button_gate fire{};
		std::array<controller_input::digital_press_gate, 2> leave{};
		clock::time_point zoom_at{};
		std::atomic_uint64_t commands{}, shots{}, submissions{}, lenses{}, supports{};
		std::atomic<const char*> reason{"not mounted"}, asset_reason{"not searched"};
		std::atomic<float> last_pitch{}, last_yaw{};
		bool fresh(clock::time_point at) noexcept {const auto now = clock::now();return now >= at && now - at <= 150ms;}
		template <class T> T read(const void* p, std::size_t offset)
		{T v{};std::memcpy(&v, static_cast<const std::byte*>(p) + offset, sizeof(v));return v;}
		snapshot latest() noexcept {const std::lock_guard lock(mutex);return published;}
		vec tuning() noexcept
		{
			vec v{};
			for (unsigned i = 0; i < 3; ++i) v[i] = grip_dvars[i] ? grip_dvars[i]->current.value : 0.f;
			return v;
		}
		anchor wrist_in_gun(unsigned h) noexcept
		{
			auto w = weapons::m82::wrists[h];
			w.position = add(w.position, add(h2_to_mwr_clip, tuning()));
			return w;
		}
		struct gun_pose {anchor gun{};std::array<anchor, 2> wrists{};};
		// grip_presenter: the gun takes the rear controller rotation, its rear
		// wrist on the authored grip; a support grip swings the authored span.
		gun_pose solve(const anchor& rear, const anchor& front, bool support) noexcept
		{
			const auto back = wrist_in_gun(1), fore = wrist_in_gun(0);
			const auto rotation = weapons::aimed_rotation(support ? weapons::aim_rule::two_hand : weapons::aim_rule::rear_hand,
				normalize(rear.rotation), rear.position, front.position, sub(fore.position, back.position));
			gun_pose p;
			p.gun = {sub(rear.position, rotate(rotation, back.position)), rotation};
			p.wrists = {compose(p.gun, fore), compose(p.gun, back)};
			return p;
		}
		bool world_pose(gun_pose& out, bool support) noexcept
		{
			anchor rear, front;
			if (!held_item::visual_wrist(1, rear)) return false;
			if (!held_item::visual_wrist(0, front)) {front = rear;support = false;}
			out = solve(rear, front, support);
			return true;
		}
		bool mounted_barrett(int& entity) noexcept
		{
			entity = -1;
			if (!enabled() || !game::CL_IsCgameInitialized() || !game::g_entities[0].client) return false;
			const auto* ps = reinterpret_cast<const game::playerState_s*>(game::g_entities[0].client);
			const int e = read<std::uint16_t>(ps, 0x1e);
			if (!mounted::attached(ps->e_flags) || e <= 0 || e >= 2048) return false;
			// MWR turret entity weapon token (+0x80) and turretInfo (+0x138, G_IsTurretUsable 0x1403086B0).
			const auto* ent = &game::g_entities[e];
			const auto token = read<unsigned>(ent, 0x80) & 511;
			const auto* definition = token ? game::weapon_defs[token] : nullptr;
			const char* name{};
			if (!definition || !utils::native_memory::read_bytes(&name, &definition->szInternalName, sizeof(name)) || !name ||
				strnlen_s(name, 64) >= 64 || std::string_view(name) != fixed_sniper::mwr_barrett_turret || !read<void*>(ent, 0x138))
				return false;
			entity = e;
			return true;
		}
		void update()
		{
			int entity{};
			snapshot next;
			if (!mounted_barrett(entity))
			{
				last_entity = -1;support_down = false;
				reason = enabled() ? "not mounted" : "disabled";
			}
			else
			{
				const auto input = controller_input::latest_interaction();
				if (entity != last_entity) {++next_epoch;last_entity = entity;support_down = false;}
				next.epoch = next_epoch;next.entity = entity;next.at = input.sampled_at;
				// Left grip on the handguard holds the support; release lets go.
				const bool down = input.focused && input.squeeze[0].active && input.squeeze[0].down && input.grip[0].valid;
				bool support = latest().support && down;
				if (down && !support_down && !support)
				{
					gun_pose one;anchor front;
					head_pose_bridge::spatial_frame body;
					if (world_pose(one, false) && held_item::visual_wrist(0, front) && head_pose_bridge::get_spatial_frame(body) &&
						body.units_per_meter > 0 && length(sub(front.position, one.wrists[0].position)) / body.units_per_meter < support_acquire_meters)
					{support = true;++supports;}
				}
				support_down = down;next.support = support;
				reason = "held";
			}
			// The turret eye is ~280 units below the player's skinned placement.
			engine_stereo_view::allow_far_placement(next.epoch != 0);
			const std::lock_guard lock(mutex);
			published = next;
		}
		std::string_view base_name(const char* name) noexcept
		{
			if (!name) return {};
			std::string_view n{name, strnlen_s(name, 256)};
			if (n.starts_with("mc/")) n.remove_prefix(3);
			else if (n.starts_with("m/")) n.remove_prefix(2);
			if (const auto at = n.find('@'); at != std::string_view::npos) n = n.substr(0, at);
			return n;
		}
		// Rear rim of the scope tube: the rearmost vertices ring the eyepiece.
		bool measure_lens(const game::XModel* scope, assets& out) noexcept
		{
			if (!scope || !scope->numLods || !scope->lodInfo[0].surfs) return false;
			const auto& lod = scope->lodInfo[0];
			float lo = 1e9f, hi = -1e9f;
			for (unsigned s = 0; s < lod.numsurfs; ++s)
			{
				const auto& surface = lod.surfs[s];
				for (unsigned v = 0; surface.verts0.packedVerts0 && v < surface.vertCount; ++v)
				{lo = std::min(lo, surface.verts0.packedVerts0[v].xyz[0]);hi = std::max(hi, surface.verts0.packedVerts0[v].xyz[0]);}
			}
			if (!(hi > lo) || hi - lo > 40.f) return false;
			std::array<float, 2> min{1e9f, 1e9f}, max{-1e9f, -1e9f};
			unsigned count{};
			for (unsigned s = 0; s < lod.numsurfs; ++s)
			{
				const auto& surface = lod.surfs[s];
				for (unsigned v = 0; surface.verts0.packedVerts0 && v < surface.vertCount; ++v)
				{
					const auto* p = surface.verts0.packedVerts0[v].xyz;
					if (p[0] > lo + .25f) continue;
					for (unsigned c = 0; c < 2; ++c) {min[c] = std::min(min[c], p[c + 1]);max[c] = std::max(max[c], p[c + 1]);}
					++count;
				}
			}
			if (count < 8) return false;
			const float outer = .25f * ((max[0] - min[0]) + (max[1] - min[1]));
			if (!(outer > .3f && outer < 2.5f)) return false;
			out.lens = add(sight_on, vec{lo + .1f, .5f * (min[0] + max[0]), .5f * (min[1] + max[1])});
			out.radius = outer * .8f; // Glass inside the eyecup rim.
			out.clearance = hi - lo + .4f; // Rear glass to the objective, plus 1 cm.
			return true;
		}
		void refresh()
		{
			int entity{};
			if (!mounted_barrett(entity) || searched) return;
			if (!scene_models::ready()) {asset_reason = "scene models not ready";return;}
			searched = true;
			struct context {assets found;game::Material* reticle{};} ctx;
			game::DB_EnumXAssets_Internal(game::ASSET_TYPE_XMODEL, [](game::XAssetHeader header, void* data)
			{
				auto* c = static_cast<context*>(data);
				if (!header.model || !header.model->name) return;
				const std::string_view n{header.model->name, strnlen_s(header.model->name, 128)};
				if (n == "weapon_m82a1_sp") c->found.body = header.model;
				else if (n == "attach_h1_scope_m82a1_npc") c->found.scope = header.model;
			}, &ctx, false);
			game::DB_EnumXAssets_Internal(game::ASSET_TYPE_MATERIAL, [](game::XAssetHeader header, void* data)
			{
				auto* c = static_cast<context*>(data);
				if (header.material && !c->reticle && base_name(header.material->info.name) == "mtl_wpn_h1_sni_m82a1_ret")
					c->reticle = header.material;
			}, &ctx, false);
			if (!ctx.found.body || !ctx.found.body->numLods) {asset_reason = "weapon_m82a1_sp not loaded";return;}
			ctx.found.reticle = weapons::optics::reticle_for(ctx.reticle);
			const bool lens = measure_lens(ctx.found.scope, ctx.found);
			asset_reason = !ctx.found.scope ? "scope model not loaded" : !lens ? "scope rim not measured" :
				!ctx.found.reticle ? "reticle material not loaded" : "ready";
			const std::lock_guard lock(asset_mutex);
			loaded = ctx.found;
		}
		void retire()
		{
			const std::lock_guard lock(asset_mutex);
			loaded = {};searched = false;
		}
		void submit()
		{
			const auto s = latest();
			assets a;
			{const std::lock_guard lock(asset_mutex);a = loaded;}
			gun_pose pose;
			if (!s.epoch || !fresh(s.at) || !a.body || !world_pose(pose, s.support)) return;
			const auto place = [](game::XModel* model, const anchor& at, unsigned short* light)
			{
				game::GfxScaledPlacement p{};
				p.scale = 1;
				std::copy(at.position.begin(), at.position.end(), p.base.origin);
				std::copy(at.rotation.begin(), at.rotation.end(), p.base.quat);
				float white[]{1, 1, 1, 1};
				scene_models::submit(model, &p, scene_models::no_cast_shadow, light, white, white, white, 8.f);
			};
			place(a.body, pose.gun, &lighting[0]);
			if (a.scope) place(a.scope, compose(pose.gun, {sight_on, {0, 0, 0, 1}}), &lighting[1]);
			++submissions;
			head_pose_bridge::spatial_frame body;
			if (!a.reticle || a.radius <= 0 || !head_pose_bridge::get_spatial_frame(body)) return;
			weapons::optics::view lens;
			const auto& r = pose.gun.rotation;
			lens.center = {true, compose(pose.gun, {a.lens, {0, 0, 0, 1}}).position, {}};
			lens.axis = {rotate(r, {1, 0, 0}), rotate(r, {0, 1, 0}), rotate(r, {0, 0, 1})};
			lens.radius = a.radius;
			lens.magnification = magnification.load();
			lens.reticle_image = a.reticle;
			lens.scene_clearance = a.clearance;
			lens.active = true;
			weapons::optics::present_external(lens, body.units_per_meter, body.generation, s.epoch | (1ull << 60));
			++lenses;
		}
	}
	bool enabled() noexcept {return enabled_dvar && enabled_dvar->current.enabled;}
	std::uint64_t epoch() noexcept
	{
		const auto s = latest();
		if (!s.epoch || !fresh(s.at) || !game::CL_IsCgameInitialized()) return 0;
		const auto* ps = game::CG_GetPredictedPlayerState(0);
		return ps && mounted::attached(ps->e_flags) && read<std::uint16_t>(ps, 0x1e) == s.entity ? s.epoch : 0;
	}
	bool command(const controller_input::frame& input, bool gameplay, game::usercmd_s* cmd, float* angles,
		const game::playerState_s* ps) noexcept
	{
		const auto s = latest();
		const auto now = clock::now();
		const float dt = zoom_at != clock::time_point{} ? std::clamp(std::chrono::duration<float>(now - zoom_at).count(), 0.f, .05f) : 0.f;
		zoom_at = now;
		if (!epoch() || !cmd || !angles || !ps) {fire = {};leave = {};return false;}
		cmd->rightmove = 0;
		cmd->forwardmove = 0;
		const bool live = gameplay && input.focused && fresh(input.sampled_at);
		if (live && input.turn_active && std::isfinite(input.turn[1]))
		{
			// Right stick forward/back: scope zoom, and the native turret zoom the
			// script's hint and depth of field follow.
			const float a = std::abs(input.turn[1]);
			const float stick = a > .2f ? std::copysign((std::min(a, 1.f) - .2f) / .8f, input.turn[1]) : 0.f;
			magnification = std::clamp(magnification.load() * std::exp(stick * 1.2f * dt), 4.f, 20.f);
			cmd->forwardmove = static_cast<char>(std::lround(stick * 32.f));
		}
		gun_pose pose;
		if (live && world_pose(pose, s.support))
		{
			const auto forward = rotate(pose.gun.rotation, {1, 0, 0});
			const float pitch = -std::asin(std::clamp(forward[2], -1.f, 1.f)) * degrees;
			const float yaw = std::atan2(forward[1], forward[0]) * degrees;
			if (std::isfinite(pitch) && std::isfinite(yaw))
			{
				angles[0] = std::remainder(pitch - ps->delta_angles[0], 360.f);
				angles[1] = std::remainder(yaw - ps->delta_angles[1], 360.f);
				last_pitch = pitch;last_yaw = yaw;
			}
		}
		if (live && input.grip[1].valid && fire.consume(input.trigger[1])) {cmd->buttons |= game::BUTTON_ATTACK;++shots;}
		else if (!live) fire = {};
		for (unsigned h = 0; h < 2; ++h)
			if (live && leave[h].consume(input.secondary[h])) cmd->buttons |= game::BUTTON_USE_RELOAD;
		++commands;
		return true;
	}
	void constrain_hands(std::array<anchor, 2>& targets, const std::array<quat, 2>& basis, vec, std::uint64_t reference) noexcept
	{
		const auto s = latest();
		const auto input = controller_input::latest_interaction();
		if (!epoch() || input.reference_generation != reference) return;
		const auto pose = solve(targets[1], targets[0], s.support);
		for (unsigned h = 0; h < 2; ++h)
			if (h == 1 || s.support)
				targets[h] = {pose.wrists[h].position, normalize(multiply(pose.wrists[h].rotation, conjugate(basis[h])))};
	}
	void present(const interaction_rig& parts, const rig& r, std::span<bone> solved, unsigned visible) noexcept
	{
		if (!epoch() || !parts.valid || r.count <= 0 || solved.size() < std::size_t(r.count)) return;
		const auto s = latest();
		for (unsigned h = 0; h < 2; ++h)
			if ((visible & (1u << h)) && (h == 1 || s.support))
				pose_math::fingers(r, parts.library, native_hand_schema::definition, weapons::m82::idle_fingers, int(h), solved);
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			enabled_dvar = dvars::register_bool("vr_barrettFreeGun", true, game::DVAR_FLAG_SAVED,
				"sniperescape Barrett: hold the M82 like a VR rifle with a physical scope (0: flat native scope)");
			const char* names[3]{"vr_barrettGripForward", "vr_barrettGripLeft", "vr_barrettGripUp"};
			for (unsigned i = 0; i < 3; ++i)
				grip_dvars[i] = dvars::register_float(names[i], 0.f, -12.f, 12.f, game::DVAR_FLAG_SAVED,
					"Barrett hand grips in the gun frame, inches");
			scheduler::loop(update, scheduler::pipeline::server);
			scheduler::loop(refresh, scheduler::pipeline::main, 250ms);
			scene_models::on_submit(submit);
			fastfiles::on_pre_unload(retire);
			::command::add("vr_barrett_status", []
			{
				assets a;
				{const std::lock_guard lock(asset_mutex);a = loaded;}
				const auto s = latest();
				console::info("[VR Barrett] enabled=%d reason=%s assets=%s epoch=%llu live_epoch=%llu entity=%d support=%d supports=%llu commands=%llu shots=%llu submissions=%llu lenses=%llu mag=%.1f aim=%.2f,%.2f body=%d scope=%d reticle=%d lens=(%.2f,%.2f,%.2f) r=%.2f clear=%.2f\n",
					enabled(), reason.load(), asset_reason.load(), static_cast<unsigned long long>(s.epoch),
					static_cast<unsigned long long>(epoch()), s.entity, s.support, static_cast<unsigned long long>(supports.load()),
					static_cast<unsigned long long>(commands.load()), static_cast<unsigned long long>(shots.load()),
					static_cast<unsigned long long>(submissions.load()), static_cast<unsigned long long>(lenses.load()),
					magnification.load(), last_pitch.load(), last_yaw.load(), a.body != nullptr, a.scope != nullptr, a.reticle != 0,
					a.lens[0], a.lens[1], a.lens[2], a.radius, a.clearance);
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::barrett_gun::component)
