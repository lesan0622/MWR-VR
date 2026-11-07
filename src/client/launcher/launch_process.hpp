#pragma once
namespace launcher_process
{
	void use_executable_directory();
	void start_game(bool unlock_campaign);
	// OpenVR backend only: start SteamVR and wait for vrserver before the game loads.
	void ensure_steamvr_for_openvr();
}
