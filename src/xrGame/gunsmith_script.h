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
::luabind::object gs_part_layout_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_sight_section_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_sight_magnification_script(lua_State* L, CScriptGameObject* self);
::luabind::object gs_preview_stats_script(lua_State* L, CScriptGameObject* self, ::luabind::object changes);
::luabind::object gs_preview_attach_script(lua_State* L, CScriptGameObject* self, CScriptGameObject* item, u8 parent, u8 slot);
::luabind::object gs_detach_script(lua_State* L, CScriptGameObject* self, u8 node);
::luabind::object gs_detach_spawn_script(lua_State* L, CScriptGameObject* self, u8 node, bool spawn);

// gunsmith.model_bones(path): the bone names of a model (path without .ogf), or nil when
// the file is missing or not skeletal. Loads the model as a world model, so don't pass a
// first-person (hud_visual) path: a model's HUD shaders are fixed when its path first loads.
::luabind::object gs_model_bones_script(lua_State* L, LPCSTR path);

// Previews (gunsmith_preview.h): inventory icons and the workbench view.
//   gunsmith.icon_rect(id) / icon_rect_section(section)  {x1, y1, x2, y2} in icon_texture(), or nil
//   gunsmith.preview_object(id) / preview_section(section) / preview_clear()
//   gunsmith.preview_camera(yaw, pitch, zoom, pan_x, pan_y, aspect), preview_tick() every frame shown
//   gunsmith.preview_slots()  { {node, slot, u, v, visible}, ... } with the current camera
//   gunsmith.preview_bounds() {x1, y1, x2, y2} of the build on screen (0..1), or nil
//   gunsmith.preview_resolution(w, h) the view texture's size (default 1024 x 512)
//   gunsmith.preview_texture(), preview_size() -> {w, h}, preview_available()
::luabind::object gs_icon_rect_script(lua_State* L, u16 id);
::luabind::object gs_icon_rect_section_script(lua_State* L, LPCSTR section);
::luabind::object gs_preview_size_script(lua_State* L);
LPCSTR gs_icon_texture();
LPCSTR gs_preview_texture();
bool gs_preview_available();
void gs_preview_object(u16 id);
void gs_preview_section(LPCSTR section);
void gs_preview_clear();
void gs_preview_camera(float yaw, float pitch, float zoom, float pan_x, float pan_y, float aspect);
::luabind::object gs_preview_slots_script(lua_State* L);
::luabind::object gs_preview_bounds_script(lua_State* L);
void gs_preview_resolution(u32 w, u32 h);
void gs_preview_tick();
bool gs_is_full_mode(LPCSTR section);
// gunsmith.carries_parts(id): whether a part item carries parts mounted on it.
bool gs_carries_parts(u16 id);
