////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_script.h
//	Description : Gunsmith part tree <-> Lua table conversion
//	              (include after pch_script.h)
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_tree.h"

namespace gunsmith
{
	// Node numbers are 0-based everywhere (node 0 is the receiver), as in gunsmith_debug.dump_tree.
	// The returned array is 1-based, so element n + 1 is node n. Table format: lua_help_ex.script, game_object gs_get_tree.
	// Build tables on the calling script's lua_State (luabind raw<1> policy): luabind
	// pushes a returned object onto the state it was created in, so a table made on
	// the main state comes back as nil to scripts running in their own thread, such as run_string.
	::luabind::object tree_to_lua(lua_State* L, const CTree& tree);

	// Builds a tree from an array of { section, parent, slot }.
	// Element 1 is the receiver: its section, if given, must be `receiver`. Every other
	// element needs a section, a parent node number lower than its own, and a slot.
	// Returns false with a message in `error` when the build is malformed.
	bool tree_from_lua(const ::luabind::object& nodes, LPCSTR receiver, CTree& out, xr_string& error);
}

// game_object methods that return Lua values (script_game_object_gunsmith.cpp),
// registered with raw<1>() so they receive the calling lua_State.
class CScriptGameObject;
::luabind::object gs_get_tree_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_get_stats_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_sight_section_script(lua_State* L, CScriptGameObject* self);
// The build's state revision (gunsmith_tree.h): a new number whenever its parts or modes change; 0 for other objects
u32 gs_revision_script(CScriptGameObject* self);
// Whether part section `section` fits the current build of a gun or a part item carrying parts: 2 a slot takes it
// (an empty one it can go into now, or a filled one it can replace), 1 a slot takes its kind but is blocked or the
// place is refused, 0 none (also for anything else). The Framework's highlight rules, in one call.
int gs_fit_verdict_script(CScriptGameObject* self, LPCSTR section);
// The jam type of a jammed gun ("barrel", "feed", "extraction"...), "none" when it isn't jammed or isn't a gun.
LPCSTR gs_jam_type_script(CScriptGameObject* self);
::luabind::object gs_sight_mode_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_sights_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_sight_node_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_sight_set_script(lua_State* L, CScriptGameObject* self, u8 node, u8 sight, u8 mode);
::luabind::object gs_tactical_set_on_script(lua_State* L, CScriptGameObject* self, bool on);
::luabind::object gs_tactical_state_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_tactical_set_mode_script(lua_State* L, CScriptGameObject* self, u8 node, u8 mode);
::luabind::object gs_preview_attach_script(lua_State* L, CScriptGameObject* self, CScriptGameObject* item, u8 parent, u8 slot);
::luabind::object gs_detach_spawn_script(lua_State* L, CScriptGameObject* self, u8 node, bool spawn);

// gunsmith.model_bones(path): the bone names of a model (path without .ogf), or nil when
// the file is missing or not skeletal. Loads the model as a world model, so don't pass a
// first-person (hud_visual) path: a model's HUD shaders are fixed when its path first loads.
::luabind::object gs_model_bones_script(lua_State* L, LPCSTR path);

// gunsmith.preload_models(): loads the part models of every Gunsmith gun and part item on the level (for a loading
// screen); returns how many objects. gunsmith.default_tree(section): a gun section's stock build as a part tree.
u32 gs_preload_models();
::luabind::object gs_default_tree_script(lua_State* L, LPCSTR section);
bool gs_is_full_mode(LPCSTR section);
// Every weapon in play recomputes its stats, so weapon_on_recalc_stats runs for each (after a handling setting
// changes: the Framework's MCM page).
void gs_refresh_stats_script();
// gunsmith.carries_parts(id): whether a part item carries parts mounted on it.
bool gs_carries_parts(u16 id);

// cse_alife_item_weapon:gs_get_tree() / gs_set_tree(nodes) (xrServer_Objects_ALife_Items_script.cpp).
class CSE_ALifeItemWeapon;
::luabind::object cse_gs_get_tree_script(lua_State* L, CSE_ALifeItemWeapon* weapon);
bool cse_gs_set_tree_script(CSE_ALifeItemWeapon* weapon, ::luabind::object nodes);

// Registers the gunsmith module (called from CLevel::script_register).
void gunsmith_script_register(lua_State* L);
