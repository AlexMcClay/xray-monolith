////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_script.cpp
//	Description : Gunsmith part tree <-> Lua table conversion
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "gunsmith_script.h"
#include "gunsmith_preview.h"
#include "ai_space.h"
#include "script_engine.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/RenderVisual.h"

namespace gunsmith
{
	::luabind::object tree_to_lua(lua_State* L, const CTree& tree)
	{
		::luabind::object result = ::luabind::newtable(L);

		SLOTS slots;
		for (u32 i = 0; i < tree.size(); ++i)
		{
			const SNode& n = tree.node(u8(i));
			::luabind::object t = ::luabind::newtable(L);
			t["index"] = int(i);
			t["section"] = n.section.c_str();
			if (i > 0)
			{
				t["parent"] = int(n.parent);
				t["slot"] = int(n.slot);
			}
			t["condition"] = n.condition;
			t["kind"] = i == 0 ? "receiver" : part_data(n.section.c_str()).kind.c_str();
			const u8 legacy = tree.node_legacy_addon(u8(i));
			if (legacy != eLegacyNone)
				t["legacy_addon"] = legacy_addon_name(legacy);

			::luabind::object slot_list = ::luabind::newtable(L);
			tree.node_slots(u8(i), slots);
			for (u32 s = 0; s < slots.size(); ++s)
			{
				const SSlotInfo& info = slots[s];
				::luabind::object st = ::luabind::newtable(L);
				st["index"] = int(s);
				st["section"] = info.section.c_str();

				::luabind::object accepts = ::luabind::newtable(L);
				for (u32 a = 0; a < info.accepts.size(); ++a)
					accepts[a + 1] = info.accepts[a].c_str();
				st["accepts"] = accepts;

				st["required"] = info.required;
				st["field_swappable"] = info.field_swappable;
				st["implicit"] = info.implicit;
				if (info.legacy_addon != eLegacyNone)
					st["legacy_addon"] = legacy_addon_name(info.legacy_addon);
				const int occupant = tree.find_in_slot(u8(i), u8(s));
				if (occupant >= 0)
					st["occupant"] = occupant;
				st["blocked"] = tree.is_slot_blocked(info.section);
				slot_list[s + 1] = st;
			}
			t["slots"] = slot_list;

			result[i + 1] = t;
		}
		return result;
	}

	namespace
	{
		bool read_number(const ::luabind::object& row, LPCSTR key, double& out)
		{
			::luabind::object v = row[key];
			if (v.type() != LUA_TNUMBER)
				return false;
			out = ::luabind::object_cast<double>(v);
			return true;
		}
	}

	bool tree_from_lua(const ::luabind::object& nodes, LPCSTR receiver, float receiver_condition, CTree& out,
	                   xr_string& error)
	{
		out.clear();
		string512 msg;

		if (nodes.type() != LUA_TTABLE)
		{
			error = "the build is not a table";
			return false;
		}

		::luabind::object first = nodes[1];
		if (first.type() == LUA_TTABLE)
		{
			::luabind::object section = first["section"];
			if (section.type() == LUA_TSTRING && xr_strcmp(::luabind::object_cast<LPCSTR>(section), receiver))
			{
				xr_sprintf(msg, "element 1 must be the receiver [%s], got [%s]", receiver, ::luabind::object_cast<LPCSTR>(section));
				error = msg;
				return false;
			}
		}
		out.add_node(receiver, GS_NO_PARENT, 0, receiver_condition);

		for (u32 i = 2;; ++i)
		{
			::luabind::object row = nodes[i];
			if (row.type() == LUA_TNIL)
				break;

			const u32 node = i - 1; // the node number this element becomes
			if (row.type() != LUA_TTABLE)
			{
				xr_sprintf(msg, "element %u is not a table", i);
				error = msg;
				return false;
			}

			::luabind::object section_obj = row["section"];
			if (section_obj.type() != LUA_TSTRING)
			{
				xr_sprintf(msg, "element %u (node %u) has no section", i, node);
				error = msg;
				return false;
			}
			LPCSTR section = ::luabind::object_cast<LPCSTR>(section_obj);
			if (!pSettings->section_exist(section))
			{
				xr_sprintf(msg, "node %u: section [%s] does not exist", node, section);
				error = msg;
				return false;
			}

			double parent = 0.0, slot = 0.0, condition = 1.0;
			read_number(row, "parent", parent);
			if (!read_number(row, "slot", slot))
			{
				xr_sprintf(msg, "node %u [%s] has no slot", node, section);
				error = msg;
				return false;
			}
			read_number(row, "condition", condition);

			if (parent < 0.0 || u32(parent) >= node)
			{
				xr_sprintf(msg, "node %u [%s]: parent %d must be an earlier node", node, section, int(parent));
				error = msg;
				return false;
			}
			if (slot < 0.0 || slot > 255.0)
			{
				xr_sprintf(msg, "node %u [%s]: bad slot %d", node, section, int(slot));
				error = msg;
				return false;
			}
			if (out.find_in_slot(u8(parent), u8(slot)) >= 0)
			{
				xr_sprintf(msg, "node %u [%s]: slot %d of node %d is already taken", node, section, int(slot), int(parent));
				error = msg;
				return false;
			}
			if (out.add_node(section, u8(parent), u8(slot), float(condition)) < 0)
			{
				xr_sprintf(msg, "node %u [%s]: the tree is full (%u nodes)", node, section, u32(GS_MAX_NODES));
				error = msg;
				return false;
			}
		}
		return true;
	}
}

namespace
{
	::luabind::object rect_to_lua(lua_State* L, bool ok, const Frect& r)
	{
		if (!ok)
			return ::luabind::object();
		::luabind::object t = ::luabind::newtable(L);
		t["x1"] = r.x1;
		t["y1"] = r.y1;
		t["x2"] = r.x2;
		t["y2"] = r.y2;
		return t;
	}
}

::luabind::object gs_icon_rect_script(lua_State* L, u16 id)
{
	Frect r;
	const bool ok = gunsmith::CPreview::instance().icon_rect(id, r);
	return rect_to_lua(L, ok, r);
}

::luabind::object gs_icon_rect_section_script(lua_State* L, LPCSTR section)
{
	Frect r;
	const bool ok = gunsmith::CPreview::instance().icon_rect(section, r);
	return rect_to_lua(L, ok, r);
}

::luabind::object gs_preview_size_script(lua_State* L)
{
	::luabind::object t = ::luabind::newtable(L);
	t["w"] = int(gunsmith::CPreview::instance().view_width());
	t["h"] = int(gunsmith::CPreview::instance().view_height());
	return t;
}

LPCSTR gs_icon_texture() { return "$user$gs_icons"; }
LPCSTR gs_preview_texture() { return "$user$gs_view"; }
bool gs_preview_available() { return gunsmith::CPreview::instance().available(); }
void gs_preview_object(u16 id) { gunsmith::CPreview::instance().view_object(id); }
void gs_preview_section(LPCSTR section) { gunsmith::CPreview::instance().view_section(section); }
void gs_preview_clear() { gunsmith::CPreview::instance().view_clear(); }
void gs_preview_camera(float yaw, float pitch, float zoom, float pan_x, float pan_y, float aspect)
{
	gunsmith::CPreview::instance().view_camera(yaw, pitch, zoom, pan_x, pan_y, aspect);
}

::luabind::object gs_preview_bounds_script(lua_State* L)
{
	Frect r;
	const bool ok = gunsmith::CPreview::instance().view_bounds(r);
	return rect_to_lua(L, ok, r);
}

void gs_preview_resolution(u32 w, u32 h) { gunsmith::CPreview::instance().view_resolution(w, h); }

::luabind::object gs_preview_slots_script(lua_State* L)
{
	::luabind::object t = ::luabind::newtable(L);
	int count = 0;
	xr_vector<gunsmith::CPreview::SViewSlot> slots;
	gunsmith::CPreview::instance().view_slots(slots);
	for (const auto& s : slots)
	{
		::luabind::object row = ::luabind::newtable(L);
		row["node"] = int(s.node);
		row["slot"] = int(s.slot);
		row["u"] = s.u;
		row["v"] = s.v;
		row["visible"] = s.visible;
		t[++count] = row;
	}
	return t;
}
void gs_preview_tick() { gunsmith::CPreview::instance().view_tick(); }
bool gs_is_full_mode(LPCSTR section)
{
	return section && pSettings->section_exist(section) && gunsmith::is_full_mode(section);
}

::luabind::object gs_model_bones_script(lua_State* L, LPCSTR path)
{
	if (!path || !*path)
		return ::luabind::object();
	string_path fn;
	xr_strconcat(fn, path, ".ogf");
	if (!FS.exist("$game_meshes$", fn))
		return ::luabind::object();

	IRenderVisual* visual = ::Render->model_Create(path);
	IKinematics* K = visual ? visual->dcast_PKinematics() : nullptr;
	::luabind::object result;
	if (K)
	{
		result = ::luabind::newtable(L);
		for (u16 i = 0; i < K->LL_BoneCount(); ++i)
			result[i + 1] = K->LL_BoneName_dbg(i);
	}
	if (visual)
		::Render->model_Delete(visual);
	return result;
}
