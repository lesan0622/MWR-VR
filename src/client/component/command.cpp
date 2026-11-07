#include <std_include.hpp>
#include "vr/gameplay/weapon_carry_runtime.hpp"
#include "vr/gameplay/native_carry.hpp"
#include "loader/component_loader.hpp"

#include "game/game.hpp"
#include "game/dvars.hpp"

#include "game/scripting/execution.hpp"

#include "command.hpp"
#include "scheduler.hpp"
#include "console.hpp"
#include "fastfiles.hpp"

#include <utils/hook.hpp>
#include <utils/string.hpp>
#include <utils/memory.hpp>
#include <utils/io.hpp>

namespace command
{
	namespace
	{
		utils::hook::detour dvar_command_hook;

		std::unordered_map<std::string, std::function<void(params&)>> handlers;

		bool game_initialized{};

		void main_handler()
		{
			params params = {};

			const auto command = utils::string::to_lower(params[0]);
			if (handlers.find(command) != handlers.end())
			{
				handlers[command](params);
			}
		}

		game::dvar_t* dvar_command_stub()
		{
			const params args;

			if (args.size() <= 0)
			{
				return 0;
			}

			auto dvar = game::Dvar_FindVar(args[0]);
			if (dvar == nullptr)
			{
				const auto hash = static_cast<int>(std::strtoull(args[0], nullptr, 16));
				dvar = game::Dvar_FindMalleableVar(hash);
			}

			if (dvar)
			{
				if (args.size() == 1)
				{
					const auto current = game::Dvar_ValueToString(dvar, nullptr, &dvar->current);
					const auto reset = game::Dvar_ValueToString(dvar, nullptr, &dvar->reset);

					const auto info = dvars::get_dvar_info_from_hash(dvar->name);
					std::string desc{};
					std::string name = args[0];

					if (info.has_value())
					{
						name = info.value().name;
						desc = info.value().description;
					}

					console::info("\"%s\" is: \"%s\" default: \"%s\" hash: 0x%08lX flags: 0x%08lX\n",
						name.data(), current, reset, dvar->name, dvar->flags);

					console::info("%s\n", desc.data());
					console::info("   %s\n", dvars::dvar_get_domain(dvar->type, dvar->domain).data());
				}
				else
				{
					char command[0x1000] = {0};
					game::Dvar_GetCombinedString(command, 1);
					game::Dvar_SetCommand(dvar->name, "", command);
				}

				return dvar;
			}

			return 0;
		}
	}

	params::params()
		: nesting_(game::cmd_args->nesting)
	{
	}

	int params::size() const
	{
		return game::cmd_args->argc[this->nesting_];
	}

	const char* params::get(const int index) const
	{
		if (index >= this->size())
		{
			return "";
		}

		return game::cmd_args->argv[this->nesting_][index];
	}

	std::string params::join(const int index) const
	{
		std::string result = {};

		for (auto i = index; i < this->size(); i++)
		{
			if (i > index) result.append(" ");
			result.append(this->get(i));
		}
		return result;
	}

	void add_raw(const char* name, void (*callback)())
	{
		// MWR Cmd_AddCommandInternal ignores a name that is already registered
		// (e.g. the game's own cheat-gated "give"). Take over its callback instead.
		struct native_cmd { native_cmd* next; const char* name; void (*function)(); };
		for (auto* cmd = *reinterpret_cast<native_cmd**>(0x14B490038); cmd; cmd = cmd->next)
		{
			if (cmd->name && !_stricmp(cmd->name, name))
			{
				cmd->function = callback;
				return;
			}
		}
		game::Cmd_AddCommandInternal(name, callback, utils::memory::get_allocator()->allocate<game::cmd_function_s>());
	}

	void add(const char* name, const std::function<void(const params&)>& callback)
	{
		const auto command = utils::string::to_lower(name);

		if (handlers.find(command) == handlers.end())
		{
			add_raw(name, main_handler);
		}

		handlers[command] = callback;
	}

	void add(const char* name, const std::function<void()>& callback)
	{
		add(name, [callback](const params&)
		{
			callback();
		});
	}

	void execute(std::string command, const bool sync)
	{
		if (!game_initialized)
		{
			return;
		}

		command += "\n";

		if (sync)
		{
			game::Cmd_ExecuteSingleCommand(0, 0, command.data());
		}
		else
		{
			game::Cbuf_AddText(0, command.data());
		}
	}

	bool is_game_initialized()
	{
		return game_initialized;
	}

	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			utils::hook::jump(0x1403A7C80, dvar_command_stub, true);

			scheduler::once([]
			{
				game_initialized = true;
			}, scheduler::main);

			add("quit", game::Quit);

			add("startmap", [](const params& params)
			{
				const auto map = params.get(1);

				// MWR: the H2 map-exists/SV_SpawnServer entry points are not rebound;
				// route through the native map command instead.
				execute(utils::string::va("map %s", map), false);
			});

			add("listassetpool", [](const params& params)
			{
				if (params.size() < 2)
				{
					console::info("listassetpool <poolnumber>: list all the assets in the specified pool\n");

					for (auto i = 0; i < game::XAssetType::ASSET_TYPE_COUNT; i++)
					{
						console::info("%d %s\n", i, game::g_assetNames[i]);
					}
				}
				else
				{
					const auto type = static_cast<game::XAssetType>(atoi(params.get(1)));

					if (type < 0 || type >= game::XAssetType::ASSET_TYPE_COUNT)
					{
						console::info("Invalid pool passed must be between [%d, %d]\n", 0, game::XAssetType::ASSET_TYPE_COUNT - 1);
						return;
					}

					console::info("Listing assets in pool %s\n", game::g_assetNames[type]);

					fastfiles::enum_assets(type, [type](const game::XAssetHeader header)
					{
						const auto asset = game::XAsset{type, header};
						const auto* const asset_name = game::DB_GetXAssetName(&asset);
						//const auto entry = game::DB_FindXAssetEntry(type, asset_name);
						//TODO: display which zone the asset is from
						console::info("%s\n", asset_name);
					}, true);
				}
			});

			add("commandDump", []()
			{
				printf("======== Start command dump =========\n");

				game::cmd_function_s* cmd = (*game::cmd_functions);

				while (cmd)
				{
					if (cmd->name)
					{
						console::info("%s\n", cmd->name);
					}

					cmd = cmd->next;
				}

				printf("======== End command dump =========\n");
			});

			// MWR: native give (no GSC); registered outside the disabled h2-mod cheat block.
			add("give", [](const params& params)
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				if (params.size() < 2)
				{
					game::CG_GameMessage(0, "You did not specify a weapon name", 0);
					return;
				}

				const std::string arg = params.get(1);
				const std::string arg2 = params.get(2);
				const auto count = params.size();

				// MWR port: native implementation; the GSC VM is not available.
				scheduler::once([=]()
				{
					auto* ent = &game::g_entities[0];
					auto* ps = ent->client;
					if (!ps) return;
					const auto give = [&](game::Weapon wp, bool dual_wield, bool select)
					{
						if (const auto duplicate = vr::gameplay::weapons::native_carry::give_duplicate(wp.data, dual_wield);
							duplicate.has_value())
						{
							if (!*duplicate) console::warn("[VR carry] give rejected: %s\n", vr::gameplay::weapons::native_carry::status());
							return *duplicate;
						}
						if (!game::G_GivePlayerWeapon(ps, wp, dual_wield, 0, 0, 0)) return false;
						game::G_InitializeAmmo(ps, wp, 0);
						// With physical carry, a new gun is stored on the body for the hands to draw.
						if (select && !vr::gameplay::weapons::carry::active()) game::G_SelectWeapon(0, wp);
						return true;
					};

					if (arg == "ammo" || arg == "allammo")
					{
						// Refill the start ammo of the current (or every owned) weapon.
						for (int i = 0; i < 15; ++i)
						{
							game::Weapon wp{};
							std::memcpy(&wp.data, reinterpret_cast<const std::byte*>(ps) + 0x2cc + i * 4, 4);
							std::uint32_t current{};
							std::memcpy(&current, reinterpret_cast<const std::byte*>(ps) + 0x38c, 4);
							if (wp.data && (arg == "allammo" || wp.data == current)) game::G_InitializeAmmo(ps, wp, 1);
						}
					}
					else if (arg == "health")
					{
						// MWR gentity health +0x184, playerState health +0x1dc.
						auto* health = reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ent) + 0x184);
						const auto* max = game::Dvar_FindVar("g_player_maxhealth");
						const int amount = count > 2 ? *health + std::atoi(arg2.data()) : (max ? max->current.integer : 100);
						*health = amount;
						*reinterpret_cast<int*>(reinterpret_cast<std::byte*>(ps) + 0x1dc) = amount;
					}
					else if (arg == "all")
					{
						// Every weapon definition loaded by this level (the table ends at the first gap).
						for (unsigned i = 1; i < 512 && game::weapon_defs[i]; ++i)
						{
							game::Weapon wp{};
							wp.data = i;
							give(wp, false, false);
						}
					}
					else
					{
						const auto wp = game::G_GetWeaponForName(arg.data());
						if (!wp.data)
						{
							game::CG_GameMessage(0, "Weapon does not exist", 0);
							console::warn("[give] %s is not loaded in this level\n", arg.data());
							return;
						}
						const auto def = game::weapon_defs[wp.fields.weaponIdx];
						const auto dual_wield = def && !def->noDualWield && std::atoi(arg2.c_str());
						const bool ok = give(wp, dual_wield, true);
						console::info("[give] %s (%u) -> %s\n", arg.data(), wp.data, ok ? "given" : "rejected (inventory full)");
					}
				}, scheduler::pipeline::server);
			});

			// MWR: write a checkpoint now, as GSC savegame("<name>") does (native 0x1402DCA60,
			// builtin 0x85): autosave\<name>, no description, default screenshot, type 2.
			add("savegame", [](const params& params)
			{
				if (!game::SV_Loaded()) return;
				const std::string name = params.size() > 1 ? params.get(1) : "levelstart";
				scheduler::once([=]()
				{
					char filename[64]{};
					sprintf_s(filename, "autosave\\%s", name.data());
					utils::hook::invoke<void>(0x140300AA0);
					const auto result = utils::hook::invoke<int>(0x1404C4F90, filename, "", "$default", 1, 2, false);
					console::info("[savegame] %s -> %d\n", filename, result);
				}, scheduler::pipeline::server);
			});

			add("take", [](const params& params)
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				if (params.size() < 2)
				{
					game::CG_GameMessage(0, "You did not specify a weapon name", 0);
					return;
				}

				const std::string weapon = params.get(1);

				// MWR port: native G_TakePlayerWeapon; the GSC VM is not available.
				scheduler::once([=]()
				{
					auto* ps = game::g_entities[0].client;
					if (!ps) return;
					std::vector<game::Weapon> targets;
					if (weapon == "all"s)
					{
						for (int i = 0; i < 15; ++i)
						{
							game::Weapon wp{};
							std::memcpy(&wp.data, reinterpret_cast<const std::byte*>(ps) + 0x2f8 + i * 4, 4); // Owned weapons (carry probe).
							if (wp.data) targets.push_back(wp);
						}
					}
					else if (const auto wp = game::G_GetWeaponForName(weapon.data()); wp.data) targets.push_back(wp);
					for (const auto wp : targets)
						console::info("[take] %u -> %s\n", unsigned(wp.data), game::G_TakePlayerWeapon(ps, wp) ? "taken" : "not owned");
					if (targets.empty()) console::info("[take] unknown weapon %s\n", weapon.data());
				}, scheduler::pipeline::server);
			});

			// MWR: these h2-mod cheats use H2 entity layouts and the GSC scripting
			// context, which are not ported. Keep the native MWR commands instead.
			if constexpr (false)
			{
			add("god", []()
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				game::g_entities[0].flags ^= game::FL_GODMODE;
				game::CG_GameMessage(0, utils::string::va("godmode %s",
					game::g_entities[0].flags & game::FL_GODMODE
					? "^2on"
					: "^1off"), 0);
			});

			add("demigod", []()
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				game::g_entities[0].flags ^= game::FL_DEMI_GODMODE;
				game::CG_GameMessage(0, utils::string::va("demigod mode %s",
					game::g_entities[0].flags & game::FL_DEMI_GODMODE
					? "^2on"
					: "^1off"), 0);
			});

			add("notarget", []()
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				game::g_entities[0].flags ^= game::FL_NOTARGET;
				game::CG_GameMessage(0, utils::string::va("notarget %s",
					game::g_entities[0].flags & game::FL_NOTARGET
					? "^2on"
					: "^1off"), 0);
			});

			add("noclip", []()
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				game::g_entities[0].client->flags ^= 1;
				game::CG_GameMessage(0, utils::string::va("noclip %s",
					game::g_entities[0].client->flags & 1
					? "^2on"
					: "^1off"), 0);
			});

			add("ufo", []()
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				game::g_entities[0].client->flags ^= 2;
				game::CG_GameMessage(
					0, utils::string::va("ufo %s", game::g_entities[0].client->flags & 2 ? "^2on" : "^1off"), 0);
			});

			add("dropweapon", [](const params& params)
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				scheduler::once([]()
				{
					try
					{
						const scripting::entity player = scripting::call("getentbynum", {0}).as<scripting::entity>();
						const auto weapon = player.call("getcurrentweapon");
						player.call("dropitem", {weapon});
					}
					catch (...)
					{
					}
				}, scheduler::pipeline::server);
			});

			add("kill", [](const params& params)
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				scheduler::once([]()
				{
					try
					{
						const scripting::entity player = scripting::call("getentbynum", {0}).as<scripting::entity>();
						player.call("kill");
					}
					catch (...)
					{
					}
				}, scheduler::pipeline::server);
			});

			add("setviewpos", [](const params& params)
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				scripting::vector origin
				(
					static_cast<float>(std::atof(params.get(1))),
					static_cast<float>(std::atof(params.get(2))),
					static_cast<float>(std::atof(params.get(3)))
				);

				scheduler::once([=]()
				{
					try
					{
						const scripting::entity player{game::Scr_GetEntityId(0, 0)};
						player.call("setorigin", {origin});
					}
					catch (...)
					{
					}
				}, scheduler::pipeline::server);
			});

			add("setviewang", [](const params& params)
			{
				if (!game::SV_Loaded())
				{
					return;
				}

				scripting::vector angles
				(
					static_cast<float>(std::atof(params.get(1))),
					static_cast<float>(std::atof(params.get(2))),
					static_cast<float>(std::atof(params.get(3)))
				);

				scheduler::once([=]()
				{
					try
					{
						const scripting::entity player{game::Scr_GetEntityId(0, 0)};
						player.call("setplayerangles", {angles});
					}
					catch (...)
					{
					}
				}, scheduler::pipeline::server);
			});

			add("fxDumpActive", []()
			{
				const auto system = game::FX_GetSystem(0);
				if (system == nullptr)
				{
					return;
				}

				printf("============================================================\n");

				for (auto i = system->firstActiveEffect; i < system->firstFreeEffect; i++)
				{
					const auto handle = system->allEffectHandles[2 * (i & 0x7FF)];
					const auto fx = reinterpret_cast<game::FxEffect*>(reinterpret_cast<size_t>(system->effects) + 16 * handle);
					printf("\t%s @ (%f, %f, %f)\n", fx->def->name, fx->frameNow.origin[0], fx->frameNow.origin[1], fx->frameNow.origin[2]);
				}

				printf("TOTAL FX: %d\n", system->firstFreeEffect - system->firstActiveEffect);
			
				printf("============================================================\n");
			});
			}
		}
	};
}

REGISTER_COMPONENT(command::component)
