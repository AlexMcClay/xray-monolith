////////////////////////////////////////////////////////////////////////////
//	Module 		: xrServer_Objects_ALife_Items_script.cpp
//	Created 	: 19.09.2002
//  Modified 	: 04.06.2003
//	Author		: Dmitriy Iassenev
//	Description : Server items for ALife simulator, script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "xrServer_Objects_ALife_Items.h"
#include "xrServer_script_macroses.h"
#ifdef XRGAME_EXPORTS
#	include "gunsmith_script.h"
#endif

void add_upgrade_script(CSE_ALifeInventoryItem* ta, LPCSTR str)
{
	ta->add_upgrade(str);
}

bool has_upgrade_script(CSE_ALifeInventoryItem* ta, LPCSTR str)
{
	return ta->has_upgrade(str);
}

float get_condition_script(CSE_ALifeInventoryItem* item)
{
	return item->m_fCondition;
}

// the net packet quantises condition over zero to one, anything else corrupts the stored value
void set_condition_script(CSE_ALifeInventoryItem* item, float value)
{
	if (value < 0.f || value > 1.f)
	{
		Msg("!cse_alife_inventory_item.condition set to %f, must be between 0 and 1", value);
		return;
	}

	item->m_fCondition = value;
}

#ifdef XRGAME_EXPORTS
// Gunsmith: part tree access on a weapon's server object, for guns that are
// offline (NPC loadouts, traders, stashes). Same table format as game_object:gs_get_tree.
// Takes the calling lua_State (raw<1>): see gunsmith_script.h.
::luabind::object cse_gs_get_tree_script(lua_State* L, CSE_ALifeItemWeapon* weapon)
{
	weapon->gs_ensure_tree();
	return gunsmith::tree_to_lua(L, weapon->m_gs_tree);
}

bool cse_gs_set_tree_script(CSE_ALifeItemWeapon* weapon, ::luabind::object nodes)
{
	gunsmith::CTree tree;
	xr_string error;
	if (!gunsmith::tree_from_lua(nodes, weapon->s_name.c_str(), weapon->m_fCondition, tree, error))
	{
		Msg("! cse_alife_item_weapon:gs_set_tree [%s]: %s", weapon->s_name.c_str(), error.c_str());
		return false;
	}
	weapon->gs_set_tree(tree);
	return true;
}
#endif

using namespace luabind;

#pragma optimize("s",on)
void CSE_ALifeInventoryItem::script_register(lua_State* L)
{
	module(L)[
		class_<CSE_ALifeInventoryItem>
		("cse_alife_inventory_item")
		//			.def(		constructor<LPCSTR>())
		.def("has_upgrade", &has_upgrade)
		.def("add_upgrade", &add_upgrade)
		.property("condition", &get_condition_script, &set_condition_script)
	];
}

void CSE_ALifeItem::script_register(lua_State* L)
{
	module(L)[
		luabind_class_item2(
			//		luabind_class_abstract2(
			CSE_ALifeItem,
			"cse_alife_item",
			CSE_ALifeDynamicObjectVisual,
			CSE_ALifeInventoryItem
		)
	];
}

void CSE_ALifeItemTorch::script_register(lua_State* L)
{
	module(L)[
		luabind_class_item1(
			CSE_ALifeItemTorch,
			"cse_alife_item_torch",
			CSE_ALifeItem
		)
	];
}

void CSE_ALifeItemAmmo::script_register(lua_State* L)
{
	module(L)[
		luabind_class_item1(
			CSE_ALifeItemAmmo,
			"cse_alife_item_ammo",
			CSE_ALifeItem
		)
	];
}

void CSE_ALifeItemWeapon::script_register(lua_State* L)
{
	module(L)[
		luabind_class_item1(
			CSE_ALifeItemWeapon,
			"cse_alife_item_weapon",
			CSE_ALifeItem
		)
		.def("clone_addons", &CSE_ALifeItemWeapon::clone_addons)
		.def("set_ammo_elapsed", &CSE_ALifeItemWeapon::set_ammo_elapsed)
		.def("get_ammo_elapsed", &CSE_ALifeItemWeapon::get_ammo_elapsed)
		.def("get_ammo_magsize", &CSE_ALifeItemWeapon::get_ammo_magsize)
#ifdef XRGAME_EXPORTS
		.def("gs_get_tree", &cse_gs_get_tree_script, raw<1>())
		.def("gs_set_tree", &cse_gs_set_tree_script)
#endif
	];
}

void CSE_ALifeItemWeaponShotGun::script_register(lua_State* L)
{
	module(L)[
		luabind_class_item1(
			CSE_ALifeItemWeaponShotGun,
			"cse_alife_item_weapon_shotgun",
			CSE_ALifeItemWeapon
		)
	];
}

void CSE_ALifeItemWeaponAutoShotGun::script_register(lua_State* L)
{
	module(L)[
		luabind_class_item1(
			CSE_ALifeItemWeaponAutoShotGun,
			"cse_alife_item_weapon_auto_shotgun",
			CSE_ALifeItemWeapon
		)
	];
}

void CSE_ALifeItemDetector::script_register(lua_State* L)
{
	module(L)[
		luabind_class_item1(
			CSE_ALifeItemDetector,
			"cse_alife_item_detector",
			CSE_ALifeItem
		)
	];
}

void CSE_ALifeItemArtefact::script_register(lua_State* L)
{
	module(L)[
		luabind_class_item1(
			CSE_ALifeItemArtefact,
			"cse_alife_item_artefact",
			CSE_ALifeItem
		)
	];
}
