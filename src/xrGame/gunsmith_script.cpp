////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_script.cpp
//	Description : Gunsmith part tree <-> Lua table conversion
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "gunsmith_script.h"
#include "inventory_item_object.h"
#include "Level.h"
#include "Weapon.h"
#include "xrServer_Objects_ALife_Items.h"
#include "ai_space.h"
#include "script_engine.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/RenderVisual.h"

namespace gunsmith
{
	::luabind::object tree_to_lua(lua_State* L, const CTree& tree)
	{
		::luabind::object result = ::luabind::newtable(L);

		// Every slot section a fitted part blocks, worked out once (is_slot_blocked walks every part per slot)
		xr_vector<shared_str> blocked;
		for (u32 i = 1; i < tree.size(); ++i)
			for (const shared_str& b : part_data(tree.node(u8(i)).section).blocks_slots)
				blocked.push_back(b);

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
			t["mode"] = int(n.mode);
			t["kind"] = i == 0 ? "receiver" : part_data(n.section).kind.c_str();
			const u8 legacy = tree.node_legacy_addon(u8(i));
			if (legacy != eLegacyNone)
				t["legacy_addon"] = legacy_addon_name(legacy);

			::luabind::object slot_list = ::luabind::newtable(L);
			const SLOTS& slots = tree.node_slots(u8(i));
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
				st["blocked"] = std::find(blocked.begin(), blocked.end(), info.section) != blocked.end();
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

	bool tree_from_lua(const ::luabind::object& nodes, LPCSTR receiver, CTree& out, xr_string& error)
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
		out.add_node(receiver, GS_NO_PARENT, 0);

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

			double parent = 0.0, slot = 0.0;
			read_number(row, "parent", parent);
			if (!read_number(row, "slot", slot))
			{
				xr_sprintf(msg, "node %u [%s] has no slot", node, section);
				error = msg;
				return false;
			}

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
			double mode = 0.0;
			read_number(row, "mode", mode);
			const int added = out.add_node(section, u8(parent), u8(slot));
			if (added >= 0)
				out.set_mode(u8(added), u8(clampr(mode, 0.0, 255.0)));
			if (added < 0)
			{
				xr_sprintf(msg, "node %u [%s]: the tree is full (%u nodes)", node, section, u32(GS_MAX_NODES));
				error = msg;
				return false;
			}
		}
		return true;
	}
}

// Loads the part models of every Gunsmith gun and part item carrying parts on the level (their world models), so
// neither their icons nor their first sight in the world wait for files. For a loading screen. Returns how many objects.
u32 gs_preload_models()
{
	if (!g_pGameLevel)
		return 0;
	CTimer timer;
	timer.Start();
	u32 count = 0;
	for (u32 i = 0, n = Level().Objects.o_count(); i < n; ++i)
	{
		CObject* O = Level().Objects.o_get_by_iterator(i);
		if (!O || O->getDestroy() || !O->Visual())
			continue;
		IKinematics* K = O->Visual()->dcast_PKinematics();
		if (!K)
			continue;
		if (CWeapon* weapon = smart_cast<CWeapon*>(O))
		{
			if (!weapon->gs_full_mode())
				continue;
			weapon->gs_visuals().ensure(gunsmith::CVisuals::eWorld, K, weapon->gs_tree(), weapon->cNameSect().c_str(),
			                            nullptr, true);
		}
		else if (CInventoryItemObject* item = smart_cast<CInventoryItemObject*>(O))
		{
			if (!item->gs_carries_parts())
				continue;
			item->gs_part_visuals().ensure(gunsmith::CVisuals::eWorld, K, item->gs_subtree(), item->cNameSect().c_str(),
			                               nullptr, true);
		}
		else
			continue;
		++count;
	}
	Msg("- [gunsmith] part models of %u guns and parts loaded in %.0f ms", count, timer.GetElapsed_sec() * 1000.f);
	return count;
}

// A gun section's stock build (its gs_default_build) as a part tree, as game_object:gs_get_tree() gives one; empty
// for a section that isn't a full-mode gun. For previews of a section with no object (trader showcases).
::luabind::object gs_default_tree_script(lua_State* L, LPCSTR section)
{
	if (!section || !pSettings->section_exist(section) || !gunsmith::is_full_mode(section))
		return ::luabind::newtable(L);
	gunsmith::CTree tree;
	tree.build_default(section, 0);
	return gunsmith::tree_to_lua(L, tree);
}

bool gs_carries_parts(u16 id)
{
	CInventoryItemObject* item = g_pGameLevel ? smart_cast<CInventoryItemObject*>(Level().Objects.net_Find(id)) : nullptr;
	return item && item->gs_carries_parts();
}


void gs_refresh_stats_script()
{
	if (!g_pGameLevel)
		return;
	for (u32 i = 0, n = Level().Objects.o_count(); i < n; ++i)
		if (CWeapon* weapon = smart_cast<CWeapon*>(Level().Objects.o_get_by_iterator(i)))
			if (!weapon->getDestroy())
				weapon->gs_rebuild_stats();
}

// [gunsmith_settings] values that can change while the game runs (gunsmith::set_setting). get: nil if unknown.
bool gs_set_setting(LPCSTR name, float value)
{
	return name && gunsmith::set_setting(name, value);
}

::luabind::object gs_get_setting_script(lua_State* L, LPCSTR name)
{
	float v;
	if (!name || !gunsmith::get_setting(name, v))
		return ::luabind::object();
	return ::luabind::object(L, v);
}

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

// Part tree access on a weapon's server object, for guns that are
// offline (NPC loadouts, traders, stashes). Same table format as game_object:gs_get_tree.
// Takes the calling lua_State (raw<1>): see gunsmith_script.h.
::luabind::object cse_gs_get_tree_script(lua_State* L, CSE_ALifeItemWeapon* weapon)
{
	weapon->gs_ensure_tree();
	return gunsmith::tree_to_lua(L, weapon->gs_tree());
}

bool cse_gs_set_tree_script(CSE_ALifeItemWeapon* weapon, ::luabind::object nodes)
{
	gunsmith::CTree tree;
	xr_string error;
	if (!gunsmith::tree_from_lua(nodes, weapon->s_name.c_str(), tree, error))
	{
		Msg("! cse_alife_item_weapon:gs_set_tree [%s]: %s", weapon->s_name.c_str(), error.c_str());
		return false;
	}
	weapon->gs_set_tree(tree);
	return true;
}

// The gunsmith module (from CLevel::script_register).
void gunsmith_script_register(lua_State* L)
{
	using namespace luabind;
	module(L, "gunsmith")
		[
		def("model_bones", &gs_model_bones_script, raw<1>()),
		def("default_tree", &gs_default_tree_script, raw<1>()),
		def("preload_models", &gs_preload_models),
		def("is_full_mode", &gs_is_full_mode),
		def("set_setting", &gs_set_setting),
		def("get_setting", &gs_get_setting_script, raw<1>()),
		def("refresh_stats", &gs_refresh_stats_script),
		def("carries_parts", &gs_carries_parts)
	];
}
