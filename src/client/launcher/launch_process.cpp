#include <std_include.hpp>
#include "launch_process.hpp"
#include "bridge_protocol.hpp"
#include <TlHelp32.h>
#include <utils/io.hpp>

namespace launcher_process
{
	namespace
	{
		std::filesystem::path executable_path()
		{
			std::wstring path(32768, L'\0');
			const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
			if (!size || size >= path.size()) throw std::runtime_error("Could not locate the launcher executable.");
			path.resize(size);
			return path;
		}
	}

	namespace
	{
		bool process_running(const wchar_t* name)
		{
			const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (snapshot == INVALID_HANDLE_VALUE) return false;
			const auto close = gsl::finally([&] { CloseHandle(snapshot); });
			PROCESSENTRY32W entry{};
			entry.dwSize = sizeof(entry);
			for (bool more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry))
				if (_wcsicmp(entry.szExeFile, name) == 0) return true;
			return false;
		}

		// SteamVR's own location from the OpenVR path registry
		// (%LOCALAPPDATA%\openvr\openvrpaths.vrpath, "runtime": [<SteamVR dir>]).
		std::optional<std::filesystem::path> steamvr_startup()
		{
			wchar_t* local{};
			if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
			const std::filesystem::path registry = std::filesystem::path(local) / L"openvr" / L"openvrpaths.vrpath";
			CoTaskMemFree(local);
			std::string text;
			if (!utils::io::read_file(registry.string(), &text)) return {};
			const auto paths = nlohmann::json::parse(text, nullptr, false);
			if (!paths.is_object() || !paths.contains("runtime") || !paths["runtime"].is_array()) return {};
			for (const auto& runtime : paths["runtime"])
			{
				if (!runtime.is_string()) continue;
				const auto& utf8 = runtime.get_ref<const std::string&>();
				const auto startup = std::filesystem::path(std::u8string(utf8.begin(), utf8.end())) / L"bin" / L"win64" / L"vrstartup.exe";
				std::error_code error;
				if (std::filesystem::is_regular_file(startup, error)) return startup;
			}
			return {};
		}
	}

	// MWR: OpenVR attaches to a running SteamVR; with vrserver down its init
	// fails ("SteamVR vrserver is not running") and the game stayed flat on
	// the monitor. Start SteamVR first and wait for its server (user request).
	void ensure_steamvr_for_openvr()
	{
		char backend[16]{};
		const auto length = GetEnvironmentVariableA("H2V_VR_BACKEND", backend, sizeof(backend));
		if (!length || length >= sizeof(backend) || (_stricmp(backend, "openvr") && _stricmp(backend, "steamvr"))) return;
		if (process_running(L"vrserver.exe")) return;
		bool started{};
		if (const auto startup = steamvr_startup())
			started = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", startup->c_str(), nullptr,
				startup->parent_path().c_str(), SW_SHOWNORMAL)) > 32;
		if (!started)
			started = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"steam://run/250820", nullptr, nullptr, SW_SHOWNORMAL)) > 32;
		if (!started) return;
		const auto deadline = GetTickCount64() + 60000;
		while (GetTickCount64() < deadline && !process_running(L"vrserver.exe")) Sleep(250);
		// The server process appears before its drivers and compositor are up.
		if (process_running(L"vrserver.exe")) Sleep(4000);
	}

	void use_executable_directory()
	{
		std::filesystem::current_path(executable_path().parent_path());
	}

	void start_game(bool unlock_campaign)
	{
		const auto executable = executable_path();
		auto command = launcher_bridge::quote_argument(executable.wstring());
		int count{};
		auto* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
		if (!arguments) throw std::runtime_error("Could not read startup arguments.");
		const auto release = gsl::finally([&] { LocalFree(arguments); });
		for (int i = 1; i < count; ++i)
		{
			// This window owns the request; do not inherit a stale command-line value.
			if (_wcsicmp(arguments[i], L"-campaign-unlock") == 0)
			{
				if (i + 1 < count && arguments[i + 1][0] != L'-') ++i;
				continue;
			}
			command += L" " + launcher_bridge::quote_argument(arguments[i]);
		}
		command += L" -singleplayer -launcher-start";
		command += unlock_campaign ? L" -campaign-unlock 1" : L" -campaign-unlock 0";
		if (command.size() >= 32767) throw std::runtime_error("The startup command is too long.");
		STARTUPINFOW startup{};
		startup.cb = sizeof(startup);
		PROCESS_INFORMATION process{};
		const auto directory = std::filesystem::current_path().wstring();
		if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
			nullptr, directory.c_str(), &startup, &process)) throw std::runtime_error("Could not start the game process.");
		CloseHandle(process.hThread);
		CloseHandle(process.hProcess);
	}
}
