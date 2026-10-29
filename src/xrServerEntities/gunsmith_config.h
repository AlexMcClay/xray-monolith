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
	//   mount_bone      = bone            bone on the owning node's model the part mounts to, or a
	//                                      virtual mount (gs_mount_<name>_pos/_rot on the owner's section)
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
		shared_str mount_bone;
		bool required;
		bool field_swappable;
		u8 legacy_addon;
		bool implicit;

		SSlotInfo() : required(false), field_swappable(false), legacy_addon(eLegacyNone), implicit(false) {}
	};

	typedef xr_vector<SSlotInfo> SLOTS;

	// Receiver keys on the weapon section:
	//   gs_mode          = legacy | full  (default legacy; full-mode guns take no old-style addons)
	//   gs_slots         = slot, slot, ... root slots
	//   gs_default_build = entry, ...     entry is a part section (placed in the first free
	//                                     slot it fits) or parent:slot:part_section, where slot is
	//                                     an index or a slot's mount_bone (e.g. mod_scope); a leading
	//                                     '?' makes an entry optional (skipped quietly if missing)
	// Part keys (on any item section):
	//   part_kind, tags, slots, blocks_slots (plus visual and stat keys used by later phases)

	bool is_full_mode(LPCSTR receiver);

	// Slots a node exposes, in index order. A receiver gets its `gs_slots`, then
	// implicit slots for any attachable legacy addon not already covered (not on full-mode
	// receivers: they take no old-style addons). A part gets
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

	//==========================================================================
	// Phase 2: part stats and durability
	//==========================================================================

	// Stats a part can change. Each has `<name>_add` (default 0) and `<name>_mul`
	// (default 1) keys; the effective value is (base + sum of adds) * product of muls.
	// Adds for fire_dispersion, cam_dispersion and cam_dispersion_inc are in degrees,
	// like the weapon keys they modify. rpm adds are in rounds per minute.
	enum EStat
	{
		eStatFireDispersion,
		eStatCamDispersion,
		eStatCamDispersionInc,
		eStatCamHorz, // horizontal recoil (StepAngleHorz, MaxAngleHorz)
		eStatHitPower,
		eStatHitImpulse,
		eStatBulletSpeed,
		eStatRpm,
		eStatPdmBase,
		eStatPdmAccel,
		eStatPdmVel,
		eStatCrosshairInertion,
		eStatZoomRotateTime, // aim-in time
		eStatConditionShotDec, // receiver wear per shot
		eStatErgonomics,
		eStatCount
	};

	LPCSTR stat_name(u8 stat);

	// How a part fails when worn (`failure` key).
	enum EFailure
	{
		eFailNone = 0,
		eFailMisfire, // receiver: the weapon's own misfire curve
		eFailExtraction, // bolt, carrier, trigger, gas tube: cleared by racking the bolt
		eFailFeed, // magazine: cleared by a full reload
		eFailBarrel, // barrel: cleared by racking the bolt
	};

	LPCSTR failure_name(u8 failure);

	// Everything the engine reads from a part section, cached per section.
	//
	// Durability keys (`part_kind` must be set for any of them to apply; the
	// defaults come from the kind, see kind table in gunsmith_config.cpp, and can
	// be changed per kind in an optional [gs_part_kind_<kind>] section):
	//   wears_on_shot, condition_wear_per_shot, condition_dispersion_factor,
	//   failure, jam_start_condition, jam_end_condition, jam_start_prob, jam_end_prob,
	//   barrel_wear_mul (muzzle), suppression_min_condition (muzzle)
	// Weight and cost: old-style addons count their item `inv_weight` and `cost`,
	// as before. Other parts count `weight` and `part_cost` (default 0), so
	// internal parts that are also items don't add their item price to the gun.
	struct SPartData
	{
		shared_str kind; // part_kind, or inferred from the item class, or "generic"
		bool declared; // part_kind was set: durability applies

		float add[eStatCount];
		float mul[eStatCount];
		bool has_mul[eStatCount];
		float legacy_k[eStatCount]; // old *_k keys, clamped 0.01..2 as the old code did

		float weight, cost; // gunsmith keys
		float legacy_weight, legacy_cost; // item inv_weight / cost

		bool wears_on_shot;
		float wear_per_shot;
		float condition_dispersion_factor;
		u8 failure;
		float jam_start_condition, jam_end_condition, jam_start_prob, jam_end_prob;
		float barrel_wear_mul;
		float suppression_min_condition;

		xr_vector<shared_str> blocks_slots;

		// Need part_kind set:
		bool suppressor; // `suppressor = true`: the gun is suppressed while this part is fitted and not
						 // worn below its suppression_min_condition, as with a silencer addon
		int ammo_mag_size; // `ammo_mag_size`: the gun's magazine size while this part is fitted, -1 if unset
	};

	const SPartData& part_data(LPCSTR section);

	// Jam chance of a part at this condition, from its curve.
	float part_jam_probability(const SPartData& part, float condition);

	// Engine-wide tuning, [gunsmith_settings] in system configs (all optional).
	struct SSettings
	{
		float ergo_base; // receiver ergonomics when the weapon has no `ergonomics` key (50)
		float ergo_aim_k; // aim time scale per ergonomics point below/above 50 (0.5 per 100 points)
		float ergo_sway_k; // movement sway scale, same units (0.5)
		float sight_sway_factor; // extra zoomed sway per point of condition below a sight's threshold (1.0)
		// The camera FOV (degrees) that counts as 1x for optic parts' `magnification` (45: a 4x scope
		// zooms about as far as vanilla's PSO). The player's own FOV when that is narrower.
		float optic_base_fov;
	};

	const SSettings& settings();
}
