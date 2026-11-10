#pragma once
#include "hand.hpp"
#include "input_observation.hpp"
#include <atomic>
#include <utility>

namespace vr::handedness
{
	inline std::atomic_bool left_controls{},left_holster{};
	inline void initialize(bool controls,bool holster) noexcept {left_controls=controls;left_holster=holster;}
	inline hand dominant() noexcept {return left_controls.load()?hand::left:hand::right;}
	// Logical controls only; tracking, triggers, grips and releases remain physical.
	template<class Frame> void controls(Frame& frame) noexcept
	{
		using controller_input::input_channel;
		using controller_input::index;
		if(!left_controls.load())return;
		std::swap(frame.move,frame.turn);std::swap(frame.move_active,frame.turn_active);
		std::swap(frame.sprint,frame.jump);
		std::swap(frame.source.channels[index(input_channel::move)],frame.source.channels[index(input_channel::turn)]);
		std::swap(frame.source.channels[index(input_channel::sprint)],frame.source.channels[index(input_channel::jump)]);
		frame.primary[0]=frame.menu_recenter;
		frame.menu_recenter=frame.primary[1];frame.primary[1]={};
	}
}
