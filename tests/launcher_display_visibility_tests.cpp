#include "component/vr/gameplay/viewmodel_policy.hpp"
#include <cassert>

int main()
{
	using namespace vr::gameplay::weapons;
	part_mask assembly{};
	assembly[0] = 0x80000000;
	part_mask reload{};
	reload[1] = 0x10000000;
	const auto normal = combine_part_masks(assembly, reload);
	assert(display_viewmodel_mask(normal, false) == normal);
	const auto aiming = display_viewmodel_mask(normal, true);
	for (const auto word : aiming) assert(word == ~std::uint32_t{});
	// Lowering the launcher restores the normal cosmetic/reload mask on
	// the next publication; no persistent mask or bone mutation is involved.
	assert(display_viewmodel_mask(normal, false) == normal);
	assert(normal[0] == assembly[0] && normal[1] == reload[1]);
}
