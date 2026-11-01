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
	eGsDebugGrip = 2, // the left hand's grip IK
	eGsDebugAim = 4, // the sight calibration
};
