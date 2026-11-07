#include <std_include.hpp>
#include "loader/component_loader.hpp"

#include "game/game.hpp"

#include "command.hpp"
#include "console.hpp"

#include <utils/hook.hpp>

namespace lui
{
	class component final : public component_interface
	{
	public:
		void post_unpack() override
		{
			command::add("lui_open", [](const command::params& params)
			{
				if (params.size() <= 1)
				{
					console::info("usage: lui_open <name>\n");
					return;
				}

				game::LUI_OpenMenu(0, params[1], 0, 0, 0);
			});

			command::add("lui_open_popup", [](const command::params& params)
			{
				if (params.size() <= 1)
				{
					console::info("usage: lui_open_popup <name>\n");
					return;
				}

				game::LUI_OpenMenu(0, params[1], 1, 0, 0);
			});

			command::add("lui_restart", []()
			{
				// MWR: one LUI restart entry (h1-mod SP 0x1401052C0) replaces H2's
				// hks shutdown 0x1403203B0 + start 0x14032D370 pair.
				utils::hook::invoke<void>(0x1401052C0);
			});
		}
	};
}

REGISTER_COMPONENT(lui::component)
