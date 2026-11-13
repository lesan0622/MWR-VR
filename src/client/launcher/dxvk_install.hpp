#pragma once
#include <filesystem>

namespace launcher_dxvk
{
	// Optional Vulkan renderer (vr_dxvk). DXVK only works as the game folder's
	// d3d11.dll/dxgi.dll, so the launcher places the bundled copies there
	// before starting the game and removes them again when the option is off.
	// Files that are not byte-identical to the bundled DXVK (ReShade, other
	// wrappers) are never overwritten or deleted.
	void sync(const std::filesystem::path& game_directory, bool enabled);
}
