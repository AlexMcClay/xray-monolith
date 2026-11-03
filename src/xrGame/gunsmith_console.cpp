////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_console.cpp
//	Description : Gunsmith console commands (registered from CCC_RegisterCommands)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"
#include "gunsmith_grip_ik.h"
#include "gunsmith_icon_cache.h"

extern int g_gs_debug; // Weapon_gunsmith.cpp

namespace
{
	// Deletes the icon disk cache's files (gunsmith_icon_cache.h); icons already shown stay until redrawn.
	class CCC_GsIconCacheClear : public IConsole_Command
	{
	public:
		CCC_GsIconCacheClear(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

		virtual void Execute(LPCSTR args)
		{
			Msg("- [gunsmith] icon cache: %u files removed", gunsmith::icon_cache::clear());
		}
	};
}

void register_gunsmith_console_commands()
{
	CMD4(CCC_Integer, "gs_grip_ik", &g_gs_grip_ik, 0, 1);
	CMD4(CCC_Integer, "gs_grip_type", &g_gs_grip_type, -1, 1);
	CMD4(CCC_Integer, "gs_debug", &g_gs_debug, 0, 7);
	CMD1(CCC_GsIconCacheClear, "gs_icon_cache_clear");
}
