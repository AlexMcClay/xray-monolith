////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_console.cpp
//	Description : Gunsmith console commands (registered from CCC_RegisterCommands)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"

extern int g_gs_debug; // Weapon_gunsmith.cpp

void register_gunsmith_console_commands()
{
	CMD4(CCC_Integer, "gs_debug", &g_gs_debug, 0, 7);
	extern int g_gs_part_draw; // gunsmith_visuals.cpp
	CMD4(CCC_Integer, "gs_part_draw", &g_gs_part_draw, 0, 1);
}
