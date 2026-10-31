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
// of the bipod-rested M82 sets those angles, already inside the turret arcs.
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
			bool support{}, gripping{};
			clock::time_point at{};
		};
		std::mutex mutex;
		snapshot published;
		std::uint64_t next_epoch{};
		int last_entity{-1};
		bool support_down{}, grip_down{};
		controller_input::clock::time_point mounted_at{};
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
		// Bipod: the gun turns about its tag_bipods on a fixed rest point, inside
		// the turret's own arcs, so the scope never shows where the round cannot go.
		constexpr vec bipod_tag{6.992f, 0.f, -1.287f}; // weapon_m82a1_sp bind.
		constexpr vec measured_lens{-11.12f, 0.f, 3.58f}; // measure_lens on attach_h1_scope_m82a1_npc.
		constexpr float eye_to_lens = 3.f; // Rest pose: scope 3 in ahead of the native eye.
		constexpr float grip_acquire_meters = .2f;
		struct rest
		{
			std::uint64_t epoch{};
			vec pivot{};
			float base_yaw{};
			std::array<float, 2> pitch{}, yaw{}; // Relative arcs: turretInfo +0x0C/+0x14, +0x10/+0x18.
			std::array<float, 2> aim{}; // Native view pitch/yaw in degrees.
			std::array<float, 2> grab{}; // Aim minus the hand's raw aim when the grip took the gun.
			bool valid{};
		};
		struct eye_sample {std::uint64_t epoch{};vec eye{}, current{};}; // First and latest native eye of the mount.
		rest bipod;           // Guarded by mutex.
		eye_sample native_eye; // Guarded by mutex.
		quat view_rotation(float pitch, float yaw) noexcept
		{
			const float p = pitch * .5f / degrees, y = yaw * .5f / degrees;
			return normalize(multiply(quat{0, 0, std::sin(y), std::cos(y)}, quat{0, std::sin(p), 0, std::cos(p)}));
		}
		std::array<float, 2> clamp_aim(const rest& b, std::array<float, 2> aim) noexcept
		{
			const float relative = std::clamp(std::remainder(aim[1] - b.base_yaw, 360.f), b.yaw[0], b.yaw[1]);
			return {std::clamp(aim[0], b.pitch[0], b.pitch[1]), std::remainder(b.base_yaw + relative, 360.f)};
		}
		gun_pose rested(const rest& b, std::array<float, 2> aim) noexcept
		{
			gun_pose p;
			p.gun.rotation = view_rotation(aim[0], aim[1]);
			p.gun.position = sub(b.pivot, rotate(p.gun.rotation, bipod_tag));
			p.wrists = {compose(p.gun, wrist_in_gun(0)), compose(p.gun, wrist_in_gun(1))};
			return p;
		}
		// The rear hand steers the butt: point the grip-to-bipod span from the hand at the rest.
		bool aim_from_hand(const rest& b, vec hand, std::array<float, 2>& aim) noexcept
		{
			const auto local = sub(bipod_tag, wrist_in_gun(1).position);
			const auto world = sub(b.pivot, hand);
			if (!(length(world) > 4.f) || !std::isfinite(length(world))) return false;
			const float yaw = (std::atan2(world[1], world[0]) - std::atan2(local[1], local[0])) * degrees;
			const float pitch = -(std::atan2(world[2], std::hypot(world[0], world[1])) -
				std::atan2(local[2], std::hypot(local[0], local[1]))) * degrees;
			if (!std::isfinite(pitch) || !std::isfinite(yaw)) return false;
			aim = {pitch, yaw}; // Raw: the grab offset is added before the arc clamp.
			return true;
		}
		// Any thread: the gripping hand steers (and stores) the aim; otherwise the
		// gun stays where it was left.
		// The native turret eye orbits the turret pivot as the view turns, and the
		// HMD camera and hands ride on it. A world-fixed rest would feed that orbit
		// back into the hand-steered aim (runaway to the arc limits), so the rest
		// moves with the eye: rigid in the player's frame, like the gun on the turret.
		rest riding() noexcept
		{
			const std::lock_guard lock(mutex);
			auto b = bipod;
			if (native_eye.epoch == b.epoch) b.pivot = add(b.pivot, sub(native_eye.current, native_eye.eye));
			return b;
		}
		bool current_pose(gun_pose& out, const snapshot& s, const vec* hand = nullptr, std::array<float, 2>* aimed = nullptr) noexcept
		{
			const auto b = riding();
			if (!b.valid || !s.epoch || b.epoch != s.epoch) return false;
			auto aim = b.aim;
			anchor wrist;
			const bool steering = s.gripping && (hand || held_item::visual_wrist(1, wrist));
			std::array<float, 2> raw{};
			if (steering && aim_from_hand(b, hand ? *hand : wrist.position, raw))
			{
				// Relative to the grab: taking the gun never makes it jump.
				aim = clamp_aim(b, {raw[0] + b.grab[0], raw[1] + b.grab[1]});
				const std::lock_guard lock(mutex);
				if (bipod.epoch == b.epoch) bipod.aim = aim;
			}
			out = rested(b, aim);
			if (aimed) *aimed = aim;
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
		// The rest point, once per mount: the scope eyepiece just ahead of the native
		// turret eye at the arc centre, so leaning in finds the scope.
		bool place_rest(int entity, std::uint64_t epoch, rest& out) noexcept
		{
			eye_sample eye;
			{const std::lock_guard lock(mutex);eye = native_eye;}
			const auto* ent = &game::g_entities[entity];
			const auto* info = read<const std::byte*>(ent, 0x138);
			if (eye.epoch != epoch || !info) return false;
			std::array<float, 4> arcs{};
			if (!utils::native_memory::read_bytes(arcs.data(), info + 0xc, sizeof(arcs))) return false;
			out = {};
			out.epoch = epoch;
			out.base_yaw = read<float>(ent, 0x104); // Entity angles +0x100 (turret_probe).
			out.pitch = {std::min(arcs[0], arcs[2]), std::max(arcs[0], arcs[2])};
			out.yaw = {std::min(arcs[1], arcs[3]), std::max(arcs[1], arcs[3])};
			for (float x : {out.base_yaw, arcs[0], arcs[1], arcs[2], arcs[3]})
				if (!std::isfinite(x) || std::abs(x) > 360.f) return false;
			out.aim = clamp_aim(out, {.5f * (out.pitch[0] + out.pitch[1]), out.base_yaw + .5f * (out.yaw[0] + out.yaw[1])});
			assets a;
			{const std::lock_guard lock(asset_mutex);a = loaded;}
			const auto lens = a.radius > 0 ? a.lens : measured_lens;
			const auto rotation = view_rotation(out.aim[0], out.aim[1]);
			const auto eyepiece = add(eye.eye, rotate(rotation, {eye_to_lens, 0, 0}));
			out.pivot = sub(eyepiece, rotate(rotation, sub(lens, bipod_tag)));
			out.valid = true;
			return true;
		}
		void update()
		{
			int entity{};
			snapshot next;
			const auto now = clock::now();
			if (!mounted_barrett(entity))
			{
				// A server frame without the mount flag (seen while gripping in a
				// mock) must not start a new mount: hold the rest for 0.5 s.
				if (last_entity > 0 && now - mounted_at < 500ms && enabled())
				{
					next = latest();
					next.at = controller_input::latest_interaction().sampled_at;
					const std::lock_guard lock(mutex);
					published = next;
					return;
				}
				last_entity = -1;support_down = grip_down = false;
				reason = enabled() ? "not mounted" : "disabled";
			}
			else
			{
				mounted_at = now;
				const auto input = controller_input::latest_interaction();
				if (entity != last_entity) {++next_epoch;last_entity = entity;support_down = grip_down = false;}
				next.epoch = next_epoch;next.entity = entity;next.at = input.sampled_at;
				{
					rest b;
					{const std::lock_guard lock(mutex);b = bipod;}
					if (b.epoch != next.epoch || !b.valid)
					{
						rest placed;
						if (place_rest(entity, next.epoch, placed)) {const std::lock_guard lock(mutex);bipod = placed;}
					}
				}
				const auto previous = latest();
				gun_pose at;
				const bool rested_now = current_pose(at, {next.epoch, entity, false, false, next.at});
				head_pose_bridge::spatial_frame body;
				const bool frame = head_pose_bridge::get_spatial_frame(body) && body.units_per_meter > 0;
				const auto close_to = [&](int h, const anchor& target)
				{
					anchor hand;
					return rested_now && frame && held_item::visual_wrist(h, hand) &&
						length(sub(hand.position, target.position)) / body.units_per_meter <
							(h ? grip_acquire_meters : support_acquire_meters);
				};
				// Right grip takes the gun on its pistol grip; the left the handguard.
				const bool grip = input.focused && input.squeeze[1].active && input.squeeze[1].down && input.grip[1].valid;
				bool gripping = previous.epoch == next.epoch && previous.gripping && grip;
				if (grip && !grip_down && !gripping && close_to(1, at.wrists[1]))
				{
					anchor hand;std::array<float, 2> raw{};
					const auto ridden = riding();
					const std::lock_guard lock(mutex);
					if (held_item::visual_wrist(1, hand) && aim_from_hand(ridden, hand.position, raw))
					{
						bipod.grab = {bipod.aim[0] - raw[0], std::remainder(bipod.aim[1] - raw[1], 360.f)};
						gripping = true;
					}
				}
				grip_down = grip;next.gripping = gripping;
				const bool down = input.focused && input.squeeze[0].active && input.squeeze[0].down && input.grip[0].valid;
				bool support = previous.epoch == next.epoch && previous.support && down;
				if (down && !support_down && !support && close_to(0, at.wrists[0])) {support = true;++supports;}
				support_down = down;next.support = support;
				reason = !rested_now ? "waiting for the native eye" : gripping ? "held on bipod" : "rested on bipod";
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
			if (!s.epoch || !fresh(s.at) || !a.body || !current_pose(pose, s)) return;
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
		// The rested barrel (clamped to the turret arcs) is the native view.
		gun_pose pose;std::array<float, 2> aim{};
		if (current_pose(pose, s, nullptr, &aim))
		{
			const float pitch = aim[0], yaw = aim[1];
			if (std::isfinite(pitch) && std::isfinite(yaw))
			{
				angles[0] = std::remainder(pitch - ps->delta_angles[0], 360.f);
				angles[1] = std::remainder(yaw - ps->delta_angles[1], 360.f);
				last_pitch = pitch;last_yaw = yaw;
			}
		}
		// The gate tracks the trigger while released too, so a press made before
		// the grab never fires on it.
		const bool pulled = live && fire.consume(input.trigger[1]);
		if (pulled && s.gripping && input.grip[1].valid) {cmd->buttons |= game::BUTTON_ATTACK;++shots;}
		else if (!live) fire = {};
		for (unsigned h = 0; h < 2; ++h)
			if (live && leave[h].consume(input.secondary[h])) cmd->buttons |= game::BUTTON_USE_RELOAD;
		++commands;
		return true;
	}
	void constrain_hands(std::array<anchor, 2>& targets, const std::array<quat, 2>& basis, vec, std::uint64_t reference) noexcept
	{
		auto s = latest();
		const auto input = controller_input::latest_interaction();
		if (!epoch() || input.reference_generation != reference) return;
		// The aim is steered only from the world visual wrist (server, command and
		// scene submission). On the turret the solver origin is not the camera,
		// so place the grips relative to that same visual wrist, not via the offset.
		anchor wrist;
		if (!held_item::visual_wrist(1, wrist)) return;
		const bool gripping = s.gripping;
		s.gripping = false;
		gun_pose pose;
		if (!current_pose(pose, s)) return;
		const auto tracked = targets[1].position; // The right visual wrist in solver space.
		for (unsigned h = 0; h < 2; ++h)
			if (h == 1 ? gripping : s.support)
				targets[h] = {add(tracked, sub(pose.wrists[h].position, wrist.position)),
					normalize(multiply(pose.wrists[h].rotation, conjugate(basis[h])))};
	}
	void observe_native_eye(const float* origin) noexcept
	{
		const auto s = latest();
		if (!origin || !s.epoch) return;
		const vec eye{origin[0], origin[1], origin[2]};
		for (float x : eye) if (!std::isfinite(x) || std::abs(x) > 1e7f) return;
		const std::lock_guard lock(mutex);
		if (native_eye.epoch != s.epoch) native_eye = {s.epoch, eye, eye};
		else native_eye.current = eye;
	}
	void present(const interaction_rig& parts, const rig& r, std::span<bone> solved, unsigned visible) noexcept
	{
		if (!epoch() || !parts.valid || r.count <= 0 || solved.size() < std::size_t(r.count)) return;
		const auto s = latest();
		for (unsigned h = 0; h < 2; ++h)
			if ((visible & (1u << h)) && (h == 1 ? s.gripping : s.support))
				pose_math::fingers(r, parts.library, native_hand_schema::definition, weapons::m82::idle_fingers, int(h), solved);
	}
	class component final : public component_interface
	{
		void post_unpack() override
		{
			enabled_dvar = dvars::register_bool("vr_barrettFreeGun", true, game::DVAR_FLAG_SAVED,
				"sniperescape Barrett: the M82 rests on its bipod, aimed by hand, with a physical scope (0: flat native scope)");
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
				rest b;eye_sample eye;
				{const std::lock_guard lock(mutex);b = bipod;eye = native_eye;}
				console::info("[VR Barrett] bipod valid=%d gripping=%d pivot=(%.1f,%.1f,%.1f) native_eye=(%.1f,%.1f,%.1f) base_yaw=%.1f pitch=%.1f..%.1f yaw=%.1f..%.1f aim=%.2f,%.2f\n",
					b.valid, s.gripping, b.pivot[0], b.pivot[1], b.pivot[2], eye.current[0], eye.current[1], eye.current[2],
					b.base_yaw, b.pitch[0], b.pitch[1], b.yaw[0], b.yaw[1], b.aim[0], b.aim[1]);
			});
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::barrett_gun::component)
