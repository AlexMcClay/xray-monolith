////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_console.cpp
//	Description : Gunsmith console commands (registered from CCC_RegisterCommands)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"
#include "gunsmith_grip_ik.h"

extern int g_gs_debug; // Weapon_gunsmith.cpp

void register_gunsmith_console_commands()
{
	CMD4(CCC_Integer, "gs_grip_ik", &g_gs_grip_ik, 0, 1);
	CMD4(CCC_Integer, "gs_grip_type", &g_gs_grip_type, -1, 1);
	CMD4(CCC_Integer, "gs_debug", &g_gs_debug, 0, 7);
}
