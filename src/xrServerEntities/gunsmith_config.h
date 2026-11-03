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

	// One slot of a receiver or part. Keys: see the Gunsmith config reference.
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

	// Receiver keys (gs_mode, gs_slots, gs_default_build) and part keys: see the Gunsmith config reference.
	// Full-mode receivers (gs_mode = full) take no old-style addons.
	bool is_full_mode(LPCSTR receiver);

	// Slots a node exposes, in index order. A receiver gets its `gs_slots`, then implicit slots for any
	// attachable legacy addon not already covered (not on full-mode receivers). A part gets its `slots`.
	// Cached per section: configs don't change while the game runs, so the reference stays valid.
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
	// Part stats
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
		eStatCamRelax, // camera recoil relax speed (cam_relax_speed, zoom_cam_relax_speed)
		eStatCount
	};

	LPCSTR stat_name(u8 stat);

	// A weapon malfunction's type: the receiver's own misfire curve gives eFailMisfire; a script deciding the shot
	// (the weapon_on_shot callback, CWeapon::gs_script_shot) any of them. Every type is cleared by racking the bolt.
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
	// malfunctions per shot (weapon_on_shot).
	// Weight and cost: old-style addons count their item `inv_weight` and `cost`. Other parts count
	// `weight` and `part_cost` (default 0), so internal parts that are also items don't add their item
	// price to the gun.
	struct SPartData
	{
		shared_str kind; // part_kind, or inferred from the item class, or "generic"
		bool declared; // part_kind was set: the keys below that need it apply

		float add[eStatCount];
		float mul[eStatCount];
		bool has_mul[eStatCount];
		float legacy_k[eStatCount]; // addon *_k keys, clamped 0.01..2 as vanilla does

		float weight, cost; // gunsmith keys
		float legacy_weight, legacy_cost; // item inv_weight / cost

		xr_vector<shared_str> blocks_slots;
		// `conflicts`: tags of parts that can't be on the same gun as this one (any tag: a part's own section is
		// one, as are a pack's tags). Checked both ways: either part listing the other is enough.
		xr_vector<shared_str> conflicts;
		xr_vector<shared_str> tags; // `tags`, read once (the conflict check compares them)
		shared_str parent_section; // `parent_section` (a weapon variant's base gun): a conflict naming the base gun covers it

		// Need part_kind set:
		bool suppressor; // `suppressor = true`: the gun is suppressed while this part is fitted, as with a silencer addon
		int ammo_mag_size; // `ammo_mag_size`: the gun's magazine size while this part is fitted, -1 if unset
	};

	const SPartData& part_data(const shared_str& section);
	const SPartData& part_data(LPCSTR section);

	// Engine-wide tuning, [gunsmith_settings] in system configs (all optional). The handling formulas and their
	// settings are the Framework's.
	struct SSettings
	{
		// The camera FOV (degrees) that counts as 1x for optic parts' `magnification` (45: a 4x scope
		// zooms about as far as vanilla's PSO). The player's own FOV when that is narrower.
		float optic_base_fov;
		// Aiming through a part's sight: the sight's roll is corrected from aim_roll_min degrees (1), and its
		// line turned onto the view only when it is less than aim_turn_max degrees off (20; further, the gun isn't
		// in an aiming pose, or the data is bad). The correction follows its target by aim_speed of the remaining
		// difference per second while the gun is idle (20).
		float aim_roll_min, aim_turn_max, aim_speed;
		// Item lights (tactical devices): how many of a holder's lights cast shadows, the strongest first (1;
		// each costs a shadow-map pass); their shadow map's near plane in metres (0.5: the holder's body and gun
		// right behind the lens don't block the light); whether the player casts no shadow while one shines (true,
		// as with the left-hand flashlight).
		u32 device_shadows;
		float device_shadow_near;
		bool device_hide_actor_shadow;
	};

	const SSettings& settings();
	// Changes a setting while the game runs (Lua gunsmith.set_setting, e.g. from MCM). False: unknown name.
	bool set_setting(LPCSTR name, float value);
	bool get_setting(LPCSTR name, float& value);
}
