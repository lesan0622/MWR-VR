#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>

// User height (vr_playerHeight, cm). Body-relative equipment (holsters, chest
// and abdomen slots, shoulders) and the physical crouch/prone thresholds are
// authored for a 175 cm player and scale with the measured or entered height.
// The camera keeps the native eye height: the eye stays inside the native
// player hull and weapon geometry keeps one unit per inch.
namespace vr::player_height
{
	inline constexpr float reference_cm = 175.f, min_cm = 120.f, max_cm = 220.f;
	inline constexpr float eye_to_crown_m = .11f; // Standing centre-eye height to the top of the head.
	inline std::atomic<float> body_scale{1.f};
	// vr_armScale: the viewmodel arm (shoulder -> wrist) length relative to the
	// model; a T-pose measurement sets it from the real arm. model_arm_units is
	// the unscaled model arm length published by the hand solver (world units).
	inline constexpr float min_arm_scale = .85f, max_arm_scale = 1.25f;
	inline std::atomic<float> arm_scale{1.f};
	inline std::atomic<float> model_arm_units{NAN};
	// Joint-centre shoulder half width per metre of height (0.18 m at 175 cm,
	// the vr_shoulderHalfWidth default); grip centre to wrist joint.
	inline constexpr float shoulder_half_width_per_height = .18f / 1.75f, grip_to_wrist_m = .08f;
	// Backend-published HMD centre-eye height above the floor (OpenXR stage
	// space or the OpenVR standing universe), with its steady-clock sample time.
	inline std::atomic<float> head_floor_meters{NAN};
	inline std::atomic<std::int64_t> head_floor_at{};
	inline void publish_floor(float meters) noexcept
	{
		if (!std::isfinite(meters)) return;
		head_floor_meters = meters;
		head_floor_at = std::chrono::steady_clock::now().time_since_epoch().count();
	}
	inline std::optional<float> floor_height(std::chrono::milliseconds max_age = std::chrono::milliseconds(500)) noexcept
	{
		const auto at = std::chrono::steady_clock::time_point{std::chrono::steady_clock::duration{head_floor_at.load()}};
		const float meters = head_floor_meters.load();
		if (!head_floor_at.load() || !std::isfinite(meters) || std::chrono::steady_clock::now() - at > max_age) return {};
		return meters;
	}
	inline float scale_for(float cm) noexcept
	{return std::isfinite(cm) ? std::clamp(cm, min_cm, max_cm) / reference_cm : 1.f;}
	// Standing eye height to body height; rejects seated or implausible samples.
	inline std::optional<float> measured_cm(float eye_meters) noexcept
	{
		if (!std::isfinite(eye_meters) || eye_meters < min_cm / 100.f - eye_to_crown_m || eye_meters > max_cm / 100.f - eye_to_crown_m) return {};
		return std::round((eye_meters + eye_to_crown_m) * 100.f);
	}
}
