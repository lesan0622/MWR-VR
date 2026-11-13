#pragma once

#include <cstdint>

// Optional half-rate mode (vr_halfRate): the game renders at half the headset
// refresh and the runtime's reprojection (SteamVR Motion Smoothing / Meta ASW)
// fills the other display slots. OpenXR only.
namespace vr::half_rate
{
	// OpenXR frame loop: report the runtime's display period after xrWaitFrame.
	void observe_period(std::int64_t period_ns) noexcept;
	// True while the cap is applied; poses then target one display period later,
	// the slot a half-rate frame actually reaches.
	[[nodiscard]] bool active() noexcept;
	// Around xrWaitFrame: while active, start each frame two periods after the last.
	void before_wait() noexcept;
	void after_wait() noexcept;
}
