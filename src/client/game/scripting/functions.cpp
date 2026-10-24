#include <std_include.hpp>
#include "functions.hpp"

#include "component/gsc/script_extension.hpp"
#include "component/gsc/script_loading.hpp"

#include <utils/string.hpp>
#include <xsk/gsc/engine/h1.hpp>

namespace xsk::gsc::h1
{
	extern std::array<std::pair<u16, char const*>, func_count> const func_list;
	extern std::array<std::pair<u16, char const*>, meth_count> const meth_list;
}

namespace scripting
{
	namespace
	{
		// MWR (H1 SP) has no GSC context. Its builtin ids match the H1 PC
		// catalog: functions 1..0x30a, methods 0x8000..0x8586.
		const std::unordered_map<std::string_view, std::uint16_t>& builtin_ids(const bool methods)
		{
			static const auto build = [](const auto& list)
			{
				std::unordered_map<std::string_view, std::uint16_t> map;
				for (const auto& [id, name] : list) map.emplace(name, id);
				return map;
			};
			static const auto functions = build(xsk::gsc::h1::func_list);
			static const auto method_map = build(xsk::gsc::h1::meth_list);
			return methods ? method_map : functions;
		}

		int find_function_index(const std::string& name, const bool prefer_global)
		{
			const auto target = utils::string::to_lower(name);
			const auto& first = builtin_ids(!prefer_global);
			const auto& second = builtin_ids(prefer_global);
			if (const auto itr = first.find(target); itr != first.end()) return itr->second;
			if (const auto itr = second.find(target); itr != second.end()) return itr->second;
			return -1;
		}

		script_function get_function_by_index(const unsigned index)
		{
			// MWR native builtin tables written by Scr_RegisterFunction
			// (0x1403BD860): functions [id-1] (0x30a), methods [id-0x8000] (0x587).
			static const auto function_table = 0x14B8CC510;
			static const auto method_table = 0x14B8CDD60;
			if ((index < 0x1000 && (index < 1 || index > 0x30a)) || (index >= 0x1000 && (index < 0x8000 || index > 0x8586))) return nullptr;

			if (index < 0x1000)
			{
				return reinterpret_cast<script_function*>(function_table)[index - 1];
			}

			return reinterpret_cast<script_function*>(method_table)[index - 0x8000];
		}

		unsigned int parse_token_id(const std::string& name)
		{
			if (name.starts_with("_ID"))
			{
				return static_cast<unsigned int>(std::strtol(name.substr(3).data(), nullptr, 10));
			}

			return 0;
		}
	}

	std::vector<std::string> find_token(unsigned int id)
	{
		std::vector<std::string> results;

		results.push_back(utils::string::va("_ID%i", id));
		results.push_back(utils::string::va("_id_%04X", id));
		if (gsc::gsc_ctx) results.push_back(gsc::gsc_ctx->token_name(id));

		return results;
	}

	std::string find_token_single(unsigned int id)
	{
		return gsc::gsc_ctx ? gsc::gsc_ctx->token_name(id) : utils::string::va("_ID%i", id);
	}

	unsigned int find_token_id(const std::string& name)
	{
		const auto id = gsc::gsc_ctx ? gsc::gsc_ctx->token_id(name) : 0u;
		if (id)
		{
			return id;
		}

		const auto parsed_id = parse_token_id(name);
		if (parsed_id)
		{
			return parsed_id;
		}

		return game::SL_GetCanonicalString(name.data());
	}

	script_function find_function(const std::string& name, const bool prefer_global)
	{
		const auto index = find_function_index(name, prefer_global);
		if (index < 0)
		{
			return nullptr;
		}

		return get_function_by_index(index);
	}
}
