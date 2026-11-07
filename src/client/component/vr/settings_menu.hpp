#pragma once
#include <string>
#include <vector>

// In-game Options > VR catalog. Built from the launcher's own schema and
// locale files (embedded as resources) plus vr_extra_settings.json, so the
// game menu and the launcher name and limit every setting the same way.
// Only dvars registered by this build appear; startup-only settings (runtime
// backend, controller pose pipeline, debug probes) stay launcher-only.
namespace vr::settings_menu
{
	enum class kind { toggle, choice, number };
	struct option { std::string value, label; };
	struct entry
	{
		std::string name, category, category_label, group, group_label, label, description;
		kind type{kind::toggle};
		float min{}, max{}, step{1}, scale{1};
		int enabled_level{1};
		std::vector<option> options;
		std::string when_name, when_value; // Shown only while when_name's value equals when_value.
	};
	// language: native language_t (Engine.GetCurrentLanguage).
	std::vector<entry> catalog(int language);
	std::string text(int language, const std::string& key);
	// Toggles "1"/"0" (or the native level), choices their value, numbers native units.
	std::string value(const std::string& name);
	bool set(const std::string& name, const std::string& value);
}
