#pragma once
#include "../engine_stereo_view.hpp"
#include "component/scene_models.hpp"
#include "game/game.hpp"
#include <utils/native_memory.hpp>
#include <array>
#include <cmath>
#include <mutex>

// MWR port: there is no render-time skeleton record (weapon_render_pose), so parts
// attached to the hands are submitted at their solve-time world placement. The arms
// themselves are drawn from the scene record's CURRENT placement origin, which moves
// with the headset. Moving a part by the same origin change keeps it on the hand.
namespace vr::gameplay::render_origin_rebase
{
	using vec = std::array<float, 3>;
	namespace detail
	{
		inline std::mutex mutex;
		inline vec solve_origin{}, solve_lag{};
		inline bool valid{};
	}
	// The hand solve's view offset (refdef.viewOffset) for the latest published pose.
	// `lag` is the camera-minus-placement offset added to the rendered arm bones.
	inline void publish_solve_origin(const vec& origin, const vec& lag = {}) noexcept
	{
		const std::lock_guard lock(detail::mutex);
		for (int i = 0; i < 3; ++i) detail::solve_origin[i] = origin[i] - lag[i];
		detail::solve_lag = lag;
		detail::valid = true;
	}
	inline vec solve_lag() noexcept
	{
		const std::lock_guard lock(detail::mutex);
		return detail::solve_lag;
	}
	inline bool solve_origin(vec& out) noexcept
	{
		const std::lock_guard lock(detail::mutex);
		out = detail::solve_origin;
		return detail::valid;
	}
	// Shift a placement built relative to `submitted` onto the record's current origin.
	inline scene_models::placement_result apply(const void* record, const vec& submitted,
		game::GfxPlacement& current, game::GfxPlacement& previous) noexcept
	{
		vec origin{};
		if (!record || !utils::native_memory::read_bytes(origin.data(),
			static_cast<const std::byte*>(record) + engine_stereo_view::h2_current_model_placement_origin_offset, sizeof(origin)))
			return scene_models::placement_result::unchanged;
		float length2{};
		vec shift{};
		for (int i = 0; i < 3; ++i)
		{
			shift[i] = origin[i] - submitted[i];
			if (!std::isfinite(origin[i]) || !std::isfinite(shift[i])) return scene_models::placement_result::unchanged;
			length2 += shift[i] * shift[i];
		}
		if (length2 > 64.f * 64.f) return scene_models::placement_result::unchanged; // stale or foreign record
		for (int i = 0; i < 3; ++i)
		{
			current.origin[i] += shift[i];
			previous.origin[i] += shift[i];
		}
		return scene_models::placement_result::replace;
	}
	inline scene_models::placement_result apply(const void* record, game::GfxPlacement& current,
		game::GfxPlacement& previous) noexcept
	{
		vec submitted{};
		return solve_origin(submitted) ? apply(record, submitted, current, previous)
			: scene_models::placement_result::unchanged;
	}
}
