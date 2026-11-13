#include <std_include.hpp>
#include "dxvk_install.hpp"
#include <fstream>

namespace launcher_dxvk
{
	namespace
	{
		constexpr const wchar_t* libraries[]{L"d3d11.dll", L"dxgi.dll"};

		bool same_content(const std::filesystem::path& a, const std::filesystem::path& b)
		{
			std::error_code error;
			const auto size = std::filesystem::file_size(a, error);
			if (error || size != std::filesystem::file_size(b, error) || error) return false;
			std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
			if (!left || !right) return false;
			std::vector<char> x(1 << 16), y(1 << 16);
			while (left && right)
			{
				left.read(x.data(), static_cast<std::streamsize>(x.size()));
				right.read(y.data(), static_cast<std::streamsize>(y.size()));
				if (left.gcount() != right.gcount() ||
					std::memcmp(x.data(), y.data(), static_cast<std::size_t>(left.gcount())) != 0) return false;
			}
			return left.eof() && right.eof();
		}

		std::string narrow(const std::filesystem::path& path)
		{
			const auto text = path.u8string();
			return {text.begin(), text.end()};
		}
	}

	void sync(const std::filesystem::path& game_directory, const bool enabled)
	{
		const auto bundle = game_directory / L"overlord-mwr" / L"dxvk";
		std::error_code error;
		// Check every file before copying any: a half-installed pair (DXVK
		// d3d11 beside a foreign dxgi) would keep the game from starting.
		if (enabled)
			for (const auto* name : libraries)
			{
				const auto source = bundle / name;
				const auto target = game_directory / name;
				if (!std::filesystem::is_regular_file(source, error))
					throw std::runtime_error("The Vulkan renderer (DXVK) files are missing: " + narrow(source) +
						". Reinstall MWR VR or turn the Vulkan renderer off.");
				if (std::filesystem::exists(target, error) && !same_content(source, target))
					throw std::runtime_error("Another " + narrow(target.filename()) +
						" (for example ReShade or a different DXVK) is in the game folder. Remove it or turn the "
						"Vulkan renderer (DXVK) off.");
			}
		for (const auto* name : libraries)
		{
			const auto source = bundle / name;
			const auto target = game_directory / name;
			const bool present = std::filesystem::exists(target, error);
			if (!enabled)
			{
				// Remove only our own copy; another wrapper stays untouched.
				if (present && std::filesystem::exists(source, error) && same_content(source, target))
					if (!std::filesystem::remove(target, error) || error)
						throw std::runtime_error("Could not remove " + narrow(target) +
							" (Vulkan renderer turned off). Close the game and try again.");
				continue;
			}
			if (present) continue; // identical, checked above
			// Copy beside the target, then rename, so a failed copy never leaves a
			// truncated d3d11/dxgi that would stop the game from starting.
			auto partial = target;
			partial += L".mwrvr-partial";
			std::filesystem::copy_file(source, partial, std::filesystem::copy_options::overwrite_existing, error);
			if (!error) std::filesystem::rename(partial, target, error);
			if (error)
			{
				std::filesystem::remove(partial, error);
				throw std::runtime_error("Could not install " + narrow(target) +
					" for the Vulkan renderer (DXVK). Check folder permissions.");
			}
		}
	}
}
