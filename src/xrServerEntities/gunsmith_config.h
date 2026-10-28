////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_config.h
//	Description : Gunsmith part tree - read-only accessors for part, slot and
//	              receiver LTX data
////////////////////////////////////////////////////////////////////////////

#pragma once

namespace gunsmith
{
	// Legacy addon bits, identical to CSE_ALifeItemWeapon::EWeaponAddonState.
	enum ELegacyAddon
	{
		eLegacyNone = 0,
		eLegacyScope = 0x01,
		eLegacyLauncher = 0x02,
		eLegacySilencer = 0x04,
	};

	// One slot of a receiver or part.
	//
	// Slot section keys:
	//   accepts         = tag, tag, ...   parts fit when their `tags` intersect this list
	//   required        = bool            (default false) gun cannot fire with it empty (enforced from phase 2)
	//   mount_bone      = bone            bone on the owning part's model (used from phase 6)
	//   field_swappable = bool            (default false) editable outside a workbench (framework UI)
	//   legacy_addon    = scope | silencer | launcher
	//                                      this slot stands for one of the old addon flags
	//
	// Implicit slots are synthesized for receivers whose old addon status is
	// "attachable" but which declare no slot for that addon. They have no LTX
	// section and accept any part.
	struct SSlotInfo
	{
		shared_str section;
		xr_vector<shared_str> accepts;
		bool required;
		bool field_swappable;
		u8 legacy_addon;
		bool implicit;

		SSlotInfo() : required(false), field_swappable(false), legacy_addon(eLegacyNone), implicit(false) {}
	};

	typedef xr_vector<SSlotInfo> SLOTS;

	// Receiver keys on the weapon section:
	//   gs_mode          = legacy | full  (default legacy)
	//   gs_slots         = slot, slot, ... root slots
	//   gs_default_build = entry, ...     entry is a part section (placed in the first free
	//                                     slot it fits) or parent:slot:part_section
	// Part keys (on any item section):
	//   part_kind, tags, slots, blocks_slots (plus visual and stat keys used by later phases)

	bool is_full_mode(LPCSTR receiver);

	// Slots a node exposes, in index order. A receiver gets its `gs_slots`, then
	// implicit slots for any attachable legacy addon not already covered. A part gets
	// its `slots`.
	void slots_of(LPCSTR section, bool is_receiver, SLOTS& out);

	void tags_of(LPCSTR part, xr_vector<shared_str>& out);

	// A part fits when its tags intersect the slot's `accepts`. In non-strict mode a
	// slot standing for an old addon (legacy or implicit) also takes any part that
	// is_legacy_part() recognises for that addon on this receiver.
	bool part_fits_slot(LPCSTR part, const SSlotInfo& slot, LPCSTR receiver, bool strict);

	// True when `part` is an old-style addon of this kind for the receiver: a name its
	// addon config lists (silencer_name, grenade_launcher_name, its scope list), or an
	// item of the matching addon class (WP_SILEN, WP_GLAUN, WP_SCOPE).
	bool is_legacy_part(LPCSTR receiver, u8 legacy_addon, LPCSTR part);

	// Scope part sections the receiver can take, in CWeapon::m_scopes order and
	// resolved the way CWeapon::GetScopeName() does.
	void legacy_scope_parts(LPCSTR receiver, xr_vector<shared_str>& out);

	// The part section an old addon flag stands for on this receiver
	// (silencer_name, grenade_launcher_name, or the first scope). Empty if unknown.
	shared_str legacy_default_part(LPCSTR receiver, u8 legacy_addon);

	LPCSTR legacy_addon_name(u8 legacy_addon);
}
