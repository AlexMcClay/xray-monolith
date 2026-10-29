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

// Functions that return Lua values take the calling lua_State (luabind raw<1>
// policy): luabind pushes a returned object onto the state it was created in, so a
// table built on the main state comes back as nil to scripts running in their own
// thread, such as run_string.

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

	::luabind::object stats_to_lua(lua_State* L, const CWeapon* weapon, const gunsmith::SWeaponStats& stats,
	                               bool has_silencer, u8 jam_type, u8 jam_node, bool jammed)
	{
		::luabind::object t = ::luabind::newtable(L);

		t["ergonomics"] = stats.ergonomics;
		// The weapon's weight and price with these parts instead of its current ones.
		t["weight"] = weapon->Weight() - weapon->gs_stats().weight_add + stats.weight_add;
		t["cost"] = float(weapon->Cost()) - weapon->gs_stats().cost_add + stats.cost_add;
		t["parts_weight"] = stats.weight_add;
		t["parts_cost"] = stats.cost_add;
		t["can_fire"] = stats.can_fire;
		t["suppressor_worn"] = stats.suppressor_worn;
		t["suppressed"] = has_silencer && !stats.suppressor_worn;
		t["muzzle_condition"] = stats.muzzle_condition;
		t["barrel_wear_mul"] = stats.barrel_wear_mul;
		t["sight_sway"] = stats.sight_sway;
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
			row["mul"] = stats.mul[s];
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

LPCSTR CScriptGameObject::gs_can_attach_reason(LPCSTR section, u8 parent, u8 slot)
{
	CWeapon* weapon = gs_weapon(this, "gs_can_attach_reason");
	if (!weapon)
		return "no_weapon";
	return weapon->gs_check_place(weapon->gs_tree(), section, parent, slot, false);
}

bool CScriptGameObject::gs_can_attach(LPCSTR section, u8 parent, u8 slot)
{
	CWeapon* weapon = gs_weapon(this, "gs_can_attach");
	return weapon && !*weapon->gs_check_place(weapon->gs_tree(), section, parent, slot, false);
}

bool CScriptGameObject::gs_attach(CScriptGameObject* part, u8 parent, u8 slot)
{
	CWeapon* weapon = gs_weapon(this, "gs_attach");
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

	gunsmith::SLOTS slots;
	weapon->gs_tree().node_slots(parent, slots);
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

bool CScriptGameObject::gs_attach_section(LPCSTR section, u8 parent, u8 slot, float condition)
{
	CWeapon* weapon = gs_weapon(this, "gs_attach_section");
	return weapon && weapon->gs_attach_part(section, parent, slot, condition);
}

::luabind::object gs_detach_spawn_script(lua_State* L, CScriptGameObject* self, u8 node, bool spawn)
{
	CWeapon* weapon = gs_weapon(self, "gs_detach");
	u16 item_id = u16(-1);
	if (!weapon || !weapon->gs_detach_part(node, spawn, item_id))
		return ::luabind::object(); // nil
	return ::luabind::object(L, item_id == u16(-1) ? -1 : int(item_id));
}

::luabind::object gs_detach_script(lua_State* L, CScriptGameObject* self, u8 node)
{
	return gs_detach_spawn_script(L, self, node, true);
}

bool CScriptGameObject::gs_set_condition(u8 node, float value)
{
	CWeapon* weapon = gs_weapon(this, "gs_set_condition");
	return weapon && weapon->gs_debug_set_condition(node, value);
}

::luabind::object gs_get_stats_script(lua_State* L, CScriptGameObject* self)
{
	CWeapon* weapon = gs_weapon(self, "gs_get_stats");
	if (!weapon)
		return ::luabind::newtable(L);
	return stats_to_lua(L, weapon, weapon->gs_stats(), weapon->IsSuppressorFitted(), weapon->gs_jam_type(),
	                    weapon->gs_jam_node(), weapon->IsMisfire());
}

// The part models on a gs_mode = full weapon's world model: { {node, parent, section, i, j, k, c}, ... }
// in tree order, i/j/k/c being the part model's axes and origin in the receiver model's space.
::luabind::object gs_part_layout_script(lua_State* L, CScriptGameObject* self)
{
	::luabind::object t = ::luabind::newtable(L);
	CWeapon* weapon = gs_weapon(self, "gs_part_layout");
	if (!weapon)
		return t;
	xr_vector<std::pair<u8, Fmatrix>> layout;
	weapon->gs_world_layout(layout);
	int count = 0;
	for (const auto& l : layout)
	{
		const gunsmith::SNode& n = weapon->gs_tree().node(l.first);
		::luabind::object row = ::luabind::newtable(L);
		row["node"] = int(l.first);
		row["parent"] = int(n.parent);
		row["section"] = n.section.c_str();
		row["i"] = l.second.i;
		row["j"] = l.second.j;
		row["k"] = l.second.k;
		row["c"] = l.second.c;
		t[++count] = row;
	}
	return t;
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

// The magnification of the optic the weapon aims through: { max, min } (min = max when fixed),
// or nil when the sight doesn't magnify or a vanilla scope addon is fitted.
::luabind::object gs_sight_magnification_script(lua_State* L, CScriptGameObject* self)
{
	CWeapon* weapon = gs_weapon(self, "gs_sight_magnification");
	const gunsmith::SPartVisual* optic = weapon ? weapon->gs_optic() : nullptr;
	if (!optic)
		return ::luabind::object();
	::luabind::object t = ::luabind::newtable(L);
	t["max"] = optic->magnification;
	t["min"] = optic->magnification_min > 0.f ? optic->magnification_min : optic->magnification;
	return t;
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
	CInventoryItem* ii = smart_cast<CInventoryItem*>(&item->object());
	LPCSTR section = item->object().cNameSect().c_str();
	const float condition = ii ? ii->GetCondition() : 1.f;

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
			tree.add_node(section, parent, slot, condition);
		else if (tree.graft(*subtree, parent, slot, condition) < 0)
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
	weapon->gs_compute_stats(tree, stats);
	const bool has_silencer = (tree.legacy_addon_flags() & gunsmith::eLegacySilencer) ||
		(weapon->IsSilencerAttached() && !(tree.legacy_mask() & gunsmith::eLegacySilencer)) || stats.suppressor_part;
	::luabind::object result = stats_to_lua(L, weapon, stats, has_silencer, 0, 0, false);
	result["errors"] = errors;
	result["carried"] = carried;
	result["dropped"] = dropped;
	return result;
}

::luabind::object gs_preview_stats_script(lua_State* L, CScriptGameObject* self, ::luabind::object changes)
{
	CWeapon* weapon = gs_weapon(self, "gs_preview_stats");
	if (!weapon)
		return ::luabind::newtable(L);

	gunsmith::CTree tree = weapon->gs_tree();
	::luabind::object errors = ::luabind::newtable(L);
	u32 error_count = 0;
	auto fail = [&](u32 index, LPCSTR reason)
	{
		string256 msg;
		xr_sprintf(msg, "%u: %s", index, reason);
		errors[++error_count] = (LPCSTR)msg;
	};

	if (changes.type() == LUA_TTABLE)
	{
		for (u32 i = 1;; ++i)
		{
			::luabind::object row = changes[i];
			if (row.type() == LUA_TNIL)
				break;
			if (row.type() != LUA_TTABLE)
			{
				fail(i, "not_a_table");
				continue;
			}
			::luabind::object parent_obj = row["parent"], slot_obj = row["slot"], section_obj = row["section"], cond_obj = row["condition"];
			if (parent_obj.type() != LUA_TNUMBER || slot_obj.type() != LUA_TNUMBER || section_obj.type() != LUA_TSTRING)
			{
				fail(i, "needs_parent_slot_section");
				continue;
			}
			const int parent = ::luabind::object_cast<int>(parent_obj);
			const int slot = ::luabind::object_cast<int>(slot_obj);
			LPCSTR section = ::luabind::object_cast<LPCSTR>(section_obj);
			const float condition = cond_obj.type() == LUA_TNUMBER ? ::luabind::object_cast<float>(cond_obj) : 1.f;
			if (parent < 0 || parent >= int(tree.size()) || slot < 0 || slot > 255)
			{
				fail(i, "no_such_node");
				continue;
			}

			// A change replaces whatever is in the slot.
			const int existing = tree.find_in_slot(u8(parent), u8(slot));
			gunsmith::CTree before = tree;
			if (existing >= 0)
				tree.remove_subtree(u8(existing));
			if (!xr_strcmp(section, "none"))
				continue;

			LPCSTR reason = weapon->gs_check_place(tree, section, u8(parent), u8(slot), true);
			if (*reason)
			{
				tree = before;
				fail(i, reason);
				continue;
			}
			tree.add_node(section, u8(parent), u8(slot), condition);
		}
	}
	else if (changes.type() != LUA_TNIL)
		fail(0, "changes_not_a_table");

	gunsmith::SWeaponStats stats;
	weapon->gs_compute_stats(tree, stats);
	const bool has_silencer = (tree.legacy_addon_flags() & gunsmith::eLegacySilencer) ||
		(weapon->IsSilencerAttached() && !(tree.legacy_mask() & gunsmith::eLegacySilencer)) || stats.suppressor_part;
	::luabind::object result = stats_to_lua(L, weapon, stats, has_silencer, 0, 0, false);
	result["errors"] = errors;
	return result;
}

#endif // GAME_OBJECT_EXTENDED_EXPORTS
