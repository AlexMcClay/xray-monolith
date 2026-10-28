////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_script.h
//	Description : Gunsmith part tree <-> Lua table conversion
//	              (include after pch_script.h)
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_tree.h"

namespace gunsmith
{
	// Node numbers are 0-based everywhere (node 0 is the receiver), as in gs_dump_tree.
	// The returned array is 1-based, so element n + 1 is node n:
	//   { index, section, parent, slot, condition, kind, legacy_addon, slots = {
	//       { index, section, accepts = {tag, ...}, required, field_swappable, implicit,
	//         legacy_addon, occupant, blocked }, ... } }
	// parent and slot are absent on the receiver; legacy_addon and occupant are absent when unset.
	// Build tables on the calling script's lua_State (luabind raw<1> policy): luabind
	// pushes a returned object onto the state it was created in, so a table made on
	// the main state comes back as nil to scripts running in their own thread.
	::luabind::object tree_to_lua(lua_State* L, const CTree& tree);

	// Builds a tree from an array of { section, parent, slot, condition }.
	// Element 1 is the receiver: its section, if given, must be `receiver`, and its
	// condition is the weapon's. Every other element needs a section, a parent node
	// number lower than its own, and a slot; condition defaults to 1.
	// Returns false with a message in `error` when the build is malformed.
	bool tree_from_lua(const ::luabind::object& nodes, LPCSTR receiver, float receiver_condition, CTree& out,
	                   xr_string& error);
}

// game_object methods that return Lua values (script_game_object_gunsmith.cpp),
// registered with raw<1>() so they receive the calling lua_State.
class CScriptGameObject;
::luabind::object gs_get_tree_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_get_stats_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_preview_stats_script(lua_State* L, CScriptGameObject* self, ::luabind::object changes);
::luabind::object gs_detach_script(lua_State* L, CScriptGameObject* self, u8 node);
::luabind::object gs_detach_spawn_script(lua_State* L, CScriptGameObject* self, u8 node, bool spawn);

// gunsmith.model_bones(path): the bone names of a model (path without .ogf), or nil when
// the file is missing or not skeletal. Loads the model as a world model, so don't pass a
// first-person (hud_visual) path: a model's HUD shaders are fixed when its path first loads.
::luabind::object gs_model_bones_script(lua_State* L, LPCSTR path);
