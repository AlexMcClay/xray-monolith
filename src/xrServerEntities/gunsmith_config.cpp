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

	void slots_of(LPCSTR section, bool is_receiver, SLOTS& out)
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

		if (!is_receiver)
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
}
