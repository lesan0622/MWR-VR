#include <std_include.hpp>
#include "designator_visual.hpp"
#include "weapon_laser.hpp"
#include "mounted_turret_laser.hpp"
#include "weapon_interaction.hpp"
#include "weapon_carry_runtime.hpp"
#include "weapon_render_pose.hpp"
#include "weapon_render_owner.hpp"
#include "native_scripted_control.hpp"
#include "render_origin_rebase.hpp"
#include <utils/native_memory.hpp>
#include "../eye_composition.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scripting.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapon_laser
{
	namespace
	{
		using namespace hands;
		namespace designator_visual = equipment::special::designator_visual;
		struct orientation
		{
			vec origin;
			std::array<vec, 3> axis;
		};
		static_assert(sizeof(orientation) == 48);
		struct sample
		{
			weapons::muzzle_frame pose;
			designator_visual::surface surface;
			controller_input::clock::time_point at;
			spatial_panel::vec4 color{1, .002f, .001f, 1};
			bool receiver_tag{}, nightvision_only{};
		};
		std::mutex mutex;
		std::array<sample, 16> samples{};
		unsigned cursor{};
		struct mounted_sample
		{
			mounted::laser_ray ray;
			designator_visual::surface surface;
		};
		std::array<mounted_sample,16> mounted_samples{};
		unsigned mounted_cursor{};
		utils::hook::detour draw_hook;
		bool installed{};
		std::atomic_uint64_t traced{}, suppressed{}, pairs{}, drawn{}, missed{};
		std::atomic_uint64_t mounted_traced{},mounted_pairs{};
		struct pair_snapshot
		{
			std::uint64_t id{}, publication{}, device{};
			std::array<world_beam::projected, 2> eyes;
			bool valid{};
		};
		thread_local pair_snapshot pair;
		// MWR bring-up: the last projected beam (world start/end, centre eye, color).
		struct beam_diagnostic {vec start{},end{},eye{},color{};bool hit{};};
		std::mutex diagnostic_mutex;
		beam_diagnostic last_beam;
		thread_local world_beam::renderer renderer;
		bool trace_surface(vec origin,vec forward,float reach,unsigned short skip,int mask,
			designator_visual::surface& surface)
		{
			const auto end=add(origin,scale(forward,reach));
			game::trace_t hit{};game::Bounds point{};unsigned trace_flags{};
			// MWR CG_Trace (H2 0x1403C70D0): same (trace, start, end, bounds, skip, mask, flags*) call.
			utils::hook::invoke<void>(0x1401A0380,&hit,origin.data(),end.data(),&point,skip,mask,&trace_flags);
			if(hit.startsolid || hit.allsolid || !std::isfinite(hit.fraction) || hit.fraction<0 || hit.fraction>1)return false;
			const vec normal{hit.normal[0],hit.normal[1],hit.normal[2]};const bool contact=hit.fraction<1;
			if(contact && (!std::isfinite(length(normal)) || std::abs(length(normal)-1)>.01f))return false;
			surface={add(origin,scale(forward,reach*hit.fraction)),contact?normal:vec{0,0,1},reach,contact};
			return true;
		}
		bool project(const eye_composition::event& event,vec start,vec forward,const designator_visual::surface& surface,
			float units,spatial_panel::vec4 color)
		{
			vec end;if(!designator_visual::endpoint(start,forward,surface,units,end))return false;
			const auto eye=scale(add(event.model_origins.eyes[0],event.model_origins.eyes[1]),.5f);
			{
				const std::lock_guard lock(diagnostic_mutex);
				last_beam={start,end,eye,{color[0],color[1],color[2]},surface.hit};
			}
			const auto world=designator_visual::geometry(start,end,surface.normal,eye,units);
			for(unsigned i=0;i<2;++i)
			{
				spatial_panel::matrix vp;
				std::memcpy(vp.data(),event.views.eyes[i].bytes.data()+engine_stereo_view::h2_current_view_projection_offset,sizeof(vp));
				pair.eyes[i]=designator_visual::project(world,event.model_origins.eyes[i],vp,surface.hit);
				pair.eyes[i].color=color;
			}
			pair.valid=true;++pairs;return true;
		}
		// MWR (H1) LaserDef: color and its HDR scale are separate vec4s, so range and
		// the flags move (0x140159560 reads +0x28 tag, +0x2C*+0x3C color, +0x6C range,
		// +0x91 nightvisionOnly, +0x95 laserSightLaser).
		struct mwr_laser
		{
			game::scr_string_t tag{};
			std::array<float, 3> color{};
			float range{};
			bool nightvision_only{}, sight_laser{};
		};
		bool read_laser(const void* definition, mwr_laser& out) noexcept
		{
			std::array<std::byte, 0x98> raw{};
			if (!definition || !utils::native_memory::read_bytes(raw.data(), definition, raw.size())) return false;
			const auto f = [&](std::size_t o) {float v{};std::memcpy(&v, raw.data() + o, sizeof(v));return v;};
			std::memcpy(&out.tag, raw.data() + 0x28, sizeof(out.tag));
			for (unsigned c = 0; c < 3; ++c) out.color[c] = f(0x2c + c * 4) * f(0x3c + c * 4);
			out.range = f(0x6c);
			out.nightvision_only = raw[0x91] != std::byte{};
			out.sight_laser = raw[0x95] != std::byte{};
			return true;
		}
		void draw(int client,
		          const void* context,
		          const void* entity,
		          const orientation* input,
		          void* effects,
		          int flags,
		          game::LaserDef* definition,
		          float range)
		{
			const auto original = [&]
			{ draw_hook.invoke<void>(client, context, entity, input, effects, flags, definition, range); };
			if (client != 0 || !entity || !weapons::carry::active())
			{
				original();
				return;
			}
			unsigned short owner{};
			// MWR centity +0x1B2: entity number, also the native trace skip (H2 +0x1B4 owner).
			if (!utils::native_memory::read_bytes(
			        &owner, static_cast<const std::byte*>(entity) + 0x1b2, sizeof(owner)) ||
			    owner != 0)
			{
				original();
				return;
			}
			// MWR: the held native weapon is the laser's owner (H2 read ps+0x3BC).
			const auto held = weapons::sample_native_equipped();
			const unsigned token = unsigned(held.weapon) & 511;
			const auto* def = token ? game::weapon_defs[token] : nullptr;
			if (!token || !def || !def->szInternalName)
			{
				original();
				return;
			}
			const bool designator = std::string_view(def->szInternalName) == "usp_laserdesignator";
			auto pose = weapons::current_muzzle();
			const auto tracking = controller_input::latest();
			const auto now = controller_input::clock::now();
			if (!scripted_control::predicted_allowed())
			{
				if (designator)
					++suppressed;
				else
					original();
				return;
			}
			mwr_laser laser{};
			if (!weapons::pose_ready(pose, held, tracking.reference_generation, now) || !read_laser(definition, laser))
			{
				++suppressed;
				return;
			}
			bool receiver_tag = !designator;
			if (designator && (pose.profile_id != "laserdesignator" || !held.can_fire()))
			{
				++suppressed;
				return;
			}
			if (!designator && laser.tag)
			{
				const auto* tag =
				    unsigned(laser.tag) < 0x40000 ? game::SL_ConvertToString(laser.tag) : nullptr;
				if (tag && std::string_view(tag) == "tag_flash")
					receiver_tag = false;
				else if (!tag || std::string_view(tag) != "tag_laser")
				{
					++suppressed;
					return;
				}
			}
			// MWR: a receiver without a solved tag_laser keeps the muzzle axis.
			if (receiver_tag && !weapons::laser_pose(pose, pose))
				receiver_tag = false;
			spatial_panel::vec4 color{1, .002f, .001f, 1};
			if (!designator)
				for (unsigned c = 0; c < 3; ++c)
				{
					const auto value = laser.color[c];
					if (!std::isfinite(value) || value < 0 || value > 64)
					{
						++suppressed;
						return;
					}
					color[c] = value;
				}
			// MWR IR lasers (PEQ-6, nightvisionOnly) are dim grey (0.2) for the native
			// scene, which the NVG post effect then brightens. This beam composes after
			// it, so present the intensified look directly.
			// The bog_a PEQ-6 does not set nightvisionOnly; use the native NVG state
			// (0x14018AE50(local client), the same query the laser callers make).
			if (!designator && (laser.nightvision_only || utils::hook::invoke<bool>(0x14018AE50, 0)))
				color = {.75f, 1.f, .75f, 1};
			const float reach =
			    laser.sight_laser && std::isfinite(range) && range > 0 ? range : laser.range;
			if (!std::isfinite(reach) || reach <= 0 || reach > 20000)
			{
				++suppressed;
				return;
			}
			// Exact native client query on its original frontend thread. No engine calls in eye composition.
			designator_visual::surface surface;
			if (!trace_surface(pose.position,pose.axis[0],reach,0,context?0x280e821:0x280e861,surface))
			{
				++suppressed;
				return;
			}
			const sample value{pose,
			                   surface,
			                   now,
			                   color,
			                   receiver_tag,
			                   laser.nightvision_only};
			{
				const std::lock_guard lock(mutex);
				samples[cursor++ % samples.size()] = value;
			}
			++traced;
			// Replace the early beam and its post_light with a scene-bound depth-tested draw.
		}
		void prepare(const eye_composition::event& event)
		{
			pair = {};
			pair.id = event.pair_id;
			pair.publication = event.views.eyes[0].publication;
			pair.device = event.device_generation;
			if(!event.model_origins.valid)return;
			mounted_sample turret;
			const auto now = controller_input::clock::now();
			{
				const std::lock_guard lock(mutex);
				for(const auto& value:mounted_samples)
					if(mounted::laser_matches_scene(value.ray,event.views,now) && value.ray.at>turret.ray.at)turret=value;
			}
			if(turret.ray.instance && mounted::laser_current(turret.ray.instance,turret.ray.reference))
			{
				if(project(event,turret.ray.origin,turret.ray.forward,turret.surface,turret.ray.units,{1,.002f,.001f,1}))++mounted_pairs;
				return;
			}
			if(!weapons::carry::active())return;
			const auto held = weapons::carry::current_hold();
			if (!valid_hand(held.holding_hand()))
				return;
			// MWR: the H2 render-pose hooks are not rebound. Use the pose traced at
			// the native laser draw (already on tag_laser), placed like the gun:
			// skinned placement plus the arm placement lag (as optics do).
			sample trace;
			{
				const std::lock_guard lock(mutex);
				for (const auto& value : samples)
					if (weapons::same_render_carrier(value.pose.owner, held) && now >= value.at &&
					    now - value.at < 150ms && value.at > trace.at)
						trace = value;
			}
			if (!trace.pose.valid || !weapons::valid_model_anchor(trace.pose.model))
				return;
			// Native draws (and so samples) only while a night-vision-only laser is visible.
			const auto& pose = trace.pose;
			auto placement = event.model_origins.placement;
			const auto lag = render_origin_rebase::solve_lag();
			for (int i = 0; i < 3; ++i) placement[i] += lag[i];
			const auto start = weapons::place_model_anchor(pose.model, placement);
			project(event,start,pose.axis[0],trace.surface,pose.units_per_meter,trace.color);
		}
		void compose(const eye_composition::event& event,
		             ID3D11DeviceContext* context,
		             ID3D11ShaderResourceView*,
		             ID3D11RenderTargetView* target) noexcept
		{
			if (!installed || event.eye > 1)
				return;
			if (pair.id != event.pair_id || pair.publication != event.views.eyes[0].publication ||
			    pair.device != event.device_generation)
				prepare(event);
			if (!pair.valid)
				return;
			if (renderer.draw(
			        context, target, event.scene_depth, pair.eyes[event.eye], event.width, event.height))
				++drawn;
			else
				++missed;
		}
	}
	void prepare_mounted() noexcept
	{
		if(!installed)return;
		mounted::laser_ray ray;if(!mounted::prepare_laser(ray))return;
		// Same native frontend trace and shared world-depth renderer as carried
		// lasers. The muzzle is outside the gun; ignore the owning turret itself.
		designator_visual::surface surface;
		if(!trace_surface(ray.origin,ray.forward,20000.f,static_cast<unsigned short>(ray.entity),0x280e861,surface))
		{++suppressed;return;}
		const std::lock_guard lock(mutex);
		mounted_samples[mounted_cursor++%mounted_samples.size()]={ray,surface};++mounted_traced;
	}
	class component final : public component_interface
	{
		eye_composition::consumer_registration composition_registration;
		void post_unpack() override
		{
			constexpr std::array<unsigned char, 16> bytes{0x4c,
			                                              0x8b,
			                                              0xdc,
			                                              0x55,
			                                              0x53,
			                                              0x56,
			                                              0x57,
			                                              0x41,
			                                              0x57,
			                                              0x49,
			                                              0x8d,
			                                              0xab,
			                                              0xf8,
			                                              0xfe,
			                                              0xff,
			                                              0xff};
			std::array<unsigned char, 16> mask;
			mask.fill(255);
			constexpr std::array<unsigned char, 4> trace{0x48, 0x83, 0xec, 0x68},
			    trace_mask{255, 255, 255, 255};
			if (utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x140159560), // MWR laser draw (H2 0x14037D470)
			                                                {bytes.data(), mask.data(), bytes.size()}) &&
			    utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(0x1401A0380),
			                                                {trace.data(), trace_mask.data(), trace.size()}))
			{
				draw_hook.create(0x140159560, draw);
				installed = true;
			}
			composition_registration =
			    eye_composition::register_consumer(compose, eye_composition::layer::world_equipment);
			scripting::on_shutdown(
			    [](bool, bool after)
			    {
				    if (!after)
				    {
					    const std::lock_guard lock(mutex);
					    samples = {};
					    cursor = 0;
					    mounted_samples = {};
					    mounted_cursor = 0;
				    }
			    });
			const auto status = []
			{
				console::info(
				    "[VR weapon laser] installed=%d traces=%llu mounted_traces=%llu mounted_pairs=%llu suppressed=%llu scene_pairs=%llu eye_draws=%llu depth_failures=%llu\n",
				    installed,
				    traced.load(),
				    mounted_traced.load(),
				    mounted_pairs.load(),
				    suppressed.load(),
				    pairs.load(),
				    drawn.load(),
				    missed.load());
				beam_diagnostic b;
				{const std::lock_guard lock(diagnostic_mutex);b=last_beam;}
				console::info("[VR weapon laser] last start=(%.1f,%.1f,%.1f) end=(%.1f,%.1f,%.1f) eye=(%.1f,%.1f,%.1f) color=(%.3f,%.3f,%.3f) hit=%d\n",
					b.start[0],b.start[1],b.start[2],b.end[0],b.end[1],b.end[2],b.eye[0],b.eye[1],b.eye[2],b.color[0],b.color[1],b.color[2],b.hit);
			};
			command::add("vr_weapon_laser_status", status);
			command::add("vr_designator_beam_status", status);
		}
		void pre_destroy() override
		{
			composition_registration.reset();
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapon_laser::component)
