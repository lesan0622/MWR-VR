#include "component/vr/native_post_aa_contract.hpp"
#include <cassert>
#include <stdexcept>

int main()
{
	using namespace vr::native_post_aa;
	for (const auto selected : {mode::none, mode::fxaa, mode::smaa})
		assert(effective_mode(selected) == selected);
	for (const auto selected : {mode::smaa_t2x, mode::filmic_smaa, mode::filmic_smaa_t2x})
	{
		assert(effective_mode(selected) == mode::smaa);
		assert(supported(effective_mode(selected)));
		std::array<std::byte, mode_offset + sizeof(mode) + 16> record{};
		record.fill(std::byte{0x5a});
		std::memcpy(record.data() + mode_offset, &selected, sizeof(selected));
		const auto original = record;
		{
			const scoped_mode outer(record.data());
			const auto effective_record = record;
			{
				const scoped_mode inner(record.data());
				assert(record == effective_record);
			}
			assert(record == effective_record);
		}
		assert(record == original);
		try
		{
			const scoped_mode override(record.data());
			mode dispatched{};
			std::memcpy(&dispatched, record.data() + mode_offset, sizeof(dispatched));
			assert(dispatched == mode::smaa);
			throw std::runtime_error("simulate render unwind");
		}
		catch (const std::runtime_error&) {}
		assert(record == original);
	}
	assert(!supported(effective_mode(mode::fxaa_internal)));
	assert(!supported(effective_mode(static_cast<mode>(0xffff))));
}
