#include "component/vr/native_post_aa_contract.hpp"
#include <cassert>

int main()
{
	using namespace vr::native_post_aa;
	// Every menu mode runs natively in VR (no SMAA 1x substitution).
	for (const auto selected : {mode::none, mode::fxaa, mode::smaa, mode::smaa_t2x, mode::filmic_smaa, mode::filmic_smaa_t2x})
		assert(supported(selected));
	assert(temporal(mode::smaa_t2x) && temporal(mode::filmic_smaa_t2x) && !temporal(mode::smaa));
	assert(needs_history(mode::smaa_t2x, 16) && !needs_history(mode::smaa, 16) && needs_history(mode::filmic_smaa, 20));
	assert(!supported(mode::fxaa_internal));
	assert(!supported(static_cast<mode>(0xffff)));
}
