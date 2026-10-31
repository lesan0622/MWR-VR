#include <std_include.hpp>
#include "climb_collision.hpp"
#include "mounted_hand_aim.hpp"
#include "../controller_input.hpp"
#include "../head_pose_bridge.hpp"
#include "component/scheduler.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "component/scripting.hpp"
#include "loader/component_loader.hpp"
#include "game/dvars.hpp"
#include <utils/native_memory.hpp>

// MWR aftermath: after the wakeup the player crawls prone out of the wreck.
// Grip plants a hand on the floor; pulling it back drags the body forward 1:1
// (user request), as Overlord's ending crawl pulls its carrier. Hand motion is
// read in the HMD tracking reference, so the body's own motion never feeds back.
// The stick still crawls natively.
namespace vr::gameplay::aftermath_crawl
{
	namespace
	{
		using vec = free_climb::vec;
		constexpr int player_mask = 0x2810011; // As the physical ladders (G_TraceCapsule 0x1402A5640).
		constexpr float max_speed_meters = 1.2f;
		std::array<head_pose_bridge::vector3, 2> previous{};
		unsigned held{};
		std::uint64_t generation{};
		int last_time{};
		bool running{true};
		game::dvar_t* enabled{};
		std::atomic_uint64_t pulls{}, blocked{};
		std::atomic<float> distance{};
		template <class T> T read(const void* p, size_t at)
		{T v{};if (p) utils::native_memory::read_bytes(&v, static_cast<const std::byte*>(p) + at, sizeof(v));return v;}
		bool aftermath()
		{
			const auto* d = game::Dvar_FindVar("mapname");
			return d && d->current.string && std::string_view(d->current.string) == "aftermath";
		}
		bool linked()
		{
			const auto* tag = read<const void*>(&game::g_entities[0], 0x208);
			return tag && read<const void*>(tag, 0);
		}
		void tick()
		{
			auto* ps = reinterpret_cast<game::playerState_s*>(game::g_entities[0].client);
			const auto* vr = game::Dvar_FindVar("vr_enable");
			if (!running || !enabled || !enabled->current.enabled || !vr || !vr->current.enabled || !ps ||
				!(ps->pm_flags & 1) || linked() || !aftermath()) {held = 0;return;}
			const int time = ps->commandTime;
			const float dt = std::clamp((time - last_time) * .001f, 0.f, .1f);
			if (time == last_time) return;
			last_time = time;
			const auto input = controller_input::latest_interaction();
			head_pose_bridge::spatial_frame frame;
			if (!input.focused || !head_pose_bridge::get_spatial_frame(frame) || frame.generation != input.reference_generation)
			{held = 0;return;}
			if (frame.generation != generation) {held = 0;generation = frame.generation;}
			unsigned now{};vec pull{};int count{};
			for (int h = 0; h < 2; ++h)
			{
				if (!input.grip[h].valid || !input.squeeze[h].active || !input.squeeze[h].down) continue;
				const auto p = mounted_hand_aim::local_position(frame.reference, input.grip[h].tracking.position_meters);
				now |= 1u << h;
				if (held & (1u << h))
				{
					for (int i = 0; i < 3; ++i) pull[i] += previous[h][i] - p[i];
					++count;
				}
				previous[h] = p;
			}
			held = now;
			if (!count || dt <= 0) return;
			// The planted hand stays put in the world: the body moves by what it pulled.
			vec move{};
			for (int i = 0; i < 2; ++i) for (int axis = 0; axis < 3; ++axis)
				move[axis] += pull[i] / float(count) * frame.world_yaw_axis[i][axis] * frame.units_per_meter;
			move[2] = 0;
			const float limit = max_speed_meters * frame.units_per_meter * dt, size = std::hypot(move[0], move[1]);
			if (!(size > .01f)) return;
			if (size > limit) for (auto& x : move) x *= limit / size;
			const auto bounds = read<game::Bounds>(&game::g_entities[0], 0xc0);
			for (int j = 0; j < 3; ++j)
				if (!std::isfinite(bounds.midPoint[j]) || !std::isfinite(bounds.halfSize[j]) || bounds.halfSize[j] <= 0 || bounds.halfSize[j] > 128) return;
			const vec origin{ps->origin[0], ps->origin[1], ps->origin[2]};
			const auto hit = free_climb::slide(origin, move, .003f * frame.units_per_meter, [&](vec a, vec z) {
				game::trace_t t{};game::G_TraceCapsule(&t, a.data(), z.data(), &bounds, 0, player_mask);
				return free_climb::sweep_hit{t.fraction, {t.normal[0], t.normal[1], t.normal[2]}, bool(t.startsolid), bool(t.allsolid)};
			});
			if (hit.blocked) ++blocked;
			if (hit.stuck) return;
			// Native prediction follows the server origin; no teleport bit, so no snap.
			for (int i = 0; i < 3; ++i) ps->origin[i] = hit.position[i];
			++pulls;
			distance = distance.load() + std::hypot(hit.position[0] - origin[0], hit.position[1] - origin[1]) / frame.units_per_meter;
		}
	}
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			enabled = dvars::register_bool("vr_handCrawl", true, game::DVAR_FLAG_SAVED,
				"aftermath: Grip plants a hand on the floor and pulling it drags the prone body");
			scheduler::loop(tick, scheduler::pipeline::server);
			scripting::on_level_start([] {held = 0;last_time = 0;});
			command::add("vr_crawl_status", [] {
				console::info("[VR crawl] map=%d prone=%d linked=%d held=%u pulls=%llu blocked=%llu distance=%.2fm\n",
					int(aftermath()), game::g_entities[0].client ? int(reinterpret_cast<game::playerState_s*>(game::g_entities[0].client)->pm_flags & 1) : -1,
					int(linked()), held, pulls.load(), blocked.load(), distance.load());
			});
		}
		void pre_destroy() override {running = false;}
	};
}
REGISTER_COMPONENT(vr::gameplay::aftermath_crawl::component)
