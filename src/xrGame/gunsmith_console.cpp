////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_console.cpp
//	Description : Gunsmith console commands (registered from CCC_RegisterCommands)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "../xrEngine/xr_ioconsole.h"
#include "../xrEngine/xr_ioc_cmd.h"
#include "gunsmith_grip_ik.h"
#include "gunsmith_icon_cache.h"
#include "gunsmith_preview.h"

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

	// The icons' look while the game runs: gs_icon_look lists it; gs_icon_look <name> <value> sets one of exposure,
	// ambient, specular, contrast, saturation, halo ([gunsmith_settings] icon_<name>) and redraws every live icon.
	// Not saved: put the values found into [gunsmith_settings].
	class CCC_GsIconLook : public IConsole_Command
	{
	public:
		CCC_GsIconLook(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

		virtual void Execute(LPCSTR args)
		{
			static const LPCSTR names[] = {"exposure", "ambient", "specular", "contrast", "saturation", "halo"};
			string64 name = {};
			float value = 0.f;
			if (args && sscanf(args, "%63s %f", name, &value) == 2)
			{
				string64 key;
				xr_sprintf(key, "icon_%s", name);
				if (!gunsmith::set_setting(key, value))
				{
					Msg("! [gunsmith] gs_icon_look: unknown name [%s]", name);
					return;
				}
				gunsmith::CPreview::instance().redraw_icons();
			}
			string512 out = {};
			for (LPCSTR n : names)
			{
				string64 key;
				xr_sprintf(key, "icon_%s", n);
				float v = 0.f;
				gunsmith::get_setting(key, v);
				string64 item;
				xr_sprintf(item, " %s=%.3g", n, v);
				xr_strcat(out, item);
			}
			Msg("- [gunsmith] icon look:%s", out);
		}

		virtual void Info(TInfo& I)
		{
			xr_strcpy(I, "[<exposure|ambient|specular|contrast|saturation|halo> <value>]");
		}
	};

	// Bakes every Gunsmith static icon with the live look (gunsmith_preview.h, bake_start); an argument keeps the
	// sections whose name contains it. In a level, with the game running.
	class CCC_GsIconBake : public IConsole_Command
	{
	public:
		CCC_GsIconBake(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

		virtual void Execute(LPCSTR args)
		{
			if (!g_pGameLevel)
			{
				Msg("! [gunsmith] icon bake: load a level first");
				return;
			}
			gunsmith::CPreview::instance().bake_start(args);
		}
	};
}

void register_gunsmith_console_commands()
{
	CMD4(CCC_Integer, "gs_grip_ik", &g_gs_grip_ik, 0, 1);
	CMD4(CCC_Integer, "gs_grip_type", &g_gs_grip_type, -1, 1);
	CMD4(CCC_Integer, "gs_debug", &g_gs_debug, 0, 7);
	CMD1(CCC_GsIconCacheClear, "gs_icon_cache_clear");
	CMD1(CCC_GsIconBake, "gs_icon_bake");
	CMD1(CCC_GsIconLook, "gs_icon_look");
}
