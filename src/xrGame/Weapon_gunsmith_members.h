////////////////////////////////////////////////////////////////////////////
//	Module 		: Weapon_gunsmith_members.h
//	Description : CWeapon's Gunsmith members: the part tree, stats, part models, devices and aiming
////////////////////////////////////////////////////////////////////////////
//
// Included INSIDE class CWeapon (Weapon.h), nowhere else, so Weapon.h stays a few lines from upstream's
// (1 Nov 2026). The bodies are in Weapon_gunsmith.cpp. Starts public; ends protected, as Weapon.h continues.
// No include guard: it is part of the class body.

public:
	// Gunsmith part tree. The server entity owns the saved copy; this mirror is
	// filled in net_Spawn and pushed back after every client-side change.
	const gunsmith::CTree& gs_tree() const { return m_gs_tree; }
	CSE_ALifeItemWeapon* gs_server_entity() const;
	void gs_sync_to_server();
	// After a shot's wear: the server copy takes the part conditions only, when it holds the same parts
	void gs_sync_conditions_to_server();

	// Stat modifiers from the parts. Read sites use gs_stats().apply(stat, base).
	const gunsmith::SWeaponStats& gs_stats() const { return m_gs_stats; }
	// Recomputes gs_stats() from the tree; `notify` re-applies suppressed sound and
	// particles when a worn suppressor crosses its threshold.
	void gs_rebuild_stats(bool notify = true);
	// After a shot's wear (only part conditions changed): refolds the condition terms onto the cached structural
	// stats; a full gs_rebuild_stats when the parts, addon flags or scope changed since they were made
	void gs_refresh_condition_stats();
	// gs_stats_check 1: compares the refolded stats with a full rebuild after every shot, logging differences
	void gs_check_stats() const;

	// A silencer is attached and not worn below its suppression threshold.
	bool IsSuppressed() const;
	// A suppressor is fitted, worn or not: a silencer addon, or a part with `suppressor = true`.
	// Gameplay checks (shot sound, AI hearing) use this; IsSilencerAttached() stays the addon flag.
	bool IsSuppressorFitted() const { return IsSilencerAttached() || m_gs_stats.suppressor_part; }

	// Vanilla readings for scripts (Gunsmith Docs 24, "Vanilla readings"). Weight() and Cost() count every fitted
	// part; these are what an unmodded exe's would give: the parts left out, the old-style addons counted.
	float GetVanillaWeight() const;
	u32 GetVanillaCost() const;
	// The script values Get_/Set_<Silencer|Scope|Launcher>_<PDM_Base|PDM_Vel|PDM_Accel|CrosshairInertion>
	// (WeaponAK74.cpp). The stats already fold an attached addon's *_k keys in, so cur_*_koef keeps only a script's
	// factor on top (1 after InitAddons). Scripts read and set the vanilla value, the addon's multiplier times that
	// factor, so a set value takes effect as it would without Gunsmith, and InitAddons resets it as before.
	enum EAddonKind { eAddonKindSilencer, eAddonKindScope, eAddonKindLauncher };
	enum EAddonKoef { eAddonKoefPdmBase, eAddonKoefPdmVel, eAddonKoefPdmAccel, eAddonKoefCrosshairInertion };
	float GetAddonKoefScript(u8 addon, u8 koef) const;
	void SetAddonKoefScript(u8 addon, u8 koef, float value);

	float gs_pdm_base() const;
	float gs_pdm_vel() const;
	float gs_pdm_accel() const;
	// Seconds per shot after rpm modifiers.
	float gs_shot_time(float base_time) const;

	// Which part caused the current jam (gunsmith::EFailure and node index).
	u8 gs_jam_type() const { return m_gs_jam_type; }
	u8 gs_jam_node() const { return m_gs_jam_node; }

	// The gun and every part at one condition (Lua gs_set_parts_condition: found and spawned guns).
	void gs_set_parts_condition(float value);
	// Debug and test helpers for the gs_* console commands.
	bool gs_debug_set_condition(u8 node, float value);
	bool gs_debug_set_part(u8 parent, u8 slot, LPCSTR section);
	void gs_dump_stats() const;

	// Lua API support (script_game_object_gunsmith.cpp).
	// Why `section` can't go into `slot` of node `parent` of `tree`, or "" when it can.
	// In an addon slot only what this weapon's addon code accepts fits, unless
	// `preview` (stats only), where any old-style addon of that type fits.
	LPCSTR gs_check_place(const gunsmith::CTree& tree, LPCSTR section, u8 parent, u8 slot, bool preview) const;
	// Puts a part into a free non-addon slot and fires on_gs_part_attached.
	bool gs_attach_part(LPCSTR section, u8 parent, u8 slot, float condition);
	// Removes a part and its children. With `spawn`, each comes back as an inventory
	// item with its condition; `item_id` is the first one, or 0xffff.
	bool gs_detach_part(u8 node, bool spawn, u16& item_id);
	// Spawns a part section as an item next to (or in the inventory of) the weapon's
	// owner, with the given condition and, for part items, the parts mounted on it.
	// Returns its id, or 0xffff.
	u16 gs_spawn_part_item(LPCSTR section, float condition, const gunsmith::CTree* subtree = nullptr);
	// Puts an inventory item into a free non-addon slot, with any parts it carries.
	// Fires on_gs_part_attached. The caller destroys the item.
	bool gs_attach_item(CInventoryItem* item, u8 parent, u8 slot);
	// Stats a tree would give this weapon (the live tree, or a preview copy), without the scripts' layer.
	void gs_compute_stats(const gunsmith::CTree& tree, gunsmith::SWeaponStats& out, bool conditions = true) const;
	// The scripts' layer of `stats` (script_add, script_mul): the weapon_on_recalc_stats callback, through
	// _G.CWeapon_OnRecalcStats (gunsmith_callbacks.script), given the parts' totals; `preview` for a build that
	// isn't the weapon's (the builder). Runs on every full recompute and preview, not per shot.
	void gs_script_stats(gunsmith::SWeaponStats& stats, bool preview);
	// One stat in display units (degrees for angles, rounds per minute for rpm):
	// the weapon's own value, the parts' add, and the effective value.
	void gs_stat_values(const gunsmith::SWeaponStats& stats, u8 stat, float& base, float& add, float& value) const;

	// Part models (gunsmith_visuals.h), for receivers with gs_mode = full: every part whose
	// slot has a mount_bone is drawn as its own model on its parent, in the world and on the HUD.
	bool gs_full_mode() const { return m_gs_full_mode; }
	gunsmith::CVisuals& gs_visuals() { return m_gs_visuals; }
	// The world model's part models: tree node and transform in the receiver model's space.
	void gs_world_layout(xr_vector<std::pair<u8, Fmatrix>>& out);
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

	// Sight modes (Gunsmith Docs 13): the sight aimed through's next mode (false when it has only one),
	// and its current mode (count: how many it has). Saved in the part's node mode.
	bool gs_sight_mode_next();
	u8 gs_sight_mode(u8* count = nullptr) const;

	// Sight switching: the fitted cyclable sights (optics and red dots, in tree order), the node aimed
	// through (0 = none), the next one (false with fewer than two), a given one (false if not cyclable).
	// The choice is saved in node 0's mode (bits 1-7); it follows its part when other parts come and go.
	void gs_sight_list(xr_vector<u8>& out) const;
	u8 gs_sight_node() const;
	bool gs_sight_next();
	bool gs_sight_select(u8 node);
	// Aim through a fitted sight with a given sight of it (a dual optic's 0 or 1) and mode (0-based); false if it
	// isn't a cyclable sight or either is out of range. The primitive scripts build sight switching on;
	// gs_sight_next and gs_sight_mode_next are the default policy (V and H without a script).
	bool gs_sight_set(u8 node, u8 sight, u8 mode);
	// Back to automatic (the best sight). Removing parts doesn't need it: CTree::remove_subtree renumbers the
	// choice with the nodes, and drops it only when the chosen sight itself comes off.
	void gs_sight_forget() { m_gs_tree.set_selected_sight(0); }

	// Tactical devices (gunsmith_tactical.h): flashlights and lasers fitted as parts, any number,
	// switched together. The state lives in the tree (saved with it): bit 0 of node 0's mode is the on
	// flag (bits 1-7: the selected sight), each device node's mode its selected mode.
	bool gs_devices_on() const { return m_gs_tree.devices_on(); }
	bool gs_has_devices() const;
	// Toggles every device; returns the new state.
	bool gs_devices_toggle();
	// Every device on or off; false without devices.
	bool gs_devices_set_on(bool on);
	// Every device to its next mode (each within its own modes).
	void gs_devices_next_mode();
	bool gs_set_device_mode(u8 node, u8 mode);
	// A device light (not just a laser) is shining in first person or on the world model.
	bool gs_devices_lit() const { return m_gs_devices.lit(); }

protected:
	gunsmith::CTacticalDevices m_gs_devices;
	void gs_update_devices();
	// Aiming through a fitted sight part: corrects UpdateHudAdditional's aim offset (Weapon_gunsmith.cpp)
	void gs_update_aim(attachable_hud_item* hi, const Fmatrix& trans, const Fmatrix& gs_prev_cam, bool gs_prev_cam_ok,
		Fvector& curr_offs, const Fvector& curr_rot, Fvector& curr_aim_rot);

	gunsmith::CVisuals m_gs_visuals;
	bool m_gs_full_mode = false;
	// Aim correction (camera space) that keeps the fitted sight on the camera axis, and the sight
	// it was made for. Predicted for the aimed pose when a sight is first aimed through (so it is
	// right on the first aim), then refined while aiming steadily.
	Fvector m_gs_aim_corr = { 0.f, 0.f, 0.f };
	float m_gs_aim_still = 0.f; // seconds the camera has been still (aim refinement waits for it)
	shared_str m_gs_aim_sight;
	// The aim rotation (H, P, B) that lays the sight's line along the view: the sight's pitch and yaw
	// off the camera axis and, from 1 degree, its roll (a canted sight). Predicted with m_gs_aim_corr.
	Fvector m_gs_aim_rot = { 0.f, 0.f, 0.f };
	// The camera the HUD was placed with last frame (m_item_transform is last frame's).
	Fmatrix m_gs_prev_cam;
	bool m_gs_prev_cam_ok = false;
	// The magnifying sight the zoom was set up for; gs_update_optic() follows tree changes.
	shared_str m_gs_optic_sect;
	void gs_update_optic();

	// Magazine parts (`ammo_mag_size`) set the magazine size; the weapon's own comes back when
	// none is fitted. `live`: a change in play, so rounds that no longer fit are returned.
	int m_gs_base_mag_size = -1;
	int m_gs_applied_mag_size = -1;
	virtual void gs_apply_mag_size(int size, bool live) { iMagazineSize = size; }

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
	// m_gs_stats without the part-condition terms, which a shot refolds (gs_refresh_condition_stats), and what it
	// was made for: the tree's layout revision, the addon flags and the scope
	gunsmith::SWeaponStats m_gs_struct_stats;
	u32 m_gs_struct_rev = 0;
	u8 m_gs_struct_flags = 0;
	u8 m_gs_struct_scope = 0;
	bool m_gs_struct_valid = false;
	u8 m_gs_jam_type = 0;
	u8 m_gs_jam_node = 0;
	bool m_gs_cant_fire_logged = false;

	// Per-shot wear of the receiver and every part in the firing path.
	void gs_apply_shot_wear(float impair);
	virtual void gs_modify_bullet(float& speed, float& hit_power, float& impulse);
	// Switches shot sound, particles and lights between suppressed and not.
	virtual void gs_on_suppression_changed() {}

	// Mirrors an attach (part != null) or detach of an old-style addon into the tree.
	void gs_set_legacy_addon(u8 legacy_addon, LPCSTR part, float condition);
	// Makes the tree's scope part match the scope the weapon actually uses (m_cur_scope).
	void gs_match_scope_part();
