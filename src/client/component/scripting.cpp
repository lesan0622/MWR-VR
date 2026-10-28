#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "scheduler.hpp"
#include "scripting.hpp"
#include "console.hpp"
#include "command.hpp"

#include "gsc/script_loading.hpp"

#include "game/scripting/event.hpp"
#include "game/scripting/functions.hpp"
#include "game/scripting/execution.hpp"
#include "game/scripting/lua/engine.hpp"

#include <utils/hook.hpp>
#include <utils/concurrency.hpp>

namespace scripting
{
	std::unordered_map<int, std::unordered_map<std::string, int>> fields_table;
	std::unordered_map<std::string, std::unordered_map<std::string, const char*>> script_function_table;
	std::unordered_map<std::string, std::vector<std::pair<std::string, const char*>>> script_function_table_sort;
	utils::concurrency::container<shared_table_t> shared_table;

	std::unordered_map<std::string, int> get_dvar_int_overrides;

	std::string current_file;

	namespace
	{
		utils::hook::detour vm_notify_hook;

		utils::hook::detour g_shutdown_game_hook;
		utils::hook::detour client_spawn_hook;
		utils::hook::detour sv_check_load_level_hook;

		utils::hook::detour scr_add_class_field_hook;

		utils::hook::detour scr_set_thread_position_hook;
		utils::hook::detour process_script_hook;

		utils::hook::detour sl_get_canonical_string_hook;

		utils::hook::detour respawn_hook;

		utils::hook::detour scr_run_current_threads_hook;

		utils::hook::detour scr_delete_hook;

		game::dvar_t* scr_auto_respawn = nullptr;

		std::string current_scriptfile;
		unsigned int current_file_id{};

		std::vector<std::function<void(bool, bool)>> shutdown_callbacks;
		std::vector<std::function<void()>> level_start_callbacks;

		std::unordered_map<unsigned int, std::string> canonical_string_table;

		using notify_list = std::vector<event>;
		std::array<notify_alias,8> notify_aliases{};std::size_t notify_alias_count{};
		utils::concurrency::container<notify_list> scheduled_notifies;

		void vm_notify_stub(const unsigned int notify_list_owner_id, const game::scr_string_t string_value,
		                    game::VariableValue* top)
		{
			std::array<game::scr_string_t,8> aliases{};std::size_t alias_count{};
			for(std::size_t i=0;i<notify_alias_count;++i)
			{
				const auto alias=notify_aliases[i](notify_list_owner_id,string_value,top);
				if(alias && alias!=string_value && std::find(aliases.begin(),aliases.begin()+alias_count,alias)==aliases.begin()+alias_count)aliases[alias_count++]=alias;
			}
			const auto* string = game::SL_ConvertToString(string_value);
			if (string)
			{
				event e;
				e.name = string;
				e.entity = notify_list_owner_id;

				for (auto* value = top; value->type != game::SCRIPT_END; --value)
				{
					e.arguments.emplace_back(*value);
				}

				lua::engine::handle_endon_conditions(e);

				scheduled_notifies.access([&](notify_list& list)
				{
					list.push_back(e);
				});
			}

			vm_notify_hook.invoke<void>(notify_list_owner_id, string_value, top);
			for(std::size_t i=0;i<alias_count;++i)vm_notify_hook.invoke<void>(notify_list_owner_id,aliases[i],top);
		}

		void clear_scheduled_notifies()
		{
			get_dvar_int_overrides.clear();
			scheduled_notifies.access([](notify_list& list)
			{
				list.clear();
			});
		}

		// MWR G_InitGame stores 2 here while a savegame is being restored (1 otherwise);
		// SV_CheckLoadLevel clears it after G_LoadGame (0x1402A231A / 0x1402A18A3).
		constexpr auto level_save_restore = 0x1456DC6C0;

		void start_level()
		{
			for (const auto& callback : level_start_callbacks) callback();
			scr_auto_respawn->current.enabled = true;
			clear_scheduled_notifies();
		}

		void client_spawn_stub(const game::gentity_s* client)
		{
			client_spawn_hook.invoke<void>(client);
			// SV_MapRestart can spawn the client and then restore a save over it;
			// that level start is reported once, after the restore, by SV_CheckLoadLevel.
			if (*reinterpret_cast<const int*>(level_save_restore) != 2) start_level();
		}

		void g_shutdown_game_stub(const int free_scripts)
		{
			if (free_scripts)
			{
				script_function_table_sort.clear();
				script_function_table.clear();
				canonical_string_table.clear();
			}

			for (const auto& callback : shutdown_callbacks)
			{
				callback(free_scripts, false);
			}

			clear_scheduled_notifies();
			g_shutdown_game_hook.invoke<void>(free_scripts);

			for (const auto& callback : shutdown_callbacks)
			{
				callback(free_scripts, true);
			}
		}
	
		void scr_add_class_field_stub(unsigned int classnum, game::scr_string_t name, unsigned int canonicalString, unsigned int offset)
		{
			const auto name_ = game::SL_ConvertToString(name);

			if (fields_table[classnum].find(name_) == fields_table[classnum].end())
			{
				fields_table[classnum][name_] = offset;
			}

			scr_add_class_field_hook.invoke<void>(classnum, name, canonicalString, offset);
		}

		void process_script_stub(const char* filename)
		{
			current_scriptfile = filename;

			const auto file_id = atoi(filename);
			if (file_id)
			{
				current_file_id = file_id;
			}
			else
			{
				current_file_id = 0;
				current_file = filename;
			}

			process_script_hook.invoke<void>(filename);
		}

		std::vector<std::string> get_token_names(unsigned int id)
		{
			auto result = scripting::find_token(id);

			if (const auto itr = canonical_string_table.find(id); itr != canonical_string_table.end())
			{
				result.push_back(itr->second);
			}

			return result;
		}

		void add_function_sort(unsigned int id, const char* pos)
		{
			std::string filename = current_file;
			if (current_file_id)
			{
				filename = scripting::get_token_single(current_file_id);
			}

			if (script_function_table_sort.find(filename) == script_function_table_sort.end())
			{
				const auto script = gsc::find_script(game::ASSET_TYPE_SCRIPTFILE, current_scriptfile.data(), false);
				if (script)
				{
					const auto end = &script->bytecode[script->bytecodeLen];
					script_function_table_sort[filename].emplace_back("__end__", end);
				}
			}

			const auto name = get_token_single(id);
			auto& itr = script_function_table_sort[filename];
			itr.insert(itr.end() - 1, {name, pos});
		}

		void add_function(const std::string& file, unsigned int id, const char* pos)
		{
			const auto function_names = scripting::get_token_names(id);
			for (const auto& name : function_names)
			{
				script_function_table[file][name] = pos;
			}
		}

		void scr_set_thread_position_stub(unsigned int thread_name, const char* code_pos)
		{
			add_function_sort(thread_name, code_pos);

			if (current_file_id)
			{
				const auto names = scripting::get_token_names(current_file_id);
				for (const auto& name : names)
				{
					add_function(name, thread_name, code_pos);
				}
			}
			else
			{
				add_function(current_file, thread_name, code_pos);
			}

			scr_set_thread_position_hook.invoke<void>(thread_name, code_pos);
		}


		// A save load skips ClientBegin/ClientSpawn (SV_InitGameVM 0x1404C40DB), so the
		// level start comes from here once G_LoadGame has restored the level.
		void sv_check_load_level_stub(void* save_game)
		{
			sv_check_load_level_hook.invoke<void>(save_game);
			if (save_game != nullptr) start_level();
		}

		unsigned int sl_get_canonical_string_stub(const char* str)
		{
			const auto result = sl_get_canonical_string_hook.invoke<unsigned int>(str);
			canonical_string_table[result] = str;
			return result;
		}

		void respawn_stub()
		{
			if (!scr_auto_respawn->current.enabled)
			{
				return;
			}

			respawn_hook.invoke<void>();
		}

		void scr_run_current_threads_stub()
		{
			notify_list list_copy{};
			scheduled_notifies.access([&](notify_list& list)
			{
				list_copy = list;
				list.clear();
			});

			for (const auto& e : list_copy)
			{
				lua::engine::notify(e);
			}

			scr_run_current_threads_hook.invoke<void>();
		}

		utils::hook::detour scr_get_dvar_int_hook;
		void scr_get_dvar_int_stub()
		{
			const auto dvar = game::Scr_GetString(0);

			if (get_dvar_int_overrides.find(dvar) != get_dvar_int_overrides.end())
			{
				game::Scr_AddInt(get_dvar_int_overrides[dvar]);
				return;
			}

			scr_get_dvar_int_hook.invoke<void>();
		}

		void* get_spawn_point_stub()
		{
			const auto spawn_point = utils::hook::invoke<void*>(0x1404B1670);
			if (spawn_point == nullptr)
			{
				console::warn("No spawnpoint found for this map, using (0, 0, 0)\n");
				return &game::g_entities[0];
			}
			return spawn_point;
		}

		void scr_delete_stub(game::scr_entref_t ref)
		{
			if (ref.entnum == 0 && ref.classnum == 0)
			{
				console::warn("Script tried to delete entity 0\n");
				return;
			}

			scr_delete_hook.invoke<void>(ref);
		}
	}

	std::string get_token_single(unsigned int id)
	{
		if (canonical_string_table.find(id) != canonical_string_table.end())
		{
			return canonical_string_table[id];
		}

		return scripting::find_token_single(id);
	}

	void on_shutdown(const std::function<void(bool, bool)>& callback)
	{
		shutdown_callbacks.push_back(callback);
	}
	void on_level_start(const std::function<void()>& callback)
	{
		level_start_callbacks.push_back(callback);
	}
	void on_notify_alias(notify_alias callback)
	{
		if(!callback || notify_alias_count==notify_aliases.size())throw std::runtime_error("Invalid notification alias registration");
		notify_aliases[notify_alias_count++]=callback;
	}

	std::optional<std::string> get_canonical_string(const unsigned int id)
	{
		if (const auto itr = canonical_string_table.find(id); itr != canonical_string_table.end())
		{
			return itr->second;
		}

		return {};
	}

	std::string get_token(unsigned int id)
	{
		if (const auto itr = canonical_string_table.find(id); itr != canonical_string_table.end())
		{
			return itr->second;
		}

		return find_token_single(id);
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			// MWR (H1 SP) rebinding, addresses shared with h1-mod's SP build.
			// Only level lifetime and entity field discovery are installed:
			// GSC loading/notify relays and the Lua engine stay H2-only.
			g_shutdown_game_hook.create(0x1402A5130, g_shutdown_game_stub);
			client_spawn_hook.create(0x14028ADC0, client_spawn_stub);
			// H2 0x1406B2940
			sv_check_load_level_hook.create(0x1404C38D0, sv_check_load_level_stub);
			scr_add_class_field_hook.create(0x1403C3CE0, scr_add_class_field_stub);

			scr_auto_respawn = dvars::register_bool("scr_autoRespawn", true, 0, "Automatically respawn player on death");

			command::add("getfunctionptr", [](const command::params& params)
			{
				const auto func = find_function(params.get(1), false);
				console::info("%p\n", func);
			});
		}
	};
}

REGISTER_COMPONENT(scripting::component)
