#include "launcher/game_language_policy.hpp"
#include <cassert>

int main()
{
	using nlohmann::json;
	using launcher_game_language::blocks_launch;
	auto result = json{{"language", "english"},
		{"choices", json::array({{{"value", "english_safe"}}})}, {"managedBySteam", true}};
	assert(!blocks_launch(result));
	result["choices"] = json::array();
	assert(!blocks_launch(result));
	result.erase("managedBySteam");
	assert(blocks_launch(result));
	result["choices"].push_back({{"value", "english"}});
	assert(!blocks_launch(result));
	result["language"] = "french";
	assert(blocks_launch(result));
}
