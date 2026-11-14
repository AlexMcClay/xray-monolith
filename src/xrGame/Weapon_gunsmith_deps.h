////////////////////////////////////////////////////////////////////////////
//	Module 		: Weapon_gunsmith_deps.h
//	Description : What Weapon.h needs for CWeapon's Gunsmith members (Weapon_gunsmith_members.h)
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_stats.h"
#include "gunsmith_visuals.h"
#include "gunsmith_tactical.h"

// Console gs_debug (Weapon_gunsmith.cpp): a bit mask of the logs to write.
extern int g_gs_debug;
enum
{
	eGsDebugPreview = 1, // the builder's preview and icons
	// 2: was the grip IK's log (the grip is Lua since 14 Nov 2026: gunsmith_grip.debug())
	eGsDebugAim = 4, // the sight calibration
};
