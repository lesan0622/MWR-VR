#pragma once
#include "controller_stance.hpp"
#include "../player_height.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>

// Real crouching and lying down select the native stance (user request). The
// head's drop below its standing height (the highest head seen since the last
// recenter, in the HMD tracking reference) picks stand/crouch/prone with
// hysteresis; only a change of that physical stance issues the native stance
// command, so the stick keeps working while the head stays put. The camera
// already follows the real head, so it cancels the native eye-height drop up
// to the physical drop instead of lowering the view twice.
namespace vr::gameplay::physical_stance
{
	inline constexpr float crouch_enter = .35f, crouch_exit = .25f; // metres below standing
	inline constexpr float prone_enter = .85f, prone_exit = .70f;
	inline constexpr float standing_eye_units = 60.f; // MWR standing viewheight.
	inline std::atomic<float> drop_meters{}; // Published for the camera (render thread).
	inline std::atomic_bool active{};
	class tracker
	{
		std::uint64_t reference_{};
		float standing_{};
		bool valid_{};
		controller_input::posture physical_{controller_input::posture::stand};
	public:
		void reset() noexcept {*this = {};drop_meters = 0;active = false;}
		// head_up_meters: head height in the tracking reference (up positive).
		std::optional<controller_input::posture> update(float head_up_meters, std::uint64_t reference) noexcept
		{
			using controller_input::posture;
			if (!std::isfinite(head_up_meters)) return {};
			if (!valid_ || reference != reference_) {valid_ = true;reference_ = reference;standing_ = head_up_meters;physical_ = posture::stand;}
			standing_ = std::max(standing_, head_up_meters);
			const float drop = std::max(0.f, standing_ - head_up_meters);
			drop_meters = drop;active = true;
			// Thresholds are authored for a 175 cm player (vr_playerHeight).
			const float s = player_height::body_scale.load();
			const float crouch_in = crouch_enter * s, crouch_out = crouch_exit * s, prone_in = prone_enter * s, prone_out = prone_exit * s;
			auto next = physical_;
			switch (physical_)
			{
			case posture::stand: next = drop > prone_in ? posture::prone : drop > crouch_in ? posture::crouch : posture::stand;break;
			case posture::crouch: next = drop > prone_in ? posture::prone : drop < crouch_out ? posture::stand : posture::crouch;break;
			case posture::prone: next = drop < crouch_out ? posture::stand : drop < prone_out ? posture::crouch : posture::prone;break;
			default: break;
			}
			if (next == physical_) return {};
			physical_ = next;
			return next;
		}
	};
	// Camera: units to add back to the native eye (never more than the real drop).
	inline float eye_compensation_units(float view_height, float units_per_meter) noexcept
	{
		if (!active.load() || !std::isfinite(view_height) || !std::isfinite(units_per_meter) || units_per_meter <= 0) return 0;
		const float native_drop = std::max(0.f, standing_eye_units - view_height);
		return std::min(native_drop, drop_meters.load() * units_per_meter);
	}
}
