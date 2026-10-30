#pragma once
#include "physical_reload_configuration.hpp"
#include "hands/pose_math.hpp"
#include <algorithm>
#include <cmath>

// AK-family rock-in geometry. Magazine poses are gun-local in native units;
// magazine +X is forward and +Z up at rest, about gun +Y (left).
namespace vr::gameplay::weapons::physical_reload
{
	struct rock_sample {bool hooked{};float tilt{};};
	inline hands::vec rock_lug(const hands::anchor& magazine, hands::vec top, const rock_in& r) noexcept
	{
		return hands::pose_math::compose(magazine, {hands::add(top, {r.lug_forward, 0, 0}), {0, 0, 0, 1}}).position;
	}
	// Tilt: the magazine's up axis leaning rearward (bottom forward) about gun Y.
	inline rock_sample sample_rock(const rock_in& r, const hands::anchor& rest, const hands::anchor& raw,
		hands::vec top, float units) noexcept
	{
		constexpr float degrees = 57.2957795f;
		rock_sample out;
		if (!std::isfinite(units) || units <= 0) return out;
		const auto pivot = rock_lug(rest, top, r), lug = rock_lug(raw, top, r);
		const float distance = hands::length(hands::sub(lug, pivot)) / units;
		const auto up = hands::rotate(hands::normalize(raw.rotation), {0, 0, 1});
		const auto rest_up = hands::rotate(hands::normalize(rest.rotation), {0, 0, 1});
		const float tilt = (std::atan2(-up[0], up[2]) - std::atan2(-rest_up[0], rest_up[2])) * degrees;
		const float lateral = std::asin(std::clamp(up[1], -1.f, 1.f)) * degrees;
		out.tilt = tilt;
		out.hooked = std::isfinite(distance) && std::isfinite(tilt) && distance < r.hook_radius && tilt >= -2.f &&
			tilt <= r.max_degrees && std::abs(lateral) <= r.lateral_degrees;
		return out;
	}
	// The seated magazine rotated rearward-up about its lug by tilt degrees.
	inline hands::anchor rocked(const rock_in& r, const hands::anchor& rest, hands::vec top, float tilt) noexcept
	{
		const auto pivot = rock_lug(rest, top, r);
		const float half = -std::clamp(tilt, 0.f, r.max_degrees) * .5f / 57.2957795f;
		const hands::quat q{0, std::sin(half), 0, std::cos(half)};
		return {hands::add(pivot, hands::rotate(q, hands::sub(rest.position, pivot))),
			hands::normalize(hands::multiply(q, rest.rotation))};
	}
}
