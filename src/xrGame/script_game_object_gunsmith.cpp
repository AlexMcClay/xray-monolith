////////////////////////////////////////////////////////////////////////////
//	Module 		: script_game_object_gunsmith.cpp
//	Description : Gunsmith part tree, game_object script export for weapons
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_game_object.h"
#include "script_game_object_impl.h"
#include "Weapon.h"
#include "inventory_item.h"
#include "ai_space.h"
#include "script_engine.h"
#include "gunsmith_script.h"
#include "Level.h"
#include "xrServer.h"
#include "game_sv_base.h"
#include "clsid_game.h"
#include "inventory_item_object.h"

#ifdef GAME_OBJECT_EXTENDED_EXPORTS

// Functions that return Lua values take the calling lua_State (raw<1>): see gunsmith_script.h.

namespace
{
	CWeapon* gs_weapon(CScriptGameObject* self, LPCSTR method)
	{
		CWeapon* weapon = self ? smart_cast<CWeapon*>(&self->object()) : nullptr;
		if (!weapon)
			ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			                                "game_object:%s: [%s] is not a weapon", method,
			                                self ? self->object().cNameSect().c_str() : "nil");
		return weapon;
	}

	// `live`: the weapon's own stats for display (gs_display_stats), against which a preview's weight and cost go
	::luabind::object stats_to_lua(lua_State* L, const CWeapon* weapon, const gunsmith::SWeaponStats& stats,
	                               const gunsmith::SWeaponStats& live, bool has_silencer, u8 jam_type, u8 jam_node,
	                               bool jammed)
	{
		::luabind::object t = ::luabind::newtable(L);

		if (stats.ergonomics >= 0.f)
			t["ergonomics"] = stats.ergonomics; // as the scripts give it (gunsmith_handling)
		t["recoil_k"] = stats.k(gunsmith::eStatCamDispersion); // the scripts' recoil multiplier (gunsmith_handling)
		t["no_stock"] = stats.no_stock;
		// The weapon's weight and price with these parts instead of its current ones.
		t["weight"] = weapon->Weight() - live.weight_add + stats.weight_add;
		t["cost"] = float(weapon->Cost()) - live.cost_add + stats.cost_add;
		t["parts_weight"] = stats.weight_add;
		t["parts_cost"] = stats.cost_add;
		t["can_fire"] = stats.can_fire;
		t["suppressed"] = has_silencer;
		t["jammed"] = jammed;
		t["jam_type"] = jammed ? gunsmith::failure_name(jam_type) : "none";
		if (jammed)
			t["jam_node"] = int(jam_node);

		::luabind::object list = ::luabind::newtable(L);
		for (u32 s = 0; s < gunsmith::eStatCount; ++s)
		{
			float base, add, value;
			weapon->gs_stat_values(stats, u8(s), base, add, value);
			::luabind::object row = ::luabind::newtable(L);
			row["base"] = base;
			row["add"] = add;
			row["mul"] = stats.mul[s] * stats.k(u8(s));
			row["value"] = value;
			list[gunsmith::stat_name(u8(s))] = row;
		}
		t["stats"] = list;
		return t;
	}
}

::luabind::object gs_get_tree_script(lua_State* L, CScriptGameObject* self)
{
	// A part item: the parts it carries (empty when none or not online on the server).
	if (self && !smart_cast<CWeapon*>(&self->object()))
	{
		if (OnServer() && Level().Server && Level().Server->game)
			if (CSE_ALifeItem* se = smart_cast<CSE_ALifeItem*>(Level().Server->game->get_entity_from_eid(self->object().ID())))
				if (se->m_tClassID == CLSID_IITEM_ATTACH)
					return gunsmith::tree_to_lua(L, se->m_gs_subtree);
	}
	CWeapon* weapon = gs_weapon(self, "gs_get_tree");
	if (!weapon)
		return ::luabind::newtable(L);
	return gunsmith::tree_to_lua(L, weapon->gs_tree());
}

namespace
{
	// What SAFE_WRAP (script_game_object.h) checks before a member call: with lua_busy_hands_debug, a destroyed
	// object is reported.
	void gs_safe_self(CScriptGameObject* self)
	{
		if (lua_busy_hands_debug && !(self != nullptr && self->is_valid()))
			SafeWrapBase::log_and_callback("Accessing destroyed object");
	}
}

static bool gs_can_attach_script(CScriptGameObject* self, LPCSTR section, u8 parent, u8 slot)
{
	gs_safe_self(self);
	CWeapon* weapon = gs_weapon(self, "gs_can_attach");
	return weapon && !*weapon->gs_check_place(weapon->gs_tree(), section, parent, slot, false);
}

static bool gs_attach_script(CScriptGameObject* self, CScriptGameObject* part, u8 parent, u8 slot)
{
	gs_safe_self(self);
	CWeapon* weapon = gs_weapon(self, "gs_attach");
	if (!weapon)
		return false;
	CInventoryItem* item = part ? part->object().cast_inventory_item() : nullptr;
	if (!item || item->object().ID() == weapon->ID())
	{
		ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError, "game_object:gs_attach: the part is not an inventory item");
		return false;
	}

	LPCSTR section = item->object().cNameSect().c_str();
	LPCSTR reason = weapon->gs_check_place(weapon->gs_tree(), section, parent, slot, false);
	if (*reason)
	{
		Msg("! [gunsmith] [%s] gs_attach [%s] to node %u slot %u: %s", weapon->cNameSect().c_str(), section, u32(parent), u32(slot), reason);
		return false;
	}

	const gunsmith::SLOTS& slots = weapon->gs_tree().node_slots(parent);
	if (slots[slot].legacy_addon != gunsmith::eLegacyNone)
	{
		// Old-style addon: the weapon's own attach code (flags, visuals, scope) does it.
		if (!weapon->CanAttach(item))
			return false;
		return weapon->Attach(item, true);
	}

	if (!weapon->gs_attach_item(item, parent, slot))
		return false;
	if (OnServer())
		item->object().DestroyObject();
	return true;
}

::luabind::object gs_detach_spawn_script(lua_State* L, CScriptGameObject* self, u8 node, bool spawn)
{
	CWeapon* weapon = gs_weapon(self, "gs_detach");
	u16 item_id = u16(-1);
	if (!weapon || !weapon->gs_detach_part(node, spawn, item_id))
		return ::luabind::object(); // nil
	return ::luabind::object(L, item_id == u16(-1) ? -1 : int(item_id));
}

::luabind::object gs_get_stats_script(lua_State* L, CScriptGameObject* self)
{
	CWeapon* weapon = gs_weapon(self, "gs_get_stats");
	if (!weapon)
		return ::luabind::newtable(L);
	gunsmith::SWeaponStats live;
	weapon->gs_display_stats(live);
	return stats_to_lua(L, weapon, live, live, weapon->IsSuppressorFitted(), weapon->gs_jam_type(),
	                    weapon->gs_jam_node(), weapon->IsMisfire());
}

// The build's state revision (gunsmith_tree.h); 0 for other objects.
u32 gs_revision_script(CScriptGameObject* self)
{
	if (!self)
		return 0;
	if (const CWeapon* weapon = smart_cast<const CWeapon*>(&self->object()))
		return weapon->gs_tree().state_revision();
	if (const CInventoryItemObject* item = smart_cast<const CInventoryItemObject*>(&self->object()))
		return item->gs_subtree().state_revision();
	return 0;
}

int gs_fit_verdict_script(CScriptGameObject* self, LPCSTR section)
{
	if (!self || !section || !*section)
		return 0;
	CWeapon* weapon = smart_cast<CWeapon*>(&self->object());
	const CInventoryItemObject* item = weapon ? nullptr : smart_cast<const CInventoryItemObject*>(&self->object());
	const gunsmith::CTree* tree = weapon ? &weapon->gs_tree() : item ? &item->gs_subtree() : nullptr;
	if (!tree || tree->empty())
		return 0;
	LPCSTR receiver = tree->node(0).section.c_str();
	int best = 0;
	for (u32 i = 0; i < tree->size(); ++i)
	{
		const gunsmith::SLOTS& slots = tree->node_slots(u8(i));
		for (u32 s = 0; s < slots.size(); ++s)
		{
			const gunsmith::SSlotInfo& slot = slots[s];
			if (slot.legacy_addon != gunsmith::eLegacyNone || !gunsmith::part_fits_slot(section, slot, receiver, true))
				continue;
			bool ok;
			if (tree->is_slot_blocked(slot.section))
				ok = false;
			else if (weapon && tree->find_in_slot(u8(i), u8(s)) < 0)
				ok = !*weapon->gs_check_place(*tree, section, u8(i), u8(s), false); // the rules gs_can_attach applies
			else
				ok = true;
			if (ok)
				return 2;
			best = 1;
		}
	}
	return best;
}

LPCSTR gs_jam_type_script(CScriptGameObject* self)
{
	const CWeapon* weapon = self ? smart_cast<const CWeapon*>(&self->object()) : nullptr;
	return weapon && weapon->IsMisfire() ? gunsmith::failure_name(weapon->gs_jam_type()) : "none";
}

// The section of the sight the weapon aims through (gs_mode = full), or nil.
::luabind::object gs_sight_section_script(lua_State* L, CScriptGameObject* self)
{
	CWeapon* weapon = gs_weapon(self, "gs_sight_section");
	shared_str section;
	if (!weapon || !weapon->gs_sight_part(&section))
		return ::luabind::object();
	return ::luabind::object(L, section.c_str());
}

// Sight modes: the sight aimed through's mode, { mode, count, zoom, sight } (mode from 0; zoom
// its magnification, 1 = none; sight: which of the part's sights, from 0), or nil without a sight.
::luabind::object gs_sight_mode_script(lua_State* L, CScriptGameObject* self)
{
	CWeapon* weapon = gs_weapon(self, "gs_sight_mode");
	gunsmith::SSightAim aim;
	if (!weapon || !weapon->gs_sight_aim(aim))
		return ::luabind::object();
	u8 count;
	const u8 mode = weapon->gs_sight_mode(&count);
	::luabind::object t = ::luabind::newtable(L);
	t["mode"] = int(mode);
	t["count"] = int(count);
	t["zoom"] = aim.magnification > 1.f ? aim.magnification : 1.f;
	const u8 node = weapon->gs_sight_node();
	const gunsmith::SNode& n = weapon->gs_tree().node(node);
	t["sight"] = int(gunsmith::node_sight(gunsmith::part_visual(n.section), n.mode));
	return t;
}

// Sight switching: the cyclable sights fitted, { { node, section }, ... } in tree order.
::luabind::object gs_sights_script(lua_State* L, CScriptGameObject* self)
{
	::luabind::object t = ::luabind::newtable(L);
	CWeapon* weapon = gs_weapon(self, "gs_sights");
	if (!weapon)
		return t;
	xr_vector<u8> sights;
	weapon->gs_sight_list(sights);
	int i = 1;
	for (u8 node : sights)
	{
		const gunsmith::SNode& n = weapon->gs_tree().node(node);
		const gunsmith::SPartVisual& pv = gunsmith::part_visual(n.section);
		::luabind::object s = ::luabind::newtable(L);
		s["node"] = int(node);
		s["section"] = n.section.c_str();
		s["tactical"] = pv.is_tactical(); // a device with a sight: no sights or modes of its own here
		s["sights"] = int(pv.is_tactical() ? 1 : pv.sight_count()); // a dual optic has 2
		s["modes"] = int(pv.is_tactical() ? 1 : pv.mode_count());
		s["sight"] = int(gunsmith::node_sight(pv, n.mode));
		s["mode"] = int(gunsmith::node_sight_mode(pv, n.mode));
		t[i++] = s;
	}
	return t;
}

// Aim through a fitted sight with that sight of it and mode (0-based); false if it isn't one or out of range.
::luabind::object gs_sight_set_script(lua_State* L, CScriptGameObject* self, u8 node, u8 sight, u8 mode)
{
	CWeapon* weapon = gs_weapon(self, "gs_sight_set");
	return ::luabind::object(L, weapon ? weapon->gs_sight_set(node, sight, mode) : false);
}

// The node of the sight the weapon aims through, or nil.
::luabind::object gs_sight_node_script(lua_State* L, CScriptGameObject* self)
{
	CWeapon* weapon = gs_weapon(self, "gs_sight_node");
	const u8 node = weapon ? weapon->gs_sight_node() : 0;
	return node ? ::luabind::object(L, int(node)) : ::luabind::object();
}

// Tactical devices: { on = bool, devices = { { node, section, mode, modes = { "light", "light, laser", ... } }, ... } }
::luabind::object gs_tactical_state_script(lua_State* L, CScriptGameObject* self)
{
	CWeapon* weapon = gs_weapon(self, "gs_tactical_state");
	if (!weapon)
		return ::luabind::object();
	::luabind::object t = ::luabind::newtable(L);
	t["on"] = weapon->gs_devices_on();
	::luabind::object list = ::luabind::newtable(L);
	const gunsmith::CTree& tree = weapon->gs_tree();
	int count = 0;
	for (u8 i = 1; i < tree.size(); ++i)
	{
		const gunsmith::SNode& n = tree.node(i);
		const gunsmith::SPartVisual& pv = gunsmith::part_visual(n.section);
		if (!pv.is_tactical())
			continue;
		::luabind::object d = ::luabind::newtable(L);
		d["node"] = int(i);
		d["section"] = n.section.c_str();
		d["mode"] = int(_min(u32(n.mode), u32(pv.tac_modes.size() - 1)));
		::luabind::object modes = ::luabind::newtable(L);
		for (u32 m = 0; m < pv.tac_modes.size(); ++m)
		{
			const u8 f = pv.tac_modes[m];
			modes[m + 1] = (f & gunsmith::SPartVisual::eTacLight) && (f & gunsmith::SPartVisual::eTacLaser) ? "light, laser"
				: (f & gunsmith::SPartVisual::eTacLight) ? "light" : "laser";
		}
		d["modes"] = modes;
		list[++count] = d;
	}
	t["devices"] = list;
	return t;
}

// Every tactical device on or off; false without devices.
::luabind::object gs_tactical_set_on_script(lua_State* L, CScriptGameObject* self, bool on)
{
	CWeapon* weapon = gs_weapon(self, "gs_tactical_set_on");
	return ::luabind::object(L, weapon ? weapon->gs_devices_set_on(on) : false);
}

// One device's mode (0-based index into its modes); false if node isn't a device or mode is out of range.
::luabind::object gs_tactical_set_mode_script(lua_State* L, CScriptGameObject* self, u8 node, u8 mode)
{
	CWeapon* weapon = gs_weapon(self, "gs_tactical_set_mode");
	return ::luabind::object(L, weapon ? weapon->gs_set_device_mode(node, mode) : false);
}

// Stats with the part item `item` in the slot, together with every part it carries, done
// the way gs_attach does it (graft, then drop carried parts that don't fit) on a copy.
// Adds `errors` ("1: <reason>" when the item itself doesn't fit), `carried` (the sections it
// brings, itself first) and `dropped` (carried sections that wouldn't stay on).
::luabind::object gs_preview_attach_script(lua_State* L, CScriptGameObject* self, CScriptGameObject* item, u8 parent, u8 slot)
{
	CWeapon* weapon = gs_weapon(self, "gs_preview_attach");
	if (!weapon || !item)
		return ::luabind::newtable(L);
	LPCSTR section = item->object().cNameSect().c_str();

	// What it carries: from its server object, as an attach reads it; else the client's copy.
	const gunsmith::CTree* subtree = nullptr;
	if (OnServer() && Level().Server && Level().Server->game)
		if (CSE_ALifeItem* se = smart_cast<CSE_ALifeItem*>(Level().Server->game->get_entity_from_eid(item->object().ID())))
			if (se->m_tClassID == CLSID_IITEM_ATTACH && se->m_gs_subtree.size() > 1)
				subtree = &se->m_gs_subtree;
	if (!subtree)
		if (CInventoryItemObject* io = smart_cast<CInventoryItemObject*>(&item->object()))
			if (io->gs_carries_parts())
				subtree = &io->gs_subtree();

	gunsmith::CTree tree = weapon->gs_tree();
	::luabind::object errors = ::luabind::newtable(L);
	::luabind::object carried = ::luabind::newtable(L);
	::luabind::object dropped = ::luabind::newtable(L);
	carried[1] = section;
	if (subtree)
		for (u32 i = 1; i < subtree->size(); ++i)
			carried[i + 1] = subtree->node(u8(i)).section.c_str();

	if (parent >= tree.size())
		errors[1] = "1: no_such_node";
	else
	{
		const int existing = tree.find_in_slot(parent, slot);
		if (existing >= 0)
			tree.remove_subtree(u8(existing));
		LPCSTR reason = weapon->gs_check_place(tree, section, parent, slot, true);
		if (*reason)
		{
			string256 msg;
			xr_sprintf(msg, "1: %s", reason);
			errors[1] = (LPCSTR)msg;
			tree = weapon->gs_tree();
		}
		else if (!subtree)
			tree.add_node(section, parent, slot);
		else if (tree.graft(*subtree, parent, slot) < 0)
		{
			errors[1] = "1: tree_full";
			tree = weapon->gs_tree();
		}
		else
		{
			xr_vector<shared_str> lost;
			tree.validate(weapon->cNameSect().c_str(), &lost);
			for (u32 i = 0; i < lost.size(); ++i)
				dropped[i + 1] = lost[i].c_str();
		}
	}

	gunsmith::SWeaponStats stats;
	weapon->gs_compute_stats(tree, stats, true);
	weapon->gs_script_stats(tree, stats, true);
	const bool has_silencer = (tree.legacy_addon_flags() & gunsmith::eLegacySilencer) ||
		(weapon->IsSilencerAttached() && !(tree.legacy_mask() & gunsmith::eLegacySilencer)) || stats.suppressor_part;
	gunsmith::SWeaponStats live;
	weapon->gs_display_stats(live);
	::luabind::object result = stats_to_lua(L, weapon, stats, live, has_silencer, 0, 0, false);
	result["errors"] = errors;
	result["carried"] = carried;
	result["dropped"] = dropped;
	return result;
}

#endif // GAME_OBJECT_EXTENDED_EXPORTS

// The gs_* game_object methods, chained into game_object's registration (script_game_object_script.cpp) like
// script_register_game_object_trader. Node and slot numbers are 0-based.
::luabind::class_<CScriptGameObject> script_register_game_object_gunsmith(::luabind::class_<CScriptGameObject>&& instance)
{
#ifdef GAME_OBJECT_EXTENDED_EXPORTS
	using namespace luabind;
	return std::move(instance)
		.def("gs_get_tree", &gs_get_tree_script, raw<1>())
		.def("gs_revision", &gs_revision_script)
		.def("gs_fit_verdict", &gs_fit_verdict_script)
		.def("gs_jam_type", &gs_jam_type_script)
		.def("gs_can_attach", &gs_can_attach_script)
		.def("gs_attach", &gs_attach_script)
		.def("gs_detach", &gs_detach_spawn_script, raw<1>())
		.def("gs_get_stats", &gs_get_stats_script, raw<1>())
		.def("gs_preview_attach", &gs_preview_attach_script, raw<1>())
		.def("gs_sight_section", &gs_sight_section_script, raw<1>())
		.def("gs_sight_mode", &gs_sight_mode_script, raw<1>())
		.def("gs_sights", &gs_sights_script, raw<1>())
		.def("gs_sight_node", &gs_sight_node_script, raw<1>())
		.def("gs_sight_set", &gs_sight_set_script, raw<1>())
		.def("gs_tactical_set_on", &gs_tactical_set_on_script, raw<1>())
		.def("gs_tactical_state", &gs_tactical_state_script, raw<1>())
		.def("gs_tactical_set_mode", &gs_tactical_set_mode_script, raw<1>());
#else
	return std::move(instance);
#endif
}
