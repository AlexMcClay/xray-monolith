////////////////////////////////////////////////////////////////////////////
//	Module 		: Weapon_gunsmith_members.h
//	Description : CWeapon's Gunsmith members: the part tree, stats, part models, devices and aiming
////////////////////////////////////////////////////////////////////////////
//
// Included INSIDE class CWeapon (Weapon.h), nowhere else, so Weapon.h stays a few lines from upstream's.
// The bodies are in Weapon_gunsmith.cpp. Starts public; ends protected, as Weapon.h continues.
// No include guard: it is part of the class body.

public:
	// Gunsmith part tree. The server entity owns the saved copy; this mirror is
	// filled in net_Spawn and pushed back after every client-side change.
	const gunsmith::CTree& gs_tree() const { return m_gs_tree; }
	CSE_ALifeItemWeapon* gs_server_entity() const;
	void gs_sync_to_server();

	// Stat modifiers from the parts. Read sites use gs_stats().apply(stat, base).
	const gunsmith::SWeaponStats& gs_stats() const { return m_gs_stats; }
	// Recomputes gs_stats() from the tree; `notify` re-applies suppressed sound and
	// particles when a suppressor part comes on or off.
	void gs_rebuild_stats(bool notify = true);

	// A suppressor is fitted: a silencer addon, or a part with `suppressor = true`.
	// Gameplay checks (shot sound, tracers, AI hearing) use this; IsSilencerAttached() stays the addon flag.
	bool IsSuppressorFitted() const { return IsSilencerAttached() || m_gs_stats.suppressor_part; }

	// Fire blocked by a script (game_object:set_fire_blocked; not saved): the trigger only clicks, for the player
	// and NPCs.
	void SetFireBlocked(bool v) { m_fire_blocked = v; }
	bool IsFireBlocked() const { return m_fire_blocked; }

	// Rounds shown in first person on a gun with round mounts (gunsmith::SRoundShow): by default the rounds loaded, the
	// top one in the chamber when the HUD has one and the rest in the magazine. A script sets what shows for an
	// animation's span (game_object:set_hud_rounds; not saved): the magazine's count, the chamber and the ejection port
	// (0 nothing, 1 a round, 2 a fired case), each -1 for the default, and the ammo section of the magazine's rounds
	// ("": the loaded ones, then the loaded type).
	void SetHudRounds(int mag, int chamber, int port, LPCSTR section);
	// Each chamber and port mount (a cylinder's, a double barrel's: the HUD section's gs_round_chamber and
	// gs_round_port lists), by a digit each: 0 nothing, 1 a round, 2 a fired case, any other character (or past the
	// end) as SetHudRounds says (game_object:set_hud_round_slots; SetHudRounds(-1, -1, -1) clears these too).
	void SetHudRoundSlots(LPCSTR chambers, LPCSTR ports);

	// Vanilla readings for scripts. Weight() and Cost() count the fitted parts; these leave them out, as an
	// unmodded exe's would (old-style addons counted).
	float GetVanillaWeight() const { return Weight() - m_gs_stats.weight_add; }
	u32 GetVanillaCost() const;

	float gs_pdm_base() const;
	float gs_pdm_vel() const;
	float gs_pdm_accel() const;
	// Seconds per shot after rpm modifiers.
	float gs_shot_time(float base_time) const;

	// Which part caused the current jam (gunsmith::EFailure and node index).
	u8 gs_jam_type() const { return m_gs_jam_type; }
	u8 gs_jam_node() const { return m_gs_jam_node; }

	// Lua API support (script_game_object_gunsmith.cpp).
	// Why `section` can't go into `slot` of node `parent` of `tree`, or "" when it can.
	// In an addon slot only what this weapon's addon code accepts fits, unless
	// `preview` (stats only), where any old-style addon of that type fits.
	LPCSTR gs_check_place(const gunsmith::CTree& tree, LPCSTR section, u8 parent, u8 slot, bool preview) const;
	// Puts a part into a free non-addon slot and fires on_gs_part_attached.
	bool gs_attach_part(LPCSTR section, u8 parent, u8 slot);
	// Removes a part and its children. With `spawn`, each comes back as an inventory
	// item; `item_id` is the first one, or 0xffff.
	bool gs_detach_part(u8 node, bool spawn, u16& item_id);
	// Spawns a part section as an item next to (or in the inventory of) the weapon's
	// owner, for part items with the parts mounted on it. Returns its id, or 0xffff.
	u16 gs_spawn_part_item(LPCSTR section, const gunsmith::CTree* subtree = nullptr);
	// Puts an inventory item into a free non-addon slot, with any parts it carries.
	// Fires on_gs_part_attached. The caller destroys the item.
	bool gs_attach_item(CInventoryItem* item, u8 parent, u8 slot);
	// Stats a tree would give this weapon (the live tree, or a preview copy), without the scripts' layer;
	// with_addons: old-style addons counted (gunsmith_stats.h).
	void gs_compute_stats(const gunsmith::CTree& tree, gunsmith::SWeaponStats& out, bool with_addons) const;
	// The live stats with old-style addons counted, for display (gs_get_stats).
	void gs_display_stats(gunsmith::SWeaponStats& out) const;
	// The scripts' layer of `stats` (script_add, script_mul, ergonomics, no_stock): the weapon_on_recalc_stats
	// callback, through _G.CWeapon_OnRecalcStats (gunsmith_callbacks.script), given `tree`'s part sections and the
	// stock build's; `preview` for a build that isn't the weapon's (the builder). Runs on every full recompute and
	// preview, not per shot.
	void gs_script_stats(const gunsmith::CTree& tree, gunsmith::SWeaponStats& stats, bool preview);
	// Attached old-style addons the tree has no slot for (e.g. made attachable by an upgrade); they count as parts.
	void gs_extra_parts(const gunsmith::CTree& tree, xr_vector<gunsmith::SExtraPart>& extra) const;
	// One stat in display units (degrees for angles, rounds per minute for rpm):
	// the weapon's own value, the parts' add, and the effective value.
	void gs_stat_values(const gunsmith::SWeaponStats& stats, u8 stat, float& base, float& add, float& value) const;

	// Part models (gunsmith_visuals.h), for receivers with gs_mode = full: every part whose
	// slot has a mount_bone is drawn as its own model on its parent, in the world and on the HUD.
	bool gs_full_mode() const { return m_gs_full_mode; }
	gunsmith::CVisuals& gs_visuals() { return m_gs_visuals; }
	virtual void RenderAttachments();
	virtual void render_hud_attachments(IKinematics* model, const Fmatrix& xform);
	virtual bool hud_fire_point(IKinematics* model, const Fmatrix& xform, Fvector& pos);
	virtual void DeleteHudItemData();

	// The sight the weapon aims through (the selected sight, else the highest aim_priority in the tree,
	// ties: the last; gunsmith::sight_node), and its section; null without one. gs_optic(): the same
	// when it magnifies and no vanilla scope addon is fitted (that one keeps its own zoom).
	const gunsmith::SPartVisual* gs_sight_part(shared_str* section = nullptr) const;
	const gunsmith::SPartVisual* gs_optic() const;
	// What aiming uses for that part's chosen sight and mode (gunsmith::sight_aim): false without a sight.
	bool gs_sight_aim(gunsmith::SSightAim& out) const;

	// Sight modes: the sight aimed through's current mode (count: how many it has).
	// Saved in the part's node mode.
	u8 gs_sight_mode(u8* count = nullptr) const;

	// Sight switching: the fitted cyclable sights (optics and red dots, in tree order), the node aimed
	// through (0 = none), a given one (false if not cyclable).
	// The choice is saved in node 0's mode (bits 1-7); it follows its part when other parts come and go.
	void gs_sight_list(xr_vector<u8>& out) const;
	u8 gs_sight_node() const;
	bool gs_sight_select(u8 node);
	// Aim through a fitted sight with a given sight of it (a dual optic's 0 or 1) and mode (0-based); false if it
	// isn't a cyclable sight or either is out of range. The primitive scripts build sight switching on.
	bool gs_sight_set(u8 node, u8 sight, u8 mode);

	// Tactical devices (gunsmith_tactical.h): flashlights and lasers fitted as parts, any number,
	// switched together. The state lives in the tree (saved with it): bit 0 of node 0's mode is the on
	// flag (bits 1-7: the selected sight), each device node's mode its selected mode.
	bool gs_devices_on() const { return m_gs_tree.devices_on(); }
	bool gs_has_devices() const;
	// Every device on or off; false without devices.
	bool gs_devices_set_on(bool on);
	bool gs_set_device_mode(u8 node, u8 mode);
	// A device light (not just a laser) shines and the player's shadow should go (device_hide_actor_shadow): CActor::AllowActorShadow.
	bool gs_devices_lit() const { return m_gs_devices.lit() && gunsmith::settings().device_hide_actor_shadow; }

protected:
	gunsmith::CTacticalDevices m_gs_devices;
	void gs_update_devices();
	void gs_update_npc_devices(CObject* holder);

public:
	void gs_render_world(bool from_actor = false);

protected:
	// Aiming through a fitted sight part: corrects UpdateHudAdditional's aim offset (Weapon_gunsmith.cpp) for the
	// aim offset index `idx`. Called every frame; it also keeps the camera of the last frame.
	void gs_update_aim(attachable_hud_item* hi, const Fmatrix& trans, u8 idx,
		Fvector& curr_offs, const Fvector& curr_rot, Fvector& curr_aim_rot);

	// Hooks from Weapon.cpp and WeaponMagazined.cpp (Weapon_gunsmith.cpp).
	// Third-person fire point from the parts on the muzzle chain (`parent`: the weapon's XFORM).
	void gs_world_fire_point(const Fmatrix& parent);
	// Main sight zoom of a Gunsmith optic: true when it set the zoom parameters (and `step_count`).
	bool gs_zoom_params(float zoom_multiple, u32& step_count);
	// Main sight scope texture of a Gunsmith optic: true when it set `tex` (empty: none).
	bool gs_scope_texture(shared_str& tex);
	// Load: the weapon's own magazine size and, for full-mode guns, no old-style addons.
	void gs_load(LPCSTR section);
	// net_Spawn: the tree from the server entity (before the modular scope pick), then the stats (after it).
	void gs_on_spawn(CSE_ALifeItemWeapon* E);
	void gs_after_spawn();
	// A malfunction a script decided for this shot (eFailNone: none): the jam recorded, the shooting stopped.
	BOOL gs_misfire(u8 failure, u8 node);
	void gs_set_jam(u8 type, u8 node) { m_gs_jam_type = type; m_gs_jam_node = node; }
	// FireStart: fire blocked by a script (SetFireBlocked), the gun clicks instead of firing (a jam still fires as one).
	bool gs_fire_blocked();

	gunsmith::CVisuals m_gs_visuals;
	bool m_gs_full_mode = false;
	// Aim correction (in the aim offset's frame) that keeps the fitted sight on the camera axis, and the sight it
	// is for: set at once for a new sight, then following its target (gs_update_aim) while the gun is idle.
	Fvector m_gs_aim_corr = { 0.f, 0.f, 0.f };
	gunsmith::CVisuals::SSightKey m_gs_aim_sight;
	// The aim rotation (H, P, B) that lays the sight's line along the view: the sight's pitch and yaw
	// off the camera axis and, from aim_roll_min degrees, its roll (a canted sight). Follows with m_gs_aim_corr.
	Fvector m_gs_aim_rot = { 0.f, 0.f, 0.f };
	// The HUD has been placed once (m_item_transform holds a pose: last frame's).
	bool m_gs_hud_placed = false;
	// The magnifying sight the zoom was set up for; gs_update_optic() follows tree changes.
	shared_str m_gs_optic_sect;
	void gs_update_optic();

	// Magazine parts (`ammo_mag_size`) set the magazine size; the weapon's own comes back when
	// none is fitted. `live`: a change in play, so rounds that no longer fit are returned.
	int m_gs_base_mag_size = -1;
	int m_gs_applied_mag_size = -1;
	virtual void gs_apply_mag_size(int size, bool live) { iMagazineSize = size; }

	// A part's `tri_state_reload` sets how a gun with that weapon key loads while fitted (a detachable magazine on a
	// gun loaded round by round); the section's own comes back without one. Not changed mid-reload.
	void gs_apply_tri_state();

	// Launcher parts (`grenade_launcher = true`): on a gun of the grenade launcher class the launcher is attached
	// (IsGrenadeLauncherAttached) while one is fitted. gs_set_launcher: the fitted launcher part (empty: none);
	// `live`: a change in play (its grenades come out when it goes).
	bool m_gs_launcher = false;
	virtual void gs_set_launcher(const shared_str& part, bool live) {}

public:
	// The left hand may take the fitted grip (grip IK; the grenade launcher's mode lets the animation have it).
	virtual bool gs_grip_ik_allowed() const { return true; }
	// The second fire point (the grenade launcher's): the fitted launcher part's `fire_point2`, if any.
	virtual bool hud_fire_point2(IKinematics* model, const Fmatrix& xform, Fvector& pos);

protected:

protected:
	bool m_gs_api_detach = false; // gs_detach_part fires the detach callback itself
	// Why `section` can't go into `slot` of node `parent` through the Lua API or the inventory (not an addon slot),
	// logged; false when it can.
	bool gs_refuse_place(LPCSTR section, u8 parent, u8 slot) const;
	// After a change to the tree: the stats rebuilt (`notify` as gs_rebuild_stats), the server copy updated.
	void gs_commit_tree(bool notify = true);
	bool gs_legacy_accepts(u8 legacy_addon, LPCSTR section) const;
	void gs_callback_attached(u8 node, LPCSTR section);
	void gs_callback_detached(LPCSTR section, int item_id);
	void gs_callback_failed(u8 node, u8 failure);

protected:
	gunsmith::CTree m_gs_tree;
	gunsmith::SWeaponStats m_gs_stats;
	float m_gs_stock_parts_cost = 0.f; // stock_parts_cost: the parts' price the section's cost already counts (Cost)
	u8 m_gs_jam_type = 0;
	u8 m_gs_jam_node = 0;
	bool m_fire_blocked = false;
	int m_hud_rounds_mag = -1, m_hud_rounds_chamber = -1, m_hud_rounds_port = -1; // SetHudRounds (-1: the default)
	shared_str m_hud_rounds_sect;
	xr_string m_hud_round_chambers, m_hud_round_ports; // SetHudRoundSlots
	gunsmith::SRoundShow m_gs_round_show; // (kept: its section list's memory is reused every frame)
	// What the rounds show now (SetHudRounds over the rounds loaded) on the HUD's chamber and port mounts.
	void gs_round_show(gunsmith::SRoundShow& out, u32 chambers, u32 ports) const;
	// The rounds the gun shows and their loaded type (a grenade launcher's mode: the rifle's, not the grenades).
	virtual const xr_vector<CCartridge>& gs_round_magazine(shared_str& loaded_type) const
	{
		loaded_type = m_ammoType < m_ammoTypes.size() ? m_ammoTypes[m_ammoType] : shared_str();
		return m_magazine;
	}

	// Per-shot wear: the weapon_on_shot callback's for this shot when a script took it (CheckForMisfire asks
	// before the shot), times the wear setting; else the receiver's own (condition_shot_dec, the cartridge's
	// impair). Parts don't wear.
	void gs_apply_shot_wear(float impair);
	// The weapon_on_shot callback (_G.CWeapon_OnShot in gunsmith_callbacks.script) for the shot about to be fired,
	// the player's or an NPC's: true when a script took it, with its malfunction (`failure`, gunsmith::EFailure;
	// eFailNone for none) and culprit node, and its wear in m_gs_shot_wear.
	bool gs_script_shot(u8& failure, u8& node);
	float m_gs_shot_wear = -1.f; // this shot's wear from a script (condition 0..1, before the wear setting); -1: none
	virtual void gs_modify_bullet(float& speed, float& hit_power, float& impulse);
	// Switches shot sound, particles and lights between suppressed and not.
	virtual void gs_on_suppression_changed() {}

	// Mirrors an attach (part != null) or detach of an old-style addon into the tree.
	void gs_set_legacy_addon(u8 legacy_addon, LPCSTR part);
	// Makes the tree's scope part match the scope the weapon actually uses (m_cur_scope).
	void gs_match_scope_part();
