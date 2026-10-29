#include <std_include.hpp>
#include "launcher_runtime.hpp"
#include "launcher_targeting.hpp"
#include "launcher_aim_bridge.hpp"
#include "javelin_screen.hpp"
#include "weapon_carry_runtime.hpp"
#include "native_scripted_control.hpp"
#include "component/console.hpp"
#include "game/game.hpp"
#include "loader/component_loader.hpp"
#include <utils/hook.hpp>
#include <utils/hook_validation.hpp>

namespace vr::gameplay::weapons::launcher
{
	namespace
	{
		std::atomic_uint64_t projected{},javelin_calls{},javelin_rejected{}; // MWR: no select/removal retention
		// MWR Target_IsInRect helper 0x1402E9FD0 calls the target projection
		// 0x1402E9F00 (H2 0x1405130D0) at 0x1402EA13A (H2 0x14051330A).
		constexpr std::uintptr_t target_projection=0x1402e9f00,target_call=0x1402ea13a;
		bool project_target(game::gentity_s* actor,float fov,const float* delta,float* screen)
		{
			++javelin_calls;
			const auto owner=carry::current_hold();
			auto muzzle=current_muzzle();
			// MWR: only the Javelin display is ported (the launcher presenter and
			// disposal boundaries are not), so require its open, current camera.
			if(actor!=&game::g_entities[0] || muzzle.profile_id!="javelin" || !javelin_screen::requested(owner))
				{++javelin_rejected;return utils::hook::invoke<bool>(target_projection,actor,fov,delta,screen);}
			const auto input=controller_input::latest();
			if(!javelin_screen::lock_aim(owner,muzzle)){++javelin_rejected;return false;}
			if(!actor->client || !delta || !screen || !input.focused || !scripted_control::allowed(actor->client) ||
				!weapons::ready(muzzle,owner,input.reference_generation,controller_input::clock::now())){++javelin_rejected;return false;}
			// GScr target projection passes target - (actor.currentOrigin + viewHeight).
			// Its native projection reads entity angles at +0x40. Keep both read-only.
			hands::vec eye{};std::memcpy(eye.data(),reinterpret_cast<const std::byte*>(actor)+0xf4,sizeof(eye));
			eye[2]+=reinterpret_cast<const game::playerState_s*>(actor->client)->viewHeightCurrent;
			std::array<hands::vec,3> axis{};
			static_assert(sizeof(axis) == 9 * sizeof(float));
			game::AnglesToAxis(reinterpret_cast<const float*>(reinterpret_cast<const std::byte*>(actor)+0x40),
				reinterpret_cast<float(*)[3]>(axis.data()));
			const auto corrected=reticle_delta({delta[0],delta[1],delta[2]},eye,muzzle,axis);
			for(float x:corrected)if(!std::isfinite(x))return false;
			++projected;
			return utils::hook::invoke<bool>(target_projection,actor,fov,corrected.data(),screen);
		}
		template<size_t N>bool verify(std::uintptr_t at,const std::uint8_t(&bytes)[N])
		{std::array<std::uint8_t,N> mask{};mask.fill(255);return bool(utils::hook_validation::verify_masked_bytes(reinterpret_cast<void*>(at),{bytes,mask.data(),N}));}
	}
	std::array<std::uint64_t,3> native_statistics()noexcept{return {projected.load(),javelin_calls.load(),javelin_rejected.load()};}
	class native_component final:public component_interface
	{
		void post_unpack()override
		{
			constexpr std::uint8_t target[]{0xe8,0xc1,0xfd,0xff,0xff};
			constexpr std::uint8_t projection[]{0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x70};
			constexpr std::uint8_t eye_height[]{0xf3,0x0f,0x5c,0x88,0x1c,0x01,0x00,0x00}; // H2 +0x118
			constexpr std::uint8_t angles[]{0x48,0x83,0xc1,0x40,0x48,0x8d,0x54,0x24,0x30};
			// MWR: viewHeight 0x1402EA129 (H2 0x1405132F9), angles 0x1402E9F1E (H2 0x1405130EE).
			// The H2 automatic select/removal and aim-mode hooks serve the
			// disposable/manual launchers and are not rebound.
			if(!verify(target_call,target) || !verify(target_projection,projection) ||
				!verify(0x1402ea129,eye_height) || !verify(0x1402e9f1e,angles))
			{console::error("[VR launcher] Native target projection contract rejected\n");return;}
			utils::hook::call(target_call,project_target);
		}
	};
}
REGISTER_COMPONENT(vr::gameplay::weapons::launcher::native_component)
