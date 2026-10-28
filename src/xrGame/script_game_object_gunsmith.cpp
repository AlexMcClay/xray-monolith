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

#ifdef GAME_OBJECT_EXTENDED_EXPORTS

namespace
{
	CWeapon* gs_weapon(CScriptGameObject* self, LPCSTR method)
	{
		CWeapon* weapon = smart_cast<CWeapon*>(&self->object());
		if (!weapon)
			ai().script_engine().script_log(ScriptStorage::eLuaMessageTypeError,
			                                "game_object:%s: [%s] is not a weapon", method, self->object().cNameSect().c_str());
		return weapon;
	}

	::luabind::object stats_to_lua(const CWeapon* weapon, const gunsmith::SWeaponStats& stats, bool has_silencer,
	                               u8 jam_type, u8 jam_node, bool jammed)
	{
		lua_State* L = ai().script_engine().lua();
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

::luabind::object CScriptGameObject::gs_get_tree()
{
	CWeapon* weapon = gs_weapon(this, "gs_get_tree");
	if (!weapon)
		return ::luabind::newtable(ai().script_engine().lua());
	return gunsmith::tree_to_lua(weapon->gs_tree());
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

	if (!weapon->gs_attach_part(section, parent, slot, item->GetCondition()))
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

::luabind::object CScriptGameObject::gs_detach_spawn(u8 node, bool spawn)
{
	CWeapon* weapon = gs_weapon(this, "gs_detach");
	if (!weapon)
		return ::luabind::object(ai().script_engine().lua()); // nil
	u16 item_id = u16(-1);
	if (!weapon->gs_detach_part(node, spawn, item_id))
		return ::luabind::object(ai().script_engine().lua()); // nil
	return ::luabind::object(ai().script_engine().lua(), item_id == u16(-1) ? -1 : int(item_id));
}

::luabind::object CScriptGameObject::gs_detach(u8 node)
{
	return gs_detach_spawn(node, true);
}

bool CScriptGameObject::gs_set_condition(u8 node, float value)
{
	CWeapon* weapon = gs_weapon(this, "gs_set_condition");
	return weapon && weapon->gs_debug_set_condition(node, value);
}

::luabind::object CScriptGameObject::gs_get_stats()
{
	CWeapon* weapon = gs_weapon(this, "gs_get_stats");
	if (!weapon)
		return ::luabind::newtable(ai().script_engine().lua());
	return stats_to_lua(weapon, weapon->gs_stats(), weapon->IsSilencerAttached(), weapon->gs_jam_type(),
	                    weapon->gs_jam_node(), weapon->IsMisfire());
}

::luabind::object CScriptGameObject::gs_preview_stats(::luabind::object changes)
{
	lua_State* L = ai().script_engine().lua();
	CWeapon* weapon = gs_weapon(this, "gs_preview_stats");
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
		(weapon->IsSilencerAttached() && !(tree.legacy_mask() & gunsmith::eLegacySilencer));
	::luabind::object result = stats_to_lua(weapon, stats, has_silencer, 0, 0, false);
	result["errors"] = errors;
	return result;
}

#endif // GAME_OBJECT_EXTENDED_EXPORTS
