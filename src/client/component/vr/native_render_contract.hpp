#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace vr::native_render_contract
{
	// These offsets belong to the fingerprint-gated H2 renderer build.  Every
	// consumer must retain the executable and owner-thread admission contract.
	inline constexpr std::uintptr_t record_command_stream_offset = 0x3C8;
	inline constexpr std::uintptr_t record_frontend_offset = 0x1CD0;
	inline constexpr std::uintptr_t record_type_offset = 0x1CE0;
	inline constexpr std::uintptr_t record_target_0_offset = 0x29E0;
	inline constexpr std::uintptr_t record_target_1_offset = 0x29E4;
	inline constexpr std::uintptr_t record_target_2_offset = 0x29E8;
	inline constexpr std::uintptr_t record_target_selector_offset = 0x29EC;
	inline constexpr std::uint32_t expected_world_record_type = 4;
	inline constexpr std::uint32_t frontend_record_capacity = 4;
	// MWR 0x1405668D7 writes frontend+0x5419A8 + i*0x130; consumers 0x1405B5368 /
	// 0x1405B54C8 read the count at +0x542328 (H2 +0x541C08 / +0x542588).
	inline constexpr std::uintptr_t dynamic_light_descriptors_offset = 0x5419A8;
	inline constexpr std::uintptr_t dynamic_light_descriptor_count_offset = 0x542328;
	inline constexpr std::size_t maximum_dynamic_light_descriptors = 8;
	inline constexpr std::uintptr_t target_registry_base = 0x151868300;
	inline constexpr std::uintptr_t target_registry_stride = 0x40;
	inline constexpr std::uint32_t target_registry_capacity = 304;
	inline constexpr std::size_t target_registry_size =
		static_cast<std::size_t>(target_registry_capacity) * target_registry_stride;
	using target_registry_entry = std::array<std::uint8_t, target_registry_stride>;
	inline constexpr std::uint64_t h2_query_recent_generation_count = 8;

}
