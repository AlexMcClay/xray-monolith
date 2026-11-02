////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_config.cpp
//	Description : Gunsmith part tree - read-only accessors for part, slot and
//	              receiver LTX data
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_config.h"
#include "alife_space.h"

namespace gunsmith
{
	namespace
	{
		void read_list(LPCSTR section, LPCSTR key, xr_vector<shared_str>& out)
		{
			out.clear();
			if (!pSettings->section_exist(section) || !pSettings->line_exist(section, key))
				return;
			LPCSTR str = pSettings->r_string(section, key);
			if (!str)
				return;
			for (int i = 0, n = _GetItemCount(str); i < n; ++i)
			{
				string256 item;
				_GetItem(str, i, item);
				if (xr_strlen(item))
					out.push_back(item);
			}
		}

		u8 parse_legacy_addon(LPCSTR value)
		{
			if (!value)
				return eLegacyNone;
			if (!xr_strcmp(value, "scope"))
				return eLegacyScope;
			if (!xr_strcmp(value, "silencer"))
				return eLegacySilencer;
			if (!xr_strcmp(value, "launcher"))
				return eLegacyLauncher;
			return eLegacyNone;
		}

		bool read_slot(LPCSTR slot_section, SSlotInfo& slot)
		{
			if (!pSettings->section_exist(slot_section))
				return false;
			slot.section = slot_section;
			read_list(slot_section, "accepts", slot.accepts);
			slot.mount_bone = READ_IF_EXISTS(pSettings, r_string, slot_section, "mount_bone", "");
			slot.required = !!READ_IF_EXISTS(pSettings, r_bool, slot_section, "required", FALSE);
			slot.field_swappable = !!READ_IF_EXISTS(pSettings, r_bool, slot_section, "field_swappable", FALSE);
			slot.legacy_addon = parse_legacy_addon(READ_IF_EXISTS(pSettings, r_string, slot_section, "legacy_addon", (LPCSTR)nullptr));
			slot.implicit = false;
			return true;
		}

		bool addon_attachable(LPCSTR receiver, LPCSTR status_key)
		{
			return READ_IF_EXISTS(pSettings, r_s32, receiver, status_key, s32(ALife::eAddonDisabled)) == s32(ALife::eAddonAttachable);
		}
	}

	bool is_full_mode(LPCSTR receiver)
	{
		// per section, read once (scripts ask on every tooltip and inventory event)
		static xr_map<shared_str, bool> cache;
		const shared_str key(receiver ? receiver : "");
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;
		LPCSTR mode = receiver ? READ_IF_EXISTS(pSettings, r_string, receiver, "gs_mode", (LPCSTR)nullptr) : nullptr;
		const bool full = mode && !xr_strcmp(mode, "full");
		cache.insert(mk_pair(key, full));
		return full;
	}

	static void slots_of_uncached(LPCSTR section, bool is_receiver, SLOTS& out)
	{
		out.clear();
		if (!section || !pSettings->section_exist(section))
			return;

		xr_vector<shared_str> names;
		read_list(section, is_receiver ? "gs_slots" : "slots", names);
		for (const shared_str& name : names)
		{
			SSlotInfo slot;
			if (read_slot(name.c_str(), slot))
				out.push_back(slot);
			else
				Msg("! [gunsmith] slot section [%s] listed by [%s] does not exist", name.c_str(), section);
		}

		// Full-mode guns take no old-style addons: scopes, suppressors and launchers are parts there.
		if (!is_receiver || is_full_mode(section))
			return;

		struct SLegacy
		{
			u8 addon;
			LPCSTR status_key;
			LPCSTR implicit_name;
		};
		static const SLegacy legacy[] = {
			{ eLegacyScope, "scope_status", "gs_slot_implicit_scope" },
			{ eLegacySilencer, "silencer_status", "gs_slot_implicit_silencer" },
			{ eLegacyLauncher, "grenade_launcher_status", "gs_slot_implicit_launcher" },
		};

		for (const SLegacy& l : legacy)
		{
			if (!addon_attachable(section, l.status_key))
				continue;
			bool covered = false;
			for (const SSlotInfo& s : out)
				covered |= (s.legacy_addon == l.addon);
			if (covered)
				continue;
			SSlotInfo slot;
			slot.section = l.implicit_name;
			slot.legacy_addon = l.addon;
			slot.implicit = true;
			out.push_back(slot);
		}
	}

	const SLOTS& slots_of(const shared_str& section, bool is_receiver)
	{
		// Slot lists only depend on configs, which don't change while the game runs, and they are read on every
		// shot and frame. Returned by reference: a map's entries stay put when others are added.
		static xr_map<shared_str, SLOTS> cache[2];
		static const SLOTS none;
		if (!section.size())
			return none;

		xr_map<shared_str, SLOTS>& c = cache[is_receiver ? 1 : 0];
		auto it = c.find(section);
		if (it != c.end())
			return it->second;
		SLOTS out;
		slots_of_uncached(section.c_str(), is_receiver, out);
		return c.insert(mk_pair(section, out)).first->second;
	}

	const SLOTS& slots_of(LPCSTR section, bool is_receiver)
	{
		return slots_of(shared_str(section && *section ? section : nullptr), is_receiver);
	}

	void tags_of(LPCSTR part, xr_vector<shared_str>& out)
	{
		read_list(part, "tags", out);
	}

	bool part_fits_slot(LPCSTR part, const SSlotInfo& slot, LPCSTR receiver, bool strict)
	{
		// A part's tags, read once per section (configs don't change while the game runs)
		static xr_map<shared_str, xr_vector<shared_str>> tag_cache;
		const shared_str key(part ? part : "");
		auto it = tag_cache.find(key);
		if (it == tag_cache.end())
		{
			xr_vector<shared_str> t;
			tags_of(part, t);
			it = tag_cache.insert(mk_pair(key, t)).first;
		}
		for (const shared_str& tag : it->second)
			for (const shared_str& accepted : slot.accepts)
				if (tag == accepted)
					return true;

		if (!strict && slot.legacy_addon != eLegacyNone)
			return is_legacy_part(receiver, slot.legacy_addon, part);
		return false;
	}

	void legacy_scope_parts(LPCSTR receiver, xr_vector<shared_str>& out)
	{
		out.clear();
		if (!receiver || !pSettings->section_exist(receiver))
			return;

		// Mirrors CWeapon::Load's scope list and CWeapon::GetScopeName().
		if (READ_IF_EXISTS(pSettings, r_bool, receiver, "modular_attachments", FALSE))
		{
			LPCSTR group = READ_IF_EXISTS(pSettings, r_string, receiver, "modular_scope_group", (LPCSTR)nullptr);
			if (group)
				read_list(group, "scopes", out);
			return;
		}

		xr_vector<shared_str> scope_sects;
		if (pSettings->line_exist(receiver, "scopes_sect"))
			read_list(receiver, "scopes_sect", scope_sects);
		else
			scope_sects.push_back(receiver);

		for (const shared_str& sect : scope_sects)
		{
			LPCSTR name = pSettings->section_exist(sect) ? READ_IF_EXISTS(pSettings, r_string, sect, "scope_name", (LPCSTR)nullptr) : nullptr;
			if (name && *name)
				out.push_back(name);
		}
	}

	bool is_legacy_part(LPCSTR receiver, u8 legacy_addon, LPCSTR part)
	{
		if (!part || !pSettings->section_exist(part))
			return false;

		LPCSTR cls = READ_IF_EXISTS(pSettings, r_string, part, "class", (LPCSTR)nullptr);
		switch (legacy_addon)
		{
		case eLegacySilencer:
			{
				LPCSTR name = READ_IF_EXISTS(pSettings, r_string, receiver, "silencer_name", (LPCSTR)nullptr);
				return (name && !xr_strcmp(name, part)) || (cls && !xr_strcmp(cls, "WP_SILEN"));
			}
		case eLegacyLauncher:
			{
				LPCSTR name = READ_IF_EXISTS(pSettings, r_string, receiver, "grenade_launcher_name", (LPCSTR)nullptr);
				return (name && !xr_strcmp(name, part)) || (cls && !xr_strcmp(cls, "WP_GLAUN"));
			}
		case eLegacyScope:
			{
				if (cls && !xr_strcmp(cls, "WP_SCOPE"))
					return true;
				xr_vector<shared_str> scopes;
				legacy_scope_parts(receiver, scopes);
				for (const shared_str& s : scopes)
					if (!xr_strcmp(s.c_str(), part))
						return true;
				return false;
			}
		default:
			return false;
		}
	}

	shared_str legacy_default_part(LPCSTR receiver, u8 legacy_addon)
	{
		switch (legacy_addon)
		{
		case eLegacySilencer:
			return READ_IF_EXISTS(pSettings, r_string, receiver, "silencer_name", (LPCSTR)nullptr);
		case eLegacyLauncher:
			return READ_IF_EXISTS(pSettings, r_string, receiver, "grenade_launcher_name", (LPCSTR)nullptr);
		case eLegacyScope:
			{
				xr_vector<shared_str> scopes;
				legacy_scope_parts(receiver, scopes);
				return scopes.empty() ? shared_str() : scopes.front();
			}
		default:
			return shared_str();
		}
	}

	LPCSTR legacy_addon_name(u8 legacy_addon)
	{
		switch (legacy_addon)
		{
		case eLegacyScope: return "scope";
		case eLegacySilencer: return "silencer";
		case eLegacyLauncher: return "launcher";
		default: return "none";
		}
	}

	//==========================================================================
	// Part stats
	//==========================================================================

	namespace
	{
		const LPCSTR STAT_NAMES[eStatCount] = {
			"fire_dispersion", "cam_dispersion", "cam_dispersion_inc", "cam_horz",
			"hit_power", "hit_impulse", "bullet_speed", "rpm",
			"pdm_base", "pdm_accel", "pdm_vel", "crosshair_inertion",
			"zoom_rotate_time", "condition_shot_dec", "cam_relax_speed",
		};

		// Old addon multiplier keys, read from silencer/scope/launcher sections by
		// CWeaponMagazined::Load*Koeffs. nullptr: no old key for this stat.
		const LPCSTR LEGACY_K_KEYS[eStatCount] = {
			"fire_dispersion_base_k", "cam_dispersion_k", "cam_dispersion_inc_k", nullptr,
			"bullet_hit_power_k", "bullet_hit_impulse_k", "bullet_speed_k", nullptr,
			"PDM_disp_base_k", "PDM_disp_accel_k", "PDM_disp_vel_k", "crosshair_inertion_k",
			"zoom_rotate_time_k", "condition_shot_dec_k", nullptr,
		};

		bool stat_add_in_degrees(u8 stat)
		{
			return stat == eStatFireDispersion || stat == eStatCamDispersion || stat == eStatCamDispersionInc;
		}

		void read_part_data(LPCSTR section, SPartData& d)
		{
			for (u32 s = 0; s < eStatCount; ++s)
			{
				d.add[s] = 0.f;
				d.mul[s] = 1.f;
				d.has_mul[s] = false;
				d.legacy_k[s] = 1.f;
			}
			d.weight = d.cost = d.legacy_weight = d.legacy_cost = 0.f;

			const bool exists = section && pSettings->section_exist(section);
			LPCSTR kind = exists ? READ_IF_EXISTS(pSettings, r_string, section, "part_kind", (LPCSTR)nullptr) : nullptr;
			d.declared = kind && *kind;
			if (!d.declared)
			{
				// Old-style addons without part_kind still get a kind for display and
				// sight handling.
				LPCSTR cls = exists ? READ_IF_EXISTS(pSettings, r_string, section, "class", (LPCSTR)nullptr) : nullptr;
				kind = (cls && !xr_strcmp(cls, "WP_SILEN")) ? "muzzle"
					: (cls && !xr_strcmp(cls, "WP_SCOPE")) ? "sight"
					: (cls && !xr_strcmp(cls, "WP_GLAUN")) ? "underbarrel"
					: "generic";
			}
			d.kind = kind;

			if (exists)
			{
				for (u32 s = 0; s < eStatCount; ++s)
				{
					string128 key;
					xr_sprintf(key, "%s_add", STAT_NAMES[s]);
					d.add[s] = READ_IF_EXISTS(pSettings, r_float, section, key, 0.f);
					if (stat_add_in_degrees(u8(s)))
						d.add[s] = deg2rad(d.add[s]);

					xr_sprintf(key, "%s_mul", STAT_NAMES[s]);
					d.has_mul[s] = !!pSettings->line_exist(section, key);
					d.mul[s] = d.has_mul[s] ? pSettings->r_float(section, key) : 1.f;

					if (LEGACY_K_KEYS[s])
					{
						d.legacy_k[s] = READ_IF_EXISTS(pSettings, r_float, section, LEGACY_K_KEYS[s], 1.f);
						clamp(d.legacy_k[s], 0.01f, 2.0f);
					}
				}

				d.weight = READ_IF_EXISTS(pSettings, r_float, section, "weight", 0.f);
				d.cost = READ_IF_EXISTS(pSettings, r_float, section, "part_cost", 0.f);
				d.legacy_weight = READ_IF_EXISTS(pSettings, r_float, section, "inv_weight", 0.f);
				d.legacy_cost = READ_IF_EXISTS(pSettings, r_float, section, "cost", 0.f);
				read_list(section, "blocks_slots", d.blocks_slots);
				read_list(section, "conflicts", d.conflicts);
				read_list(section, "tags", d.tags);
				d.parent_section = READ_IF_EXISTS(pSettings, r_string, section, "parent_section", "");
			}

			d.suppressor = exists && d.declared && !!READ_IF_EXISTS(pSettings, r_bool, section, "suppressor", FALSE);
			d.ammo_mag_size = exists && d.declared ? READ_IF_EXISTS(pSettings, r_s32, section, "ammo_mag_size", -1) : -1;
		}
	}

	LPCSTR stat_name(u8 stat)
	{
		return stat < eStatCount ? STAT_NAMES[stat] : "unknown";
	}

	LPCSTR failure_name(u8 failure)
	{
		switch (failure)
		{
		case eFailMisfire: return "misfire";
		case eFailExtraction: return "extraction";
		case eFailFeed: return "feed";
		case eFailBarrel: return "barrel";
		case eFailHardSlide: return "hard_slide";
		default: return "none";
		}
	}

	u8 failure_from_name(LPCSTR name)
	{
		for (u8 f = eFailMisfire; f <= eFailHardSlide; ++f)
			if (name && !xr_strcmp(name, failure_name(f)))
				return f;
		return eFailNone;
	}

	const SPartData& part_data(const shared_str& section)
	{
		// Keyed by the section string itself: callers hold a node's shared_str, so no lookup in the global
		// string table per call.
		static xr_map<shared_str, SPartData> cache;
		auto it = cache.find(section);
		if (it != cache.end())
			return it->second;

		SPartData d;
		read_part_data(section.c_str(), d);
		return cache.insert(mk_pair(section, d)).first->second;
	}

	const SPartData& part_data(LPCSTR section)
	{
		return part_data(shared_str(section ? section : ""));
	}

	namespace
	{
		SSettings& settings_rw()
		{
			static SSettings s;
			static bool loaded = false;
			if (!loaded)
			{
				LPCSTR sect = "gunsmith_settings";
				s.optic_base_fov = READ_IF_EXISTS(pSettings, r_float, sect, "optic_base_fov", 45.f);
				s.aim_roll_min = READ_IF_EXISTS(pSettings, r_float, sect, "aim_roll_min", 1.f);
				s.aim_turn_max = READ_IF_EXISTS(pSettings, r_float, sect, "aim_turn_max", 20.f);
				s.aim_still_turn = READ_IF_EXISTS(pSettings, r_float, sect, "aim_still_turn", 0.1f);
				s.aim_still_move = READ_IF_EXISTS(pSettings, r_float, sect, "aim_still_move", 0.3f);
				s.aim_still_time = READ_IF_EXISTS(pSettings, r_float, sect, "aim_still_time", 0.25f);
				s.aim_speed = clampr(READ_IF_EXISTS(pSettings, r_float, sect, "aim_speed", 6.f), 0.5f, 30.f);
				loaded = true;
			}
			return s;
		}
	}

	const SSettings& settings() { return settings_rw(); }
}
