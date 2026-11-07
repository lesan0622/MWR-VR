#include <std_include.hpp>
#include "settings_menu.hpp"
#include "settings.hpp"
#include "resource.hpp"
#include "component/command.hpp"
#include "component/console.hpp"
#include "loader/component_loader.hpp"
#include "game/game.hpp"
#include <json.hpp>
#include <utils/nt.hpp>

namespace vr::settings_menu
{
	namespace
	{
		using json = nlohmann::json;
		constexpr std::array categories{"basics", "calibration", "gameplay", "cheats", "other"};
		// Applied at game start only; changing them in game would mislead.
		constexpr std::array launcher_only{settings::runtime_backend.name, settings::controller_pose_mode.name};

		// Read during static initialization, like ui_scripting's Lua resources:
		// once the game image is mapped, the process module no longer holds them.
		const std::unordered_map<int, std::string> resources = []
		{
			std::unordered_map<int, std::string> result;
			for (const int id : {VR_SETTINGS_SCHEMA, VR_EXTRA_SETTINGS, VR_LOCALE_EN, VR_LOCALE_KO, VR_LOCALE_DE, VR_LOCALE_ES,
				VR_LOCALE_FR, VR_LOCALE_JA, VR_LOCALE_RU, VR_LOCALE_ZH_CN, VR_LOCALE_ZH_TW})
				result[id] = utils::nt::load_resource(id);
			return result;
		}();
		json parse(int resource)
		{
			const auto found = resources.find(resource);
			if (found == resources.end()) return {};
			auto value = json::parse(found->second, nullptr, false);
			return value.is_discarded() ? json{} : value;
		}
		// language_t -> launcher locale (game language without a locale falls back to English).
		const char* locale_name(int language)
		{
			switch (language)
			{
			case 1: return "fr";
			case 2: return "de";
			case 4: case 14: return "es";
			case 5: case 17: return "ru";
			case 8: case 9: return "ja";
			case 10: return "zh-TW";
			case 11: return "zh-CN";
			case 15: return "ko";
			default: return "en";
			}
		}
		const json& locale(const std::string& name)
		{
			static const std::unordered_map<std::string, json> locales{
				{"en", parse(VR_LOCALE_EN)}, {"ko", parse(VR_LOCALE_KO)}, {"de", parse(VR_LOCALE_DE)},
				{"es", parse(VR_LOCALE_ES)}, {"fr", parse(VR_LOCALE_FR)}, {"ja", parse(VR_LOCALE_JA)},
				{"ru", parse(VR_LOCALE_RU)}, {"zh-CN", parse(VR_LOCALE_ZH_CN)}, {"zh-TW", parse(VR_LOCALE_ZH_TW)}};
			const auto found = locales.find(name);
			return found != locales.end() ? found->second : locales.at("en");
		}
		std::string condition(const json& value)
		{
			if (value.is_boolean()) return value.get<bool>() ? "1" : "0";
			return value.is_string() ? value.get<std::string>() : value.dump();
		}
		const settings::number_setting* number(const std::string& name)
		{
			for (const auto& field : settings::numbers) if (name == field.name) return &field;
			return nullptr;
		}
		const settings::choice_setting* choice(const std::string& name)
		{
			for (const auto& field : settings::choices) if (name == field.name) return &field;
			return nullptr;
		}
		std::unordered_map<std::string, unsigned> rejections;
		void reject(const char* reason) {++rejections[reason];}
		const settings::boolean_setting* toggle(const std::string& name)
		{
			for (const auto& field : settings::toggles) if (name == field.name) return &field;
			return nullptr;
		}
	}

	std::string text(int language, const std::string& key)
	{
		for (const auto* name : {locale_name(language), "en"})
			if (const auto& table = locale(name); table.is_object() && table.contains(key) && table[key].is_string())
				return table[key].get<std::string>();
		return key;
	}

	std::vector<entry> catalog(int language)
	{
		static const json schema = parse(VR_SETTINGS_SCHEMA);
		static const json extras = parse(VR_EXTRA_SETTINGS);
		std::vector<entry> out;
		const auto make = [&](const json& field) -> std::optional<entry> {
			if (!field.is_object() || !field.contains("name") || !field.contains("category") || !field.contains("type")) {reject("shape");return {};}
			entry e;
			e.name = field["name"].get<std::string>();
			e.category = field["category"].get<std::string>();
			if (std::find(categories.begin(), categories.end(), e.category) == categories.end()) {reject("category");return {};}
			if (std::find(launcher_only.begin(), launcher_only.end(), e.name) != launcher_only.end()) {reject("launcher_only");return {};}
			if (!game::Dvar_FindVar(e.name.c_str())) {reject("unregistered");return {};} // Not registered by this build.
			e.category_label = text(language, "settings." + e.category);
			e.group = field.value("group", "");
			e.group_label = text(language, e.group);
			if (field.contains("visibleWhen") && field["visibleWhen"].is_array() && field["visibleWhen"].size() == 2)
			{e.when_name = field["visibleWhen"][0].get<std::string>();e.when_value = condition(field["visibleWhen"][1]);}
			const auto type = field["type"].get<std::string>();
			if (type == "number")
			{
				e.type = kind::number;
				if (const auto* n = number(e.name)) {e.min = n->min;e.max = n->max;e.step = n->step;e.scale = n->display_scale;}
				else if (field.contains("min") && field.contains("max") && field.contains("step"))
				{e.min = field["min"].get<float>();e.max = field["max"].get<float>();e.step = field["step"].get<float>();}
				else return {};
			}
			else if (type == "choice")
			{
				const auto* c = choice(e.name);
				if (!c) return {};
				e.type = kind::choice;
				for (std::size_t i = 0; i < c->values.size() && c->values[i]; ++i)
					e.options.push_back({c->values[i], text(language, c->label_keys[i])});
			}
			else if (type == "toggle")
			{
				e.type = kind::toggle;
				if (const auto* t = toggle(e.name)) e.enabled_level = t->max_enabled_value;
			}
			else return {};
			if (field.contains("text"))
			{
				// vr_extra_settings.json: [label, description] per locale.
				const auto& texts = field["text"];
				const auto* lang = texts.contains(locale_name(language)) ? locale_name(language) : "en";
				if (!texts.contains(lang) || !texts[lang].is_array() || texts[lang].size() != 2) return {};
				e.label = texts[lang][0].get<std::string>();e.description = texts[lang][1].get<std::string>();
			}
			else
			{
				e.label = text(language, field.value("label", e.name));
				if (field.contains("row")) e.label = text(language, field["row"].get<std::string>()) + " - " + e.label;
				e.description = field.contains("description") ? text(language, field["description"].get<std::string>()) : "";
			}
			return e;
		};
		for (const auto* category : categories)
		{
			// Launcher order; extras join their group after its launcher rows.
			std::vector<entry> rows;
			for (const auto* list : {&schema, &extras})
				if (list->is_array())
					for (const auto& field : *list)
						if (field.value("category", "") == category)
							if (auto e = make(field)) rows.push_back(std::move(*e));
			std::vector<std::string> groups;
			for (const auto& row : rows)
				if (std::find(groups.begin(), groups.end(), row.group) == groups.end()) groups.push_back(row.group);
			for (const auto& group : groups)
				for (const auto& row : rows)
					if (row.group == group) out.push_back(row);
		}
		return out;
	}

	std::string value(const std::string& name)
	{
		const auto* dvar = game::Dvar_FindVar(name.c_str());
		if (!dvar) return {};
		switch (dvar->type)
		{
		case game::dvar_type::boolean: return dvar->current.enabled ? "1" : "0";
		case game::dvar_type::integer: return std::to_string(dvar->current.integer);
		case game::dvar_type::value: return std::format("{}", dvar->current.value);
		case game::dvar_type::enumeration:
			return dvar->current.integer >= 0 && dvar->current.integer < dvar->domain.enumeration.stringCount ?
				dvar->domain.enumeration.strings[dvar->current.integer] : "";
		case game::dvar_type::string: return dvar->current.string ? dvar->current.string : "";
		default: return {};
		}
	}

	bool set(const std::string& name, const std::string& value)
	{
		if (!game::Dvar_FindVar(name.c_str()) || value.empty() || value.size() > 64 ||
			value.find_first_of("\";\n\r") != std::string::npos) return false;
		command::execute(std::format("set {} \"{}\"", name, value), true);
		return true;
	}
}

namespace vr::settings_menu
{
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			command::add("vr_settings_menu_status", [](const command::params& params)
			{
				rejections.clear();
				const auto entries = catalog(params.size() > 1 ? std::atoi(params.get(1)) : 0);
				std::string reasons;
				for (const auto& [reason, count] : rejections) reasons += std::format(" {}={}", reason, count);
				console::info("[VR settings menu] entries=%zu rejected:%s schema=%s extras=%s\n", entries.size(), reasons.c_str(),
					parse(VR_SETTINGS_SCHEMA).type_name(), parse(VR_EXTRA_SETTINGS).type_name());
				for (const auto& e : entries) console::info("  %s/%s %s = %s\n", e.category.c_str(), e.group.c_str(), e.name.c_str(), value(e.name).c_str());
			});
		}
	};
}

REGISTER_COMPONENT(vr::settings_menu::component)
