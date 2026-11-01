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
	// The cached slot list of a section. Configs don't change while the game runs, so the reference stays valid.
	const SLOTS& slots_of(const shared_str& section, bool is_receiver);
	const SLOTS& slots_of(LPCSTR section, bool is_receiver);

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
	// Phase 2: part stats
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

	// A weapon malfunction's type: the receiver's own misfire curve gives eFailMisfire; a script deciding the shot
	// (the weapon_on_shot callback, CWeapon::gs_script_shot) any of them. Every type is cleared by racking the bolt.
	// Parts had their own types and jam curves until 2 Nov 2026 (Gunsmith Docs 25).
	enum EFailure
	{
		eFailNone = 0,
		eFailMisfire, // "misfire"
		eFailExtraction, // "extraction": a failure to eject (Tarkov's Jam)
		eFailFeed, // "feed": a failure to feed
		eFailBarrel, // "barrel": the slide or bolt not going home (Tarkov's SoftSlide)
		eFailHardSlide, // "hard_slide": the same, stuck harder (Tarkov's HardSlide)
	};

	LPCSTR failure_name(u8 failure);
	// The type of a name above; eFailNone for "none" or an unknown name.
	u8 failure_from_name(LPCSTR name);

	// Everything the engine reads from a part section, cached per section.
	//
	// Parts have no condition: only the weapon's condition wears, and a script may decide its wear and
	// malfunctions per shot (weapon_on_shot; Gunsmith Docs 25).
	// Weight and cost: old-style addons count their item `inv_weight` and `cost`,
	// as before. Other parts count `weight` and `part_cost` (default 0), so
	// internal parts that are also items don't add their item price to the gun.
	struct SPartData
	{
		shared_str kind; // part_kind, or inferred from the item class, or "generic"
		bool declared; // part_kind was set: the keys below that need it apply

		float add[eStatCount];
		float mul[eStatCount];
		bool has_mul[eStatCount];
		float legacy_k[eStatCount]; // old *_k keys, clamped 0.01..2 as the old code did

		float weight, cost; // gunsmith keys
		float legacy_weight, legacy_cost; // item inv_weight / cost

		xr_vector<shared_str> blocks_slots;

		// Handling, as Tarkov (Gunsmith Docs 02):
		float recoil; // `recoil`: Tarkov's recoil percent (negative = less); a gun sums its parts' values
		bool shoulder_contact; // `shoulder_contact = true`: a stock the shooter's shoulder rests on

		// Need part_kind set:
		bool suppressor; // `suppressor = true`: the gun is suppressed while this part is fitted, as with a silencer addon
		int ammo_mag_size; // `ammo_mag_size`: the gun's magazine size while this part is fitted, -1 if unset
	};

	const SPartData& part_data(const shared_str& section);
	const SPartData& part_data(LPCSTR section);

	// Engine-wide tuning, [gunsmith_settings] in system configs (all optional). Some can also be changed while
	// playing (set_setting; the Framework's MCM page does). The handling formulas' settings (recoil_strength,
	// no_stock_recoil, ergo_strength, ergo_aim_k, ergo_sway_k) are the Framework's since 1 Nov 2026.
	struct SSettings
	{
		float ergo_base; // receiver ergonomics when the weapon has no `ergonomics` key (50)
		float recoil_recovery; // camera recoil relax speed multiplier on full-mode guns (1.0 = vanilla)
		// The camera FOV (degrees) that counts as 1x for optic parts' `magnification` (45: a 4x scope
		// zooms about as far as vanilla's PSO). The player's own FOV when that is narrower.
		float optic_base_fov;
		// Aiming through a part's sight (Gunsmith Docs 13, "Aim direction"): the sight's roll is corrected from
		// aim_roll_min degrees (1), and its line turned onto the view only when it is less than aim_turn_max
		// degrees off (20; more is bad data). The correction is refined while the camera turns slower than
		// aim_still_turn rad/s (0.1) and moves slower than aim_still_move m/s (0.3) for aim_still_time s (0.25).
		float aim_roll_min, aim_turn_max, aim_still_turn, aim_still_move, aim_still_time;
	};

	const SSettings& settings();
	// Settings by name (recoil_recovery). False for an unknown name. Takes effect at once.
	bool set_setting(LPCSTR name, float value);
}
