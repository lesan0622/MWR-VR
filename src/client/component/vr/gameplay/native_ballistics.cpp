#include <std_include.hpp>
#include "../h2/entrypoints.hpp"
#include "native_scripted_control.hpp"
#include "native_ballistics.hpp"
#include "official_cheats.hpp"
#include "native_shield.hpp"
#include "muzzle_clearance.hpp"
#include "weapon_carry_runtime.hpp"
#include "../head_pose_bridge.hpp"
#include "component/scheduler_context.hpp"
#include "game/game.hpp"
#include "game/scripting/execution.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::native_ballistics
{
	namespace
	{
		bool verified{};
		thread_local bool emitting{};
		// Riding: the player is linked to a parent (vehicle, moving platform);
		// gentity tag info +0x208 -> parent, as aftermath_crawl reads it.
		bool riding(const game::gentity_s* entity)
		{
			const void* tag{};
			std::memcpy(&tag,reinterpret_cast<const std::byte*>(entity)+0x208,sizeof(tag));
			return tag && *static_cast<const void* const*>(tag);
		}
		// Server eye minus the render frame's native view origin at the last shot.
		std::atomic_uint64_t rebased_shots{},unrebased_shots{};
		std::atomic<float> last_rebase_units{};
		constexpr std::uintptr_t definitions = 0x14D750C40; // MWR (H2 0x14CE01580)
		template <std::size_t N> bool verify(std::uintptr_t address, const std::uint8_t (&bytes)[N])
		{
			std::array<std::uint8_t,N> mask{}; mask.fill(0xff);
			return static_cast<bool>(utils::hook_validation::verify_masked_bytes(
				reinterpret_cast<void*>(address), {bytes,mask.data(),N}));
		}
		bool valid(const shot_geometry& g)
		{
			muzzle_frame muzzle;
			muzzle.position=g.origin;
			muzzle.axis={g.forward,hands::scale(g.right,-1),g.up};
			return valid_geometry(muzzle);
		}
	}
	bool initialize()
	{
		if (verified) return true;
		// MWR: Bullet_Fire 0x140284C40, ADS spread 0x1404B2830, weapon type
		// 0x1404B6810, G Scr_Notify 0x1402E8C70 with scr_const weapon_fired. The
		// fire packet layout (weapon +0x3c, alternate +0x44, definition +0x48)
		// matches G_FireWeapon 0x1402EF8F0.
		constexpr std::uint8_t bullet[]{0x48,0x83,0xec,0x48,0xf3,0x0f,0x10,0x44,0x24,0x78,0x8b,0x44};
		constexpr std::uint8_t spread[]{0x48,0x83,0xec,0x28,0x45,0x0f,0xb6,0xc8,0x8b,0xc2};
		constexpr std::uint8_t notify[]{0x0f,0xbf,0x89,0x8a,0x00,0x00,0x00,0x45,0x8b,0xc8};
		constexpr std::uint8_t fire_notify_call[]{0xe8,0x56,0x92,0xff,0xff};
		verified=verify(bullet_address,bullet) && verify(ads_spread_address,spread) &&
			verify(0x1402E8C70,notify) && verify(0x1402EFA15,fire_notify_call) && native_ammunition::initialize();
		return verified;
	}
	bool firing_timing(std::uint32_t weapon,int shot_count,independent_fire::timing& out)
	{
		// Same descriptor-based mode and timing accessors as PM_Weapon_Fire,
		// independent of selected PS state and native akimbo timers.
		// MWR: fire type 0x1404B65D0 (+0x554), fire timing 0x1404B5390, burst cooldown 0x1404B4E90 (+0x560).
		constexpr std::uint8_t mode[]{0x48,0x83,0xec,0x28,0x41,0xb8,0x54,0x05,0,0};
		constexpr std::uint8_t timing[]{0x89,0x4c,0x24,0x08,0x55,0x57,0x48,0x83,0xec,0x28};
		constexpr std::uint8_t burst[]{0x48,0x83,0xec,0x28,0x41,0xb8,0x60,0x05,0,0};
		static const bool timing_ready=verify(0x1404B65D0,mode) && verify(0x1404B5390,timing) && verify(0x1404B4E90,burst);
		if (!verified || !timing_ready || !weapon || weapon>=512 || !game::weapon_defs[weapon]) return false;
		out.mode=utils::hook::invoke<int>(0x1404B65D0,weapon,false);
		utils::hook::invoke<void>(0x1404B5390,weapon,false,false,std::clamp(shot_count,0,31),&out.interval,&out.delay);
		// Native PM_Weapon (0x14069C3AA) rounds this accessor's float and stores
		// it in weaponTime without a seconds conversion. Keep attachment overrides.
		const auto pause=out.mode>=2 && out.mode<=4 ? utils::hook::invoke<float>(0x1404B4E90,weapon,false) : 0.f;
		return independent_fire::decode_native_timing(out.mode,out.interval,out.delay,pause,out);
	}
	std::string rebase_status()
	{
		return std::format("rebased_shots={} unrebased_shots={} last_rebase_units={:.1f}", rebased_shots.load(), unrebased_shots.load(), last_rebase_units.load());
	}
	result fire_owned(std::uint32_t weapon,const shot_geometry& geometry,int command_time,debit_observer settled)
	{ return fire_owned(carry::native_identity(weapon),geometry,command_time,settled); }
	result fire_owned(weapon_identity id,const shot_geometry& geometry,int command_time,debit_observer settled)
	{
		const auto weapon=id.weapon;
		result out;
		if (!verified) return out;
		out.status=outcome::invalid_context;
		if (emitting || cheats::transitioning() || !scheduler::is_executing(scheduler::pipeline::server) ||
			!game::CL_IsCgameInitialized()) return out;
		// Cover the complete transaction, including mechanical settlement and
		// the script notification. No reentrant callback may debit a second shot.
		emitting=true;
		const auto restore=gsl::finally([] {emitting=false;});
		auto* entity=&game::g_entities[0];
		auto* ps=reinterpret_cast<game::playerState_s*>(entity->client);
		if (!ps || ps->commandTime!=command_time || (ps->e_flags & 0x103000) || !scripted_control::firing_allowed(ps)) return out;
		const auto* paused=game::Dvar_FindVar("cl_paused");
		if (!paused || paused->current.integer || *game::keyCatchers) return out;
		out.status=outcome::invalid_geometry;
		if (!valid(geometry)) return out;
		auto shot=geometry;
		out.status=outcome::unsupported_weapon;
		const auto owned=native_ammunition::observe_carried(ps,id);
		if (!owned.valid || vr::h2::sp::weapon_type(weapon,false)!=1) return out;
		out.before=owned;
		out.status=outcome::empty;
		if (out.before.loaded<=0) return out;
		hands::vec eye{ps->origin[0],ps->origin[1],ps->origin[2]+ps->viewHeightCurrent};
		out.status=outcome::invalid_geometry;
		for (float x : eye) if (!std::isfinite(x)) return out;
		// The muzzle is a render-frame world pose. Keep it fixed relative to the
		// native view origin that frame used and carry it onto the server eye: in
		// a moving vehicle (jeepride) the rendered view trails the server by
		// speed x latency, so shots left from a stale point and passed behind
		// riders alongside (user report). MWR addition, not in Overlord: applied
		// only while riding (user request); scripted views far from the eye keep
		// the unshifted muzzle.
		if (!riding(entity)) {}
		else if (head_pose_bridge::spatial_frame frame{}; head_pose_bridge::get_spatial_frame(frame) && frame.has_native_origin)
		{
			const hands::vec native{frame.native_origin[0],frame.native_origin[1],frame.native_origin[2]};
			const auto shift=hands::sub(eye,native);
			const float distance=hands::length(shift);
			if (std::isfinite(distance) && distance<=300.f) {shot.origin=hands::add(shot.origin,shift);last_rebase_units=distance;++rebased_shots;}
			else ++unrebased_shots;
		}
		else ++unrebased_shots;
		if (hands::length(hands::sub(eye,shot.origin))>200.f) return out;
		out.status=outcome::obstruction;
		if(!shield::firing_clear(id,shot.origin))return out;
		game::trace_t sweep{};
		game::Bounds point{};
		game::G_TraceCapsule(&sweep,eye.data(),shot.origin.data(),&point,entity->s.entityNum,muzzle_clearance_mask);
		if (!std::isfinite(sweep.fraction) || sweep.fraction<1 || sweep.allsolid || sweep.startsolid) return out;
		parameters packet{};
		packet.shot=shot;
		packet.weapon=weapon;
		packet.definition=reinterpret_cast<void* const*>(definitions)[weapon];
		const auto spread=utils::hook::invoke<float>(ads_spread_address,ps,weapon,false,0);
		out.status=outcome::unsupported_weapon;
		if (!packet.definition || !std::isfinite(spread) || spread<0 || spread>90) return out;
		// Freeze the ACTUAL firing weapon, including its native variant name.
		// Prepare script references/arguments before the irreversible ammo debit;
		// native selection can belong to the other hand and must not be borrowed.
		std::array<char,1024> fired_name{};
		game::Weapon native_weapon{};
		native_weapon.data=weapon; // Preserve the full token; do not initialize SDK bitfields.
		game::BG_GetWeaponNameComplete(native_weapon,false,fired_name.data(),static_cast<unsigned>(fired_name.size()));
		const auto name_size=strnlen_s(fired_name.data(),fired_name.size());
		if (!name_size || name_size==fired_name.size()) return out;
		const auto timeline=native_ammunition::timeline();
		const auto previous=native_ammunition::projected_identity(weapon);
		if (id && !native_ammunition::project(id)) return out;
		const auto restore_projection=gsl::finally([&]{if (previous) native_ammunition::project(previous);});
		out.status=outcome::ammunition_changed;
		const bool sustained=cheats::sustain_ammo();
		auto debited=out.before;debited.loaded-=int(!sustained);
		if (entity->client!=reinterpret_cast<game::gclient_s*>(ps) || ps->commandTime!=command_time || !scripted_control::firing_allowed(ps) ||
			!native_ammunition::commit_carried(out.before,debited.loaded,out.before.reserve)) return out;
		// Settle the existing mechanical owner before ballistics can run damage
		// or script callbacks. The observer never writes native ammunition.
		if (settled && !settled(out.before,debited,sustained)) {out.status=outcome::mechanical_changed;return out;}
		// Restore the event skipped by direct bullet dispatch. Existing notify
		// isolates the VM stack and preserves native GSC/Lua notification routing.
		// Never refund/retry after this boundary: scripts may have observed it.
		// MWR: the same native notify G_FireWeapon sends (no GSC VM wrapper).
		game::Scr_AddString(fired_name.data());
		utils::hook::invoke<void>(0x1402E8C70,entity,*reinterpret_cast<const unsigned*>(0x146663898),1);
		out.notified=true;
		if (entity->client!=reinterpret_cast<game::gclient_s*>(ps) ||
			native_ammunition::timeline()!=timeline || !game::CL_IsCgameInitialized() || ps->commandTime!=command_time)
		{out.status=outcome::context_changed;return out;}
		utils::hook::invoke<void>(bullet_address,entity,spread,&packet,entity,command_time,-1.f);
		out.status=outcome::emitted;
		if (entity->client==reinterpret_cast<game::gclient_s*>(ps))
			out.after=native_ammunition::observe_carried(ps,id);
		return out;
	}
}
