#pragma once
#include <algorithm>
#include <json.hpp>

namespace launcher_game_language
{
	inline bool blocks_launch(const nlohmann::json& result)
	{
		// Steam owns the effective language. Its folder inventory is advisory:
		// it cannot validate the native loader's resolution or repair a selection.
		if (result.value("managedBySteam", false)) return false;
		const auto& selected = result.at("language");
		const auto& choices = result.at("choices");
		return std::none_of(choices.begin(), choices.end(), [&](const auto& choice) { return choice.at("value") == selected; });
	}
}
