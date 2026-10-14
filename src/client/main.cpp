#include <std_include.hpp>
#include "launcher/launcher.hpp"
#include "launcher/vr_settings.hpp"
#include "launcher/launch_process.hpp"
#include "component/game_data.hpp"
#include "loader/loader.hpp"
#include "loader/component_loader.hpp"
#include "loader/target_identity.hpp"
#include "game/game.hpp"

#include <utils/string.hpp>
#include <utils/flags.hpp>
#include <utils/io.hpp>
#include <utils/properties.hpp>

#include <cstdint>
#include <string_view>

namespace
{

	DECLSPEC_NORETURN void WINAPI exit_hook(const int code)
	{
		// Normal game exit already retires components at Com_Quit_f, before native
		// graphics/driver teardown. Retain this idempotent fallback for startup
		// failures and direct ExitProcess paths that never enter the game loop.
		component_loader::pre_destroy();
		exit(code);
	}

	BOOL WINAPI system_parameters_info_a(const UINT uiAction, const UINT uiParam, const PVOID pvParam, const UINT fWinIni)
	{
		// MWR: the first SystemParametersInfoA call happens after Arxan has
		// decrypted .text (h1-mod's unpack point).
		try
		{
			component_loader::post_unpack();
		}
		catch (const std::exception& e)
		{
			MessageBoxA(nullptr, e.what(), "ERROR", MB_ICONERROR);
			exit_hook(1);
		}
		catch (...)
		{
			MessageBoxA(nullptr, "Unknown exception during component post_unpack", "ERROR", MB_ICONERROR);
			exit_hook(1);
		}

		return SystemParametersInfoA(uiAction, uiParam, pvParam, fWinIni);
	}

	FARPROC WINAPI get_proc_address(const HMODULE hModule, const LPCSTR lpProcName)
	{
		if (reinterpret_cast<std::uintptr_t>(lpProcName) <= 0xFFFF)
		{
			return GetProcAddress(hModule, lpProcName);
		}

		const std::string_view proc_name{lpProcName};
		if (proc_name == "ExitProcess")
		{
			return reinterpret_cast<FARPROC>(exit_hook);
		}

		return GetProcAddress(hModule, lpProcName);
	}

	launcher::mode detect_mode_from_arguments()
	{
		if (utils::flags::has_flag("singleplayer"))
		{
			return launcher::mode::singleplayer;
		}

		return launcher::mode::none;
	}

	void apply_aslr_patch(std::string* data)
	{
		// sp binary
		if (data->size() != game_data::supported_binary_size)
		{
			throw std::runtime_error("File size mismatch, bad game files");
		}

		auto* dos_header = reinterpret_cast<PIMAGE_DOS_HEADER>(&data->at(0));
		auto* nt_headers = reinterpret_cast<PIMAGE_NT_HEADERS>(&data->at(dos_header->e_lfanew));
		auto* optional_header = &nt_headers->OptionalHeader;

		if (optional_header->DllCharacteristics & IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE)
		{
			optional_header->DllCharacteristics &= ~(IMAGE_DLLCHARACTERISTICS_DYNAMIC_BASE);
		}
	}

	void get_aslr_patched_binary(std::string* binary, std::string* data)
	{
		const auto patched_binary = (utils::properties::get_appdata_path() / "bin/h1_sp64_ship.exe"s).generic_string();

		try
		{
			apply_aslr_patch(data);
			std::string cached_data{};
			if ((!utils::io::read_file(patched_binary, &cached_data) || cached_data != *data)
				&& !utils::io::write_file(patched_binary, *data, false))
			{
				throw std::runtime_error("Could not write file");
			}
		}
		catch (const std::exception& e)
		{
			throw std::runtime_error(
				utils::string::va("Could not create aslr patched binary for %s! %s",
					binary->data(), e.what())
			);
		}

		*binary = patched_binary;
	}

	FARPROC load_binary(const launcher::mode mode)
	{
		loader loader;
		utils::nt::library self;

		loader.set_import_resolver([self](const std::string& library, const std::string& function) -> void*
		{
			// MWR imports steam_api64; resolve it to the exported emulation
			// (h1-mod). Arxan needs one genuine import.
			if (library == "steam_api64.dll" && function != "SteamAPI_GetSteamInstallPath")
			{
				return self.get_proc<FARPROC>(function);
			}

			if (function == "ExitProcess")
			{
				return exit_hook;
			}
			else if (function == "SystemParametersInfoA")
			{
				return system_parameters_info_a;
			}
			else if (function == "GetProcAddress")
			{
				return get_proc_address;
			}

			return component_loader::load_import(library, function);
		});

		std::string binary = game_data::get_game_binary_path();

		std::string data;
		if (!utils::io::read_file(binary, &data))
		{
			throw std::runtime_error(utils::string::va(
				"Failed to read game binary (%s)!\nPlease copy overlord.exe into your Call of Duty: Modern Warfare Remastered installation folder and run it from there.",
				binary.data()));
		}

		target_identity::capture_original(binary, data);
		get_aslr_patched_binary(&binary, &data);
		target_identity::capture_loaded(binary, data);

#ifdef INJECT_HOST_AS_LIB
		return loader.load_library(binary);
#else
		return loader.load(self, data);
#endif
	}

	void remove_crash_file()
	{
		utils::io::remove_file("__h1Exe");
	}

	void verify_version()
	{
		// On-disk (still encrypted) .text dword of the supported Steam
		// h1_sp64_ship.exe, sha256 3d23e869...b41f9190.
		const auto value = *reinterpret_cast<DWORD*>(0x140123456);
		if (value != 0xD0BC8AA6)
		{
			throw std::runtime_error("Unsupported Call of Duty: Modern Warfare Remastered version");
		}

		target_identity::mark_compatibility_probe_passed();
	}

	void enable_dpi_awareness()
	{
		const utils::nt::library user32{"user32.dll"};
		const auto set_dpi = user32
			? user32.get_proc<BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)>("SetProcessDpiAwarenessContext")
			: nullptr;

		if (set_dpi)
		{
			set_dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
		}
	}

	void limit_parallel_dll_loading()
	{
		const utils::nt::library self;
		const auto registry_path = R"(Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\)" + self.get_name();

		HKEY key = nullptr;
		if (RegCreateKeyA(HKEY_LOCAL_MACHINE, registry_path.data(), &key) == ERROR_SUCCESS)
		{
			RegCloseKey(key);
		}

		key = nullptr;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, registry_path.data(), 0, KEY_ALL_ACCESS, &key) != ERROR_SUCCESS)
		{
			return;
		}

		DWORD value = 1;
		RegSetValueExA(key, "MaxLoaderThreads", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value));

		RegCloseKey(key);
	}
}

int main()
{
	ShowWindow(GetConsoleWindow(), SW_HIDE);

	FARPROC entry_point;
	enable_dpi_awareness();

	// Keep WebView2 and the game loader in different processes. The launcher
	// path never starts game components, graphics hooks or tracking runtimes.
	if (detect_mode_from_arguments() == launcher::mode::none)
	{
		try
		{
			launcher_process::use_executable_directory();
			const launcher launcher;
			return launcher.run();
		}
		catch (const std::exception& error)
		{
			MessageBoxA(nullptr, error.what(), "Overlord", MB_ICONERROR);
			return 1;
		}
	}

	limit_parallel_dll_loading();

	srand(uint32_t(time(nullptr)));
	remove_crash_file();

	{
		auto premature_shutdown = true;
		const auto _ = gsl::finally([&premature_shutdown]()
		{
			if (premature_shutdown)
			{
				component_loader::pre_destroy();
			}
		});

		try
		{
			if (!component_loader::post_start())
			{
				return 0;
			}

			auto mode = detect_mode_from_arguments();

			game::environment::set_mode(mode);
			launcher_vr_settings::initialize_startup_options(utils::flags::has_flag("launcher-start"));

			entry_point = load_binary(mode);
			if (!entry_point)
			{
				throw std::runtime_error("Unable to load binary into memory");
			}

			verify_version();

			if (!component_loader::post_load())
			{
				return 0;
			}

			premature_shutdown = false;
		}
		catch (std::exception& e)
		{
			MessageBoxA(nullptr, e.what(), "ERROR", MB_ICONERROR);
			return 1;
		}
	}

	const auto shutdown = gsl::finally([]
	{
		component_loader::pre_destroy();
	});
	return static_cast<int>(entry_point());
}

int __stdcall WinMain(HINSTANCE, HINSTANCE, PSTR, int)
{
	return main();
}
