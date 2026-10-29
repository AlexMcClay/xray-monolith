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
		LPCSTR mode = READ_IF_EXISTS(pSettings, r_string, receiver, "gs_mode", (LPCSTR)nullptr);
		return mode && !xr_strcmp(mode, "full");
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

	void slots_of(LPCSTR section, bool is_receiver, SLOTS& out)
	{
		// Slot lists only depend on configs, which don't change while the game runs,
		// and they are read on every shot from phase 2 on.
		static xr_map<shared_str, SLOTS> cache[2];
		out.clear();
		if (!section || !*section)
			return;

		xr_map<shared_str, SLOTS>& c = cache[is_receiver ? 1 : 0];
		const shared_str key(section);
		auto it = c.find(key);
		if (it != c.end())
		{
			out = it->second;
			return;
		}
		slots_of_uncached(section, is_receiver, out);
		c.insert(mk_pair(key, out));
	}

	void tags_of(LPCSTR part, xr_vector<shared_str>& out)
	{
		read_list(part, "tags", out);
	}

	bool part_fits_slot(LPCSTR part, const SSlotInfo& slot, LPCSTR receiver, bool strict)
	{
		xr_vector<shared_str> tags;
		tags_of(part, tags);
		for (const shared_str& tag : tags)
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
	// Phase 2: part stats and durability
	//==========================================================================

	namespace
	{
		const LPCSTR STAT_NAMES[eStatCount] = {
			"fire_dispersion", "cam_dispersion", "cam_dispersion_inc", "cam_horz",
			"hit_power", "hit_impulse", "bullet_speed", "rpm",
			"pdm_base", "pdm_accel", "pdm_vel", "crosshair_inertion",
			"zoom_rotate_time", "condition_shot_dec", "ergonomics",
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

		u8 parse_failure(LPCSTR value, u8 fallback)
		{
			if (!value) return fallback;
			if (!xr_strcmp(value, "none")) return eFailNone;
			if (!xr_strcmp(value, "misfire")) return eFailMisfire;
			if (!xr_strcmp(value, "extraction")) return eFailExtraction;
			if (!xr_strcmp(value, "feed")) return eFailFeed;
			if (!xr_strcmp(value, "barrel")) return eFailBarrel;
			Msg("! [gunsmith] unknown failure type [%s], using the part kind's default", value);
			return fallback;
		}

		struct SKindDefaults
		{
			bool wears_on_shot;
			float wear_per_shot;
			float condition_dispersion_factor;
			u8 failure;
			float jam_start_condition, jam_end_condition, jam_start_prob, jam_end_prob;
			float barrel_wear_mul;
			float suppression_min_condition;
		};

		// Built-in defaults per part kind. Any of them can be changed for a whole kind
		// in [gs_part_kind_<kind>], and per part in the part's own section.
		SKindDefaults builtin_kind_defaults(LPCSTR kind)
		{
			SKindDefaults d = { false, 0.f, 0.f, eFailNone, 0.5f, 0.1f, 0.f, 0.f, 1.f, 0.f };
			auto is = [kind](LPCSTR k) { return !xr_strcmp(kind, k); };

			if (is("barrel"))
			{
				d.wears_on_shot = true;
				d.wear_per_shot = 0.0004f;
				d.condition_dispersion_factor = 1.f;
				d.failure = eFailBarrel;
				d.jam_end_prob = 0.05f;
			}
			else if (is("bolt") || is("bolt_carrier") || is("trigger") || is("gas_tube"))
			{
				d.wears_on_shot = true;
				d.wear_per_shot = 0.0003f;
				d.failure = eFailExtraction;
				d.jam_end_prob = 0.05f;
			}
			else if (is("magazine"))
			{
				d.wears_on_shot = true;
				d.wear_per_shot = 0.0005f;
				d.failure = eFailFeed;
				d.jam_end_prob = 0.05f;
			}
			else if (is("muzzle"))
			{
				d.wears_on_shot = true;
				d.wear_per_shot = 0.0004f;
				d.suppression_min_condition = 0.3f;
			}
			// sight: jam_start_condition (0.5) is the condition below which it adds zoomed sway.
			// stock, grip, handguard, rail, underbarrel, receiver, generic: no wear, no failure.
			return d;
		}

		SKindDefaults kind_defaults(LPCSTR kind)
		{
			SKindDefaults d = builtin_kind_defaults(kind);
			string128 sect;
			xr_sprintf(sect, "gs_part_kind_%s", kind);
			if (!pSettings->section_exist(sect))
				return d;

			d.wears_on_shot = !!READ_IF_EXISTS(pSettings, r_bool, sect, "wears_on_shot", d.wears_on_shot);
			d.wear_per_shot = READ_IF_EXISTS(pSettings, r_float, sect, "condition_wear_per_shot", d.wear_per_shot);
			d.condition_dispersion_factor = READ_IF_EXISTS(pSettings, r_float, sect, "condition_dispersion_factor", d.condition_dispersion_factor);
			d.failure = parse_failure(READ_IF_EXISTS(pSettings, r_string, sect, "failure", (LPCSTR)nullptr), d.failure);
			d.jam_start_condition = READ_IF_EXISTS(pSettings, r_float, sect, "jam_start_condition", d.jam_start_condition);
			d.jam_end_condition = READ_IF_EXISTS(pSettings, r_float, sect, "jam_end_condition", d.jam_end_condition);
			d.jam_start_prob = READ_IF_EXISTS(pSettings, r_float, sect, "jam_start_prob", d.jam_start_prob);
			d.jam_end_prob = READ_IF_EXISTS(pSettings, r_float, sect, "jam_end_prob", d.jam_end_prob);
			d.barrel_wear_mul = READ_IF_EXISTS(pSettings, r_float, sect, "barrel_wear_mul", d.barrel_wear_mul);
			d.suppression_min_condition = READ_IF_EXISTS(pSettings, r_float, sect, "suppression_min_condition", d.suppression_min_condition);
			return d;
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
				// sight handling, but no durability: the engine alone changes nothing.
				LPCSTR cls = exists ? READ_IF_EXISTS(pSettings, r_string, section, "class", (LPCSTR)nullptr) : nullptr;
				kind = (cls && !xr_strcmp(cls, "WP_SILEN")) ? "muzzle"
					: (cls && !xr_strcmp(cls, "WP_SCOPE")) ? "sight"
					: (cls && !xr_strcmp(cls, "WP_GLAUN")) ? "underbarrel"
					: "generic";
			}
			d.kind = kind;

			SKindDefaults k = d.declared ? kind_defaults(kind) : builtin_kind_defaults("generic");
			if (!d.declared)
				k.jam_start_condition = 0.f; // no sight sway either

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
			}

			d.suppressor = exists && d.declared && !!READ_IF_EXISTS(pSettings, r_bool, section, "suppressor", FALSE);
			d.ammo_mag_size = exists && d.declared ? READ_IF_EXISTS(pSettings, r_s32, section, "ammo_mag_size", -1) : -1;

			d.wears_on_shot = k.wears_on_shot;
			d.wear_per_shot = k.wear_per_shot;
			d.condition_dispersion_factor = k.condition_dispersion_factor;
			d.failure = k.failure;
			d.jam_start_condition = k.jam_start_condition;
			d.jam_end_condition = k.jam_end_condition;
			d.jam_start_prob = k.jam_start_prob;
			d.jam_end_prob = k.jam_end_prob;
			d.barrel_wear_mul = k.barrel_wear_mul;
			d.suppression_min_condition = k.suppression_min_condition;

			if (exists && d.declared)
			{
				d.wears_on_shot = !!READ_IF_EXISTS(pSettings, r_bool, section, "wears_on_shot", d.wears_on_shot);
				d.wear_per_shot = READ_IF_EXISTS(pSettings, r_float, section, "condition_wear_per_shot", d.wear_per_shot);
				d.condition_dispersion_factor = READ_IF_EXISTS(pSettings, r_float, section, "condition_dispersion_factor", d.condition_dispersion_factor);
				d.failure = parse_failure(READ_IF_EXISTS(pSettings, r_string, section, "failure", (LPCSTR)nullptr), d.failure);
				d.jam_start_condition = READ_IF_EXISTS(pSettings, r_float, section, "jam_start_condition", d.jam_start_condition);
				d.jam_end_condition = READ_IF_EXISTS(pSettings, r_float, section, "jam_end_condition", d.jam_end_condition);
				d.jam_start_prob = READ_IF_EXISTS(pSettings, r_float, section, "jam_start_prob", d.jam_start_prob);
				d.jam_end_prob = READ_IF_EXISTS(pSettings, r_float, section, "jam_end_prob", d.jam_end_prob);
				d.barrel_wear_mul = READ_IF_EXISTS(pSettings, r_float, section, "barrel_wear_mul", d.barrel_wear_mul);
				d.suppression_min_condition = READ_IF_EXISTS(pSettings, r_float, section, "suppression_min_condition", d.suppression_min_condition);
			}
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
		default: return "none";
		}
	}

	const SPartData& part_data(LPCSTR section)
	{
		static xr_map<shared_str, SPartData> cache;
		const shared_str key(section ? section : "");
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;

		SPartData d;
		read_part_data(section, d);
		return cache.insert(mk_pair(key, d)).first->second;
	}

	float part_jam_probability(const SPartData& part, float condition)
	{
		if (part.failure == eFailNone)
			return 0.f;
		if (condition > part.jam_start_condition)
			return 0.f;
		if (condition < part.jam_end_condition)
			return part.jam_end_prob;
		const float span = part.jam_start_condition - part.jam_end_condition;
		if (span <= EPS)
			return part.jam_end_prob;
		const float t = (part.jam_start_condition - condition) / span;
		return part.jam_start_prob + t * (part.jam_end_prob - part.jam_start_prob);
	}

	const SSettings& settings()
	{
		static SSettings s;
		static bool loaded = false;
		if (!loaded)
		{
			LPCSTR sect = "gunsmith_settings";
			s.ergo_base = READ_IF_EXISTS(pSettings, r_float, sect, "ergo_base", 50.f);
			s.ergo_aim_k = READ_IF_EXISTS(pSettings, r_float, sect, "ergo_aim_k", 0.5f);
			s.ergo_sway_k = READ_IF_EXISTS(pSettings, r_float, sect, "ergo_sway_k", 0.5f);
			s.sight_sway_factor = READ_IF_EXISTS(pSettings, r_float, sect, "sight_sway_factor", 1.0f);
			s.optic_base_fov = READ_IF_EXISTS(pSettings, r_float, sect, "optic_base_fov", 45.f);
			loaded = true;
		}
		return s;
	}
}
