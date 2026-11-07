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
