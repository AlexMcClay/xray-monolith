////////////////////////////////////////////////////////////////////////////
//	Module 		: Weapon_gunsmith.cpp
//	Description : CWeapon's Gunsmith code: the part tree, stats, sights, parts, devices and aiming through a
//	              fitted sight. Kept out of Weapon.cpp so that file stays close to upstream.
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Weapon.h"
#include "inventory.h"
#include "InventoryOwner.h"
#include "xrserver_objects_alife_items.h"
#include "actor.h"
#include "level.h"
#include "../Include/xrRender/Kinematics.h"
#include "ai_object_location.h"
#include "player_hud.h"
#include "gamepersistent.h"
#include "clsid_game.h"
#include "WeaponMagazinedWGrenade.h"
#include "pch_script.h"
#include "script_game_object.h"
#include "xrServer.h"
#include "game_sv_base.h"
#include "alife_simulator.h"
#include "xrMessages.h"

extern float scope_radius;

CSE_ALifeItemWeapon* CWeapon::gs_server_entity() const
{
	if (!OnServer() || !Level().Server || !Level().Server->game)
		return nullptr;
	return smart_cast<CSE_ALifeItemWeapon*>(Level().Server->game->get_entity_from_eid(ID()));
}

void CWeapon::gs_sync_to_server()
{
	CSE_ALifeItemWeapon* se = gs_server_entity();
	if (!se)
		return;
	se->gs_tree() = m_gs_tree;
	se->m_addon_flags.flags = m_flagsAddOnState;
}

void CWeapon::gs_set_legacy_addon(u8 legacy_addon, LPCSTR part)
{
	const bool detach = !part || !*part;
	shared_str detached_section;
	if (detach)
	{
		const int node = m_gs_tree.find_legacy(legacy_addon);
		if (node >= 0)
			detached_section = m_gs_tree.node(u8(node)).section;
	}
	m_gs_tree.set_legacy_addon(legacy_addon, part);
	gs_commit_tree(false); // callers run InitAddons next

	if (detach)
	{
		// The item spawns asynchronously here, so its id isn't known (-1).
		if (detached_section.size() && !m_gs_api_detach)
			gs_callback_detached(detached_section.c_str(), -1);
	}
	else
	{
		const int node = m_gs_tree.find_legacy(legacy_addon);
		if (node >= 0)
			gs_callback_attached(u8(node), part);
	}
}

void CWeapon::gs_extra_parts(const gunsmith::CTree& tree, xr_vector<gunsmith::SExtraPart>& extra) const
{
	const u8 mask = tree.legacy_mask();
	auto add_extra = [&](u8 addon, ALife::EWeaponAddonStatus status, const shared_str& section)
	{
		if (status == ALife::eAddonAttachable && (m_flagsAddOnState & addon) && !(mask & addon) && section.size())
		{
			gunsmith::SExtraPart e;
			e.section = section;
			e.legacy_addon = addon;
			extra.push_back(e);
		}
	};
	add_extra(gunsmith::eLegacyScope, m_eScopeStatus, m_cur_scope < m_scopes.size() ? GetScopeName() : shared_str());
	add_extra(gunsmith::eLegacySilencer, m_eSilencerStatus, m_sSilencerName);
	add_extra(gunsmith::eLegacyLauncher, m_eGrenadeLauncherStatus, m_sGrenadeLauncherName);
}

void CWeapon::gs_compute_stats(const gunsmith::CTree& tree, gunsmith::SWeaponStats& out, bool with_addons) const
{
	xr_vector<gunsmith::SExtraPart> extra;
	gs_extra_parts(tree, extra);
	gunsmith::compute(tree, extra, out, with_addons);
}

void CWeapon::gs_display_stats(gunsmith::SWeaponStats& out) const
{
	gs_compute_stats(m_gs_tree, out, true);
	for (u32 s = 0; s < gunsmith::eStatCount; ++s)
	{
		out.script_add[s] = m_gs_stats.script_add[s];
		out.script_mul[s] = m_gs_stats.script_mul[s];
	}
	out.ergonomics = m_gs_stats.ergonomics;
	out.no_stock = m_gs_stats.no_stock;
}

void CWeapon::gs_rebuild_stats(bool notify)
{
	const bool was_suppressed = IsSuppressorFitted();
	gs_compute_stats(m_gs_tree, m_gs_stats, false);
	gs_script_stats(m_gs_tree, m_gs_stats, false);

	// Magazine parts set the magazine size; without one the weapon's own size comes back.
	// Guns that never had one are left alone, so upgrades that change the size keep working.
	if (m_gs_stats.mag_size >= 0 && m_gs_stats.mag_size != m_gs_applied_mag_size)
	{
		m_gs_applied_mag_size = m_gs_stats.mag_size;
		gs_apply_mag_size(m_gs_stats.mag_size, notify);
	}
	else if (m_gs_stats.mag_size < 0 && m_gs_applied_mag_size >= 0)
	{
		m_gs_applied_mag_size = -1;
		gs_apply_mag_size(m_gs_base_mag_size, notify);
	}

	if (notify && was_suppressed != IsSuppressorFitted())
		gs_on_suppression_changed();
	gs_update_optic();
}

void CWeapon::gs_script_stats(const gunsmith::CTree& tree, gunsmith::SWeaponStats& stats, bool preview)
{
	::luabind::functor<void> funct;
	if (!ai().script_engine().functor("_G.CWeapon_OnRecalcStats", funct))
		return;
	lua_State* L = ai().script_engine().lua();
	::luabind::object t = ::luabind::newtable(L);
	t["section"] = cNameSect().c_str();
	t["preview"] = preview;
	t["can_fire"] = stats.can_fire;
	::luabind::object parts = ::luabind::newtable(L);
	int n = 0;
	for (u32 i = 1; i < tree.size(); ++i)
		parts[++n] = tree.node(u8(i)).section.c_str();
	xr_vector<gunsmith::SExtraPart> extra;
	gs_extra_parts(tree, extra);
	for (const gunsmith::SExtraPart& e : extra)
		parts[++n] = e.section.c_str();
	t["parts"] = parts;
	::luabind::object stock = ::luabind::newtable(L);
	n = 0;
	for (const shared_str& s : gunsmith::stock_parts(cNameSect()))
		stock[++n] = s.c_str();
	t["stock_parts"] = stock;
	t["mul"] = ::luabind::newtable(L);
	t["add"] = ::luabind::newtable(L);
	funct(lua_game_object(), t);

	// What the handlers left: display values, and t.mul and t.add by stat name; anything not a finite number is
	// ignored.
	::luabind::object ergo = t["ergonomics"];
	if (ergo.type() == LUA_TNUMBER)
	{
		const float f = ::luabind::object_cast<float>(ergo);
		if (_valid(f))
			stats.ergonomics = f;
	}
	::luabind::object no_stock = t["no_stock"];
	stats.no_stock = no_stock.type() == LUA_TBOOLEAN && ::luabind::object_cast<bool>(no_stock);

	::luabind::object mul = t["mul"], add = t["add"];
	for (u32 s = 0; s < gunsmith::eStatCount; ++s)
	{
		LPCSTR name = gunsmith::stat_name(u8(s));
		if (mul.type() == LUA_TTABLE)
		{
			::luabind::object v = mul[name];
			if (v.type() == LUA_TNUMBER)
			{
				const float f = ::luabind::object_cast<float>(v);
				if (_valid(f))
					stats.script_mul[s] = f;
			}
		}
		if (add.type() == LUA_TTABLE)
		{
			::luabind::object v = add[name];
			if (v.type() == LUA_TNUMBER)
			{
				const float f = ::luabind::object_cast<float>(v);
				if (_valid(f))
					stats.script_add[s] = f;
			}
		}
	}
}

const gunsmith::SPartVisual* CWeapon::gs_sight_part(shared_str* section) const
{
	if (!m_gs_full_mode)
		return nullptr;
	const u8 node = gs_sight_node(); // the same choice CVisuals makes (gunsmith::sight_node)
	if (!node)
		return nullptr;
	const gunsmith::SNode& n = m_gs_tree.node(node);
	if (section)
		*section = n.section;
	return &gunsmith::part_visual(n.section);
}

u8 CWeapon::gs_sight_node() const
{
	return m_gs_full_mode ? gunsmith::sight_node(m_gs_tree, gunsmith::selected_sight(m_gs_tree)) : 0;
}

void CWeapon::gs_sight_list(xr_vector<u8>& out) const
{
	out.clear();
	if (!m_gs_full_mode)
		return;
	for (u8 i = 1; i < m_gs_tree.size(); ++i)
		if (gunsmith::is_cyclable_sight(gunsmith::part_visual(m_gs_tree.node(i).section)))
			out.push_back(i);
}

bool CWeapon::gs_sight_select(u8 node)
{
	xr_vector<u8> sights;
	gs_sight_list(sights);
	if (std::find(sights.begin(), sights.end(), node) == sights.end())
		return false;
	m_gs_tree.set_selected_sight(node);
	gs_update_optic(); // zoom, overlay and lens follow; the aim correction re-predicts on its own
	gs_sync_to_server();
	return true;
}

bool CWeapon::gs_sight_set(u8 node, u8 sight, u8 mode)
{
	if (node == 0 || node >= m_gs_tree.size())
		return false;
	const gunsmith::SPartVisual& pv = gunsmith::part_visual(m_gs_tree.node(node).section);
	if (!gunsmith::is_cyclable_sight(pv))
		return false;
	if (!pv.is_tactical())
	{
		if (sight >= pv.sight_count() || mode >= pv.mode_count())
			return false;
		m_gs_tree.set_mode(node, gunsmith::make_sight_mode(sight, mode));
	}
	return gs_sight_select(node); // the zoom, overlay and lens follow there
}

bool CWeapon::gs_sight_aim(gunsmith::SSightAim& out) const
{
	const u8 node = gs_sight_node();
	if (!node)
		return false;
	const gunsmith::SNode& n = m_gs_tree.node(node);
	out = gunsmith::sight_aim(gunsmith::part_visual(n.section), n.mode);
	return true;
}

u8 CWeapon::gs_sight_mode(u8* count) const
{
	const u8 node = gs_sight_node();
	const gunsmith::SPartVisual* pv = node ? &gunsmith::part_visual(m_gs_tree.node(node).section) : nullptr;
	const bool modes = pv && !pv->is_tactical() && gunsmith::node_sight(*pv, m_gs_tree.node(node).mode) == 0;
	if (count)
		*count = modes ? pv->mode_count() : 1;
	return modes ? gunsmith::node_sight_mode(*pv, m_gs_tree.node(node).mode) : 0;
}

const gunsmith::SPartVisual* CWeapon::gs_optic() const
{
	if (ALife::eAddonPermanent != m_eScopeStatus && 0 != (m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope) && m_scopes.size())
		return nullptr; // a vanilla scope addon brings its own zoom
	gunsmith::SSightAim aim;
	const gunsmith::SPartVisual* pv = gs_sight_part();
	return pv && gs_sight_aim(aim) && aim.optic() ? pv : nullptr; // (its chosen sight and mode magnify)
}

// Zoom follows the fitted optic: re-read the zoom parameters when it changes, and when aiming
// right now, jump to the new optic's zoom.
void CWeapon::gs_update_optic()
{
	shared_str sect;
	const gunsmith::SPartVisual* pv = gs_sight_part(&sect);
	gunsmith::SSightAim aim;
	if (!pv || !gs_sight_aim(aim) || !aim.optic())
		sect = nullptr;
	else
	{
		string256 k; // the section, sight and mode: a mode switch is a different optic
		xr_sprintf(k, "%s:%u", sect.c_str(), u32(m_gs_tree.node(gs_sight_node()).mode));
		sect = k;
	}
	if (sect == m_gs_optic_sect)
		return;
	m_gs_optic_sect = sect;
	m_fRTZoomFactor = 0.f;
	UpdateUIScope(); // ends with UpdateZoomParams()
	if (IsZoomed())
	{
		firstZoomDone = true;
		if (m_zoom_params.m_bUseDynamicZoom)
		{
			m_fRTZoomFactor = GetMinScopeZoomFactor();
			SetZoomFactor(scope_radius > 0.0 ? m_fRTZoomFactor / scope_scrollpower : m_fRTZoomFactor);
		}
		else
			SetZoomFactor(CurrentZoomFactor());
	}
	else
		firstZoomDone = false;
}

float CWeapon::gs_pdm_base() const
{
	return m_gs_stats.apply(gunsmith::eStatPdmBase, m_pdm.m_fPDM_disp_base);
}

float CWeapon::gs_pdm_vel() const
{
	return m_gs_stats.apply(gunsmith::eStatPdmVel, m_pdm.m_fPDM_disp_vel_factor);
}

float CWeapon::gs_pdm_accel() const
{
	return m_gs_stats.apply(gunsmith::eStatPdmAccel, m_pdm.m_fPDM_disp_accel_factor);
}

float CWeapon::gs_shot_time(float base_time) const
{
	if (base_time <= 0.f)
		return base_time;
	const float rpm = m_gs_stats.apply(gunsmith::eStatRpm, 60.f / base_time);
	return rpm > 1.f ? 60.f / rpm : base_time;
}

void CWeapon::gs_modify_bullet(float& speed, float& hit_power, float& impulse)
{
	speed = m_gs_stats.apply(gunsmith::eStatBulletSpeed, speed);
	hit_power = m_gs_stats.apply(gunsmith::eStatHitPower, hit_power);
	impulse = m_gs_stats.apply(gunsmith::eStatHitImpulse, impulse);
}

void CWeapon::gs_apply_shot_wear(float impair)
{
	if (m_gs_shot_wear >= 0.f)
		ChangeCondition(-m_gs_shot_wear * f_weapon_deterioration);
	else
		ChangeCondition(-m_gs_stats.apply(gunsmith::eStatConditionShotDec, GetWeaponDeterioration()) * impair *
			cur_silencer_koef.condition_shot_dec);
	m_gs_shot_wear = -1.f;
}

bool CWeapon::gs_script_shot(u8& failure, u8& node)
{
	m_gs_shot_wear = -1.f;
	::luabind::functor<::luabind::object> funct;
	if (!ai().script_engine().functor("_G.CWeapon_OnShot", funct))
		return false;
	lua_State* L = ai().script_engine().lua();
	::luabind::object t = ::luabind::newtable(L);
	t["handled"] = false;
	t["condition"] = GetCondition();
	t["rounds_left"] = _max(iAmmoElapsed - 1, 0); // after this shot
	t["magazine_size"] = iMagazineSize;
	if (!m_magazine.empty())
		t["ammo"] = m_magazine.back().m_ammoSect.c_str(); // the round being fired
	CGameObject* owner = smart_cast<CGameObject*>(H_Parent());
	funct(lua_game_object(), owner ? owner->lua_game_object() : nullptr, t);

	::luabind::object handled = t["handled"];
	if (handled.type() != LUA_TBOOLEAN || !::luabind::object_cast<bool>(handled))
		return false;
	::luabind::object wear = t["wear"];
	if (wear.type() == LUA_TNUMBER)
	{
		const float w = ::luabind::object_cast<float>(wear);
		if (_valid(w) && w >= 0.f)
			m_gs_shot_wear = w;
	}
	::luabind::object malfunction = t["malfunction"];
	if (malfunction.type() == LUA_TSTRING)
		failure = gunsmith::failure_from_name(::luabind::object_cast<LPCSTR>(malfunction));
	::luabind::object culprit = t["node"];
	if (culprit.type() == LUA_TNUMBER)
		node = u8(_max(0, ::luabind::object_cast<int>(culprit)));
	return true;
}

bool CWeapon::gs_legacy_accepts(u8 legacy_addon, LPCSTR section) const
{
	// Mirrors CWeaponMagazined::CanAttach for each addon type.
	switch (legacy_addon)
	{
	case gunsmith::eLegacySilencer:
		return m_eSilencerStatus == ALife::eAddonAttachable && !xr_strcmp(m_sSilencerName.c_str(), section);
	case gunsmith::eLegacyLauncher:
		return m_eGrenadeLauncherStatus == ALife::eAddonAttachable && !xr_strcmp(m_sGrenadeLauncherName.c_str(), section);
	case gunsmith::eLegacyScope:
		if (m_eScopeStatus != ALife::eAddonAttachable)
			return false;
		for (const shared_str& scope : m_scopes)
		{
			if (m_modular_attachments)
			{
				if (!xr_strcmp(scope.c_str(), section))
					return true;
			}
			else if (pSettings->line_exist(scope, "scope_name") && !xr_strcmp(pSettings->r_string(scope, "scope_name"), section))
				return true;
		}
		return false;
	default:
		return false;
	}
}

LPCSTR CWeapon::gs_check_place(const gunsmith::CTree& tree, LPCSTR section, u8 parent, u8 slot, bool preview) const
{
	if (tree.empty())
		return "no_tree";
	if (parent >= tree.size())
		return "no_such_node";
	const gunsmith::SLOTS& slots = tree.node_slots(parent);
	if (slot >= slots.size())
		return "no_such_slot";
	if (!section || !*section || !pSettings->section_exist(section))
		return "no_such_section";
	if (tree.find_in_slot(parent, slot) >= 0)
		return "slot_taken";
	if (tree.is_slot_blocked(slots[slot].section))
		return "slot_blocked";
	// both ways: a fitted part blocking this slot (above), or this part blocking a slot that is in use
	if (tree.blocks_filled_slot(section))
		return "blocks_filled_slot";
	// `conflicts`: either part naming a tag of the other
	if (tree.conflict_for(section))
		return "conflict";

	LPCSTR receiver = tree.node(0).section.c_str();
	const u8 legacy = slots[slot].legacy_addon;
	if (legacy != gunsmith::eLegacyNone)
	{
		const bool fits = preview
			? (gunsmith::part_fits_slot(section, slots[slot], receiver, true) || gunsmith::is_legacy_part(receiver, legacy, section))
			: gs_legacy_accepts(legacy, section);
		if (!fits)
			return "does_not_fit";
	}
	else if (!gunsmith::part_fits_slot(section, slots[slot], receiver, true))
		return "does_not_fit";

	if (tree.size() >= gunsmith::GS_MAX_NODES)
		return "tree_full";
	return "";
}

bool CWeapon::gs_refuse_place(LPCSTR section, u8 parent, u8 slot) const
{
	LPCSTR reason = gs_check_place(m_gs_tree, section, parent, slot, false);
	if (*reason)
	{
		Msg("! [gunsmith] [%s] cannot attach [%s] to node %u slot %u: %s", cNameSect().c_str(), section ? section : "", u32(parent), u32(slot), reason);
		return true;
	}
	const gunsmith::SLOTS& slots = m_gs_tree.node_slots(parent);
	if (slots[slot].legacy_addon != gunsmith::eLegacyNone)
	{
		Msg("! [gunsmith] [%s] slot %u is an addon slot (%s): attach the item itself", cNameSect().c_str(), u32(slot), gunsmith::legacy_addon_name(slots[slot].legacy_addon));
		return true;
	}
	return false;
}

void CWeapon::gs_commit_tree(bool notify)
{
	gs_rebuild_stats(notify);
	gs_sync_to_server();
}

bool CWeapon::gs_attach_part(LPCSTR section, u8 parent, u8 slot)
{
	if (gs_refuse_place(section, parent, slot))
		return false;
	const int node = m_gs_tree.add_node(section, parent, slot);
	if (node < 0)
		return false;
	gs_commit_tree();
	gs_callback_attached(u8(node), section);
	return true;
}

u16 CWeapon::gs_spawn_part_item(LPCSTR section, const gunsmith::CTree* subtree)
{
	if (!OnServer() || !ai().get_alife())
		return u16(-1);
	if (!pSettings->section_exist(section) || !pSettings->line_exist(section, "class"))
		return u16(-1);

	// Same as alife():create(section, ..., parent): build the server object, then spawn
	// it through the server so the id is known right away.
	CALifeSimulator& alife = const_cast<CALifeSimulator&>(ai().alife());
	const u16 parent_id = H_Parent() ? H_Parent()->ID() : u16(-1);
	CSE_Abstract* item = alife.spawn_item(section, Position(), ai_location().level_vertex_id(),
	                                      ai_location().game_vertex_id(), parent_id, false);
	if (!item)
		return u16(-1);
	if (CSE_ALifeItem* part = smart_cast<CSE_ALifeItem*>(item))
		part->gs_set_subtree(subtree);

	NET_Packet packet;
	packet.w_begin(M_SPAWN);
	packet.w_stringZ(item->s_name);
	item->Spawn_Write(packet, FALSE);
	alife.server().FreeID(item->ID, 0);
	F_entity_Destroy(item);

	ClientID clientID;
	clientID.set(0xffff);
	u16 dummy;
	packet.r_begin(dummy);
	CSE_Abstract* spawned = alife.server().Process_spawn(packet, clientID);
	return spawned ? spawned->ID : u16(-1);
}

bool CWeapon::gs_detach_part(u8 node, bool spawn, u16& item_id)
{
	item_id = u16(-1);
	if (node == 0 || node >= m_gs_tree.size())
	{
		Msg("! [gunsmith] [%s] gs_detach: there is no part node %u", cNameSect().c_str(), u32(node));
		return false;
	}

	// The part and everything mounted on it, in tree order (parents come first).
	struct SRemoved
	{
		shared_str section;
	};
	xr_vector<SRemoved> removed;
	xr_vector<u8> in_subtree;
	in_subtree.resize(m_gs_tree.size(), 0);
	for (u32 i = node; i < m_gs_tree.size(); ++i)
	{
		const gunsmith::SNode& n = m_gs_tree.node(u8(i));
		in_subtree[i] = u8(i == node || in_subtree[n.parent]);
		if (in_subtree[i])
			removed.push_back({ n.section });
	}

	const u8 legacy = m_gs_tree.node_legacy_addon(node);

	// A plain part item (II_ATTCH) with parts mounted on it comes off as one item that
	// carries them; anything else comes off as one item per part.
	gunsmith::CTree subtree;
	m_gs_tree.extract_subtree(node, subtree);
	const bool bundle = spawn && legacy == gunsmith::eLegacyNone && removed.size() > 1 &&
		pSettings->line_exist(removed[0].section, "class") &&
		pSettings->r_clsid(removed[0].section, "class") == CLSID_IITEM_ATTACH;

	if (spawn)
		for (const SRemoved& r : removed)
		{
			if (!pSettings->line_exist(r.section, "class"))
			{
				Msg("! [gunsmith] [%s] gs_detach: [%s] is not an inventory item; detach with spawn = false to discard it", cNameSect().c_str(), r.section.c_str());
				return false;
			}
			if (bundle)
				break; // only the carrying part has to be an item
		}

	if (legacy != gunsmith::eLegacyNone)
	{
		// Old-style addon: the weapon's own detach code clears the flag, visuals and
		// stats; the item is spawned below so its id is known.
		m_gs_api_detach = true;
		Detach(removed[0].section.c_str(), false);
		m_gs_api_detach = false;
		if (m_gs_tree.find_legacy(legacy) >= 0)
		{
			Msg("! [gunsmith] [%s] gs_detach: the weapon refused to detach [%s]", cNameSect().c_str(), removed[0].section.c_str());
			return false;
		}
	}
	else
	{
		m_gs_tree.remove_subtree(node);
		gs_commit_tree();
	}

	if (bundle)
	{
		item_id = gs_spawn_part_item(removed[0].section.c_str(), &subtree);
		gs_callback_detached(removed[0].section.c_str(), item_id == u16(-1) ? -1 : int(item_id));
		return true;
	}

	for (const SRemoved& r : removed)
	{
		const u16 id = spawn ? gs_spawn_part_item(r.section.c_str()) : u16(-1);
		if (item_id == u16(-1))
			item_id = id;
		gs_callback_detached(r.section.c_str(), id == u16(-1) ? -1 : int(id));
	}
	return true;
}

bool CWeapon::gs_attach_item(CInventoryItem* item, u8 parent, u8 slot)
{
	LPCSTR section = item->object().cNameSect().c_str();

	// Parts carried by the item, from its server object.
	const gunsmith::CTree* subtree = nullptr;
	if (OnServer() && Level().Server && Level().Server->game)
		if (CSE_ALifeItem* se = smart_cast<CSE_ALifeItem*>(Level().Server->game->get_entity_from_eid(item->object().ID())))
			if (se->m_tClassID == CLSID_IITEM_ATTACH)
				subtree = se->m_gs_subtree;

	if (!subtree)
		return gs_attach_part(section, parent, slot);

	if (gs_refuse_place(section, parent, slot))
		return false;
	const int root = m_gs_tree.graft(*subtree, parent, slot);
	if (root < 0)
	{
		Msg("! [gunsmith] [%s] cannot attach [%s] with its %u part(s): the tree is full", cNameSect().c_str(), section, u32(subtree->size() - 1));
		return false;
	}
	// Carried parts that no longer fit their slots are dropped and logged, as on load.
	m_gs_tree.validate(cNameSect().c_str());
	gs_commit_tree();
	gs_callback_attached(u8(root), section);
	return true;
}

void CWeapon::gs_stat_values(const gunsmith::SWeaponStats& stats, u8 stat, float& base, float& add, float& value) const
{
	float b = 0.f;
	bool angle = false;
	switch (stat)
	{
	case gunsmith::eStatFireDispersion: b = fireDispersionBase; angle = true; break;
	case gunsmith::eStatCamDispersion: b = cam_recoil.Dispersion; angle = true; break;
	case gunsmith::eStatCamDispersionInc: b = cam_recoil.DispersionInc; angle = true; break;
	case gunsmith::eStatCamHorz: b = cam_recoil.StepAngleHorz; angle = true; break;
	case gunsmith::eStatHitPower: b = fvHitPower[g_SingleGameDifficulty]; break;
	case gunsmith::eStatHitImpulse: b = fHitImpulse; break;
	case gunsmith::eStatBulletSpeed: b = m_fStartBulletSpeed; break;
	case gunsmith::eStatRpm: b = fOneShotTime > 0.f ? 60.f / fOneShotTime : 0.f; break;
	case gunsmith::eStatPdmBase: b = m_pdm.m_fPDM_disp_base; break;
	case gunsmith::eStatPdmAccel: b = m_pdm.m_fPDM_disp_accel_factor; break;
	case gunsmith::eStatPdmVel: b = m_pdm.m_fPDM_disp_vel_factor; break;
	case gunsmith::eStatCrosshairInertion: b = m_crosshair_inertion; break;
	case gunsmith::eStatZoomRotateTime: b = m_zoom_params.m_fZoomRotateTime; break;
	case gunsmith::eStatConditionShotDec: b = conditionDecreasePerShot; break;
	case gunsmith::eStatCamRelax: b = cam_recoil.RelaxSpeed; angle = true; break;
	default: break;
	}

	float v;
	if (stat == gunsmith::eStatCamHorz)
		v = b * stats.horz_k(); // horizontal recoil only uses the multiplier
	else
		v = stats.apply(stat, b);
	float a = stat < gunsmith::eStatCount ? stats.add[stat] + stats.script_add[stat] : 0.f;

	if (angle)
	{
		b = rad2deg(b);
		v = rad2deg(v);
		a = rad2deg(a);
	}
	base = b;
	add = a;
	value = v;
}

void CWeapon::gs_callback_attached(u8 node, LPCSTR section)
{
	::luabind::functor<void> funct;
	if (ai().script_engine().functor("_G.CWeapon_gs_part_attached", funct))
		funct(lua_game_object(), int(node), section);
}

void CWeapon::gs_callback_detached(LPCSTR section, int item_id)
{
	::luabind::functor<void> funct;
	if (ai().script_engine().functor("_G.CWeapon_gs_part_detached", funct))
		funct(lua_game_object(), section, item_id);
}

void CWeapon::gs_callback_failed(u8 node, u8 failure)
{
	::luabind::functor<void> funct;
	if (ai().script_engine().functor("_G.CWeapon_gs_part_failed", funct))
		funct(lua_game_object(), int(node), gunsmith::failure_name(failure));
}

void CWeapon::gs_match_scope_part()
{
	if (m_eScopeStatus != ALife::eAddonAttachable || m_scopes.empty() || m_cur_scope >= m_scopes.size() ||
		!(m_flagsAddOnState & CSE_ALifeItemWeapon::eWeaponAddonScope))
		return;

	const shared_str scope = GetScopeName();
	if (!scope.size())
		return;

	const int node = m_gs_tree.find_legacy(gunsmith::eLegacyScope);
	if (node >= 0 && m_gs_tree.node(u8(node)).section == scope)
		return;

	// Trees rebuilt from old saves guess the first scope; the saved scope index wins.
	m_gs_tree.set_legacy_addon(gunsmith::eLegacyScope, scope.c_str());
	gs_sync_to_server();
}

// Console gs_debug, a bit mask (Weapon_gunsmith_deps.h): 1 preview log, 2 grip log, 4 aim log
// (the sight calibration, twice a second while aiming).
int g_gs_debug = 0;

namespace
{
	// The camera-space rotation that lays a sight line (forward f, up u, unit, camera space: x right, y up,
	// z forward) along the view: forward onto +z, then, from aim_roll_min degrees (Tarkov's SCOPE_ROTATION_THRESHOLD),
	// up onto +y (a canted sight rolls the gun upright). turn and roll are the angles applied (radians).
	void gs_sight_line_rotation(const Fvector& f, const Fvector& u, Fmatrix& out, float& turn, float& roll)
	{
		const Fvector z = {0.f, 0.f, 1.f};
		Fmatrix r1;
		r1.identity();
		turn = acosf(_max(-1.f, _min(1.f, f.dotproduct(z))));
		if (turn > EPS_L)
		{
			Fvector axis;
			axis.crossproduct(f, z);
			if (axis.magnitude() > EPS_L)
			{
				axis.normalize();
				r1.rotation(axis, turn);
				Fvector t;
				r1.transform_dir(t, f);
				if (t.dotproduct(z) < 1.f - 1e-4f) // the other way round
					r1.rotation(axis, -turn);
			}
		}
		Fvector u1;
		r1.transform_dir(u1, u);
		roll = atan2f(u1.x, u1.y); // the sight's up, off the camera's up
		Fmatrix r2;
		r2.identity();
		if (_abs(roll) >= deg2rad(gunsmith::settings().aim_roll_min))
		{
			r2.rotation(z, roll);
			Fvector t;
			r2.transform_dir(t, u1);
			if (_abs(atan2f(t.x, t.y)) > deg2rad(0.1f))
				r2.rotation(z, -roll);
		}
		else
			roll = 0.f;
		out.mul_43(r2, r1);
	}
}

void CWeapon::RenderAttachments()
{
	inherited::RenderAttachments();
	IKinematics* K = m_gs_full_mode && Visual() ? Visual()->dcast_PKinematics() : nullptr;
	if (!K)
		return;
	m_gs_visuals.ensure(gunsmith::CVisuals::eWorld, K, m_gs_tree, cNameSect().c_str(), nullptr, true);
	m_gs_visuals.render(gunsmith::CVisuals::eWorld, K, XFORM());
	m_gs_devices.render(gunsmith::CVisuals::eWorld);
}

// Laser beams and dots are in the world in first person too. The gun on the HUD isn't rendered in the world, so
// then CActor::renderable_Render draws them (from_actor); otherwise CWeapon::renderable_Render. Main phase, every
// traversal of it: the renderer keeps each visual only in the traversal of its shader's priority.
void CWeapon::gs_render_world(bool from_actor)
{
	if (!m_gs_full_mode || ::Render->active_phase() != 0 || from_actor != !!GetHUDmode())
		return;
	m_gs_devices.render_world();
}

void CWeapon::render_hud_attachments(IKinematics* model, const Fmatrix& xform)
{
	if (!m_gs_full_mode || !model)
		return;
	m_gs_visuals.ensure(gunsmith::CVisuals::eHud, model, m_gs_tree, cNameSect().c_str(), HudSection().c_str(), true);
	m_gs_visuals.render(gunsmith::CVisuals::eHud, model, xform);
	m_gs_devices.render(gunsmith::CVisuals::eHud);
}

bool CWeapon::gs_has_devices() const
{
	for (u8 i = 1; i < m_gs_tree.size(); ++i)
		if (gunsmith::part_visual(m_gs_tree.node(i).section).is_tactical())
			return true;
	return false;
}

bool CWeapon::gs_devices_set_on(bool on)
{
	if (m_gs_tree.empty() || !gs_has_devices())
		return false;
	m_gs_tree.set_devices_on(on);
	gs_sync_to_server();
	return true;
}

bool CWeapon::gs_set_device_mode(u8 node, u8 mode)
{
	if (node == 0 || node >= m_gs_tree.size())
		return false;
	const gunsmith::SPartVisual& pv = gunsmith::part_visual(m_gs_tree.node(node).section);
	if (!pv.is_tactical() || mode >= pv.tac_modes.size())
		return false;
	m_gs_tree.set_mode(node, mode);
	gs_sync_to_server();
	return true;
}

// Devices light on a holder's active weapon while they are on: the actor's on the HUD model in first person and on
// the world model otherwise; other holders' (NPCs, whose scripts switch them) on the world model, within the
// npc_devices_* limits.
void CWeapon::gs_update_devices()
{
	if (!m_gs_full_mode)
		return;
	// Other guns (the bag's, holstered ones) skip the device list's rebuild check each frame and keep whatever
	// they built, off. It is rebuilt when in hand.
	CObject* holder = H_Parent();
	CInventoryOwner* owner = smart_cast<CInventoryOwner*>(holder);
	if (!owner || owner->inventory().ActiveItem() != this || !gs_devices_on())
	{
		m_gs_devices.off();
		return;
	}
	CActor* actor = smart_cast<CActor*>(holder);
	if (!actor)
	{
		gs_update_npc_devices(holder);
		return;
	}
	m_gs_devices.sync(m_gs_tree);
	if (m_gs_devices.empty())
		return;
	attachable_hud_item* hi = GetHUDmode() ? HudItemData() : nullptr;
	if (hi && hi->m_model)
	{
		m_gs_visuals.ensure(gunsmith::CVisuals::eHud, hi->m_model, m_gs_tree, cNameSect().c_str(), HudSection().c_str(), true);
		// the HUD projection the barrel ray uses (CHudItem::Ray), to cast the laser in the world
		Fmatrix proj;
		proj.build_projection(deg2rad(GetBaseHudFov() * 83.f), Device.fASPECT, R_VIEWPORT_NEAR,
							  g_pGamePersistent->Environment().CurrentEnv->far_plane);
		m_gs_devices.update(m_gs_tree, m_gs_visuals, gunsmith::CVisuals::eHud, hi->m_model, hi->m_item_transform, &proj, actor, true,
							gunsmith::settings().device_shadows);
	}
	else
	{
		IKinematics* K = Visual() ? Visual()->dcast_PKinematics() : nullptr;
		if (!K)
		{
			m_gs_devices.off();
			return;
		}
		m_gs_devices.update(m_gs_tree, m_gs_visuals, gunsmith::CVisuals::eWorld, K, XFORM(), nullptr, actor, true,
							gunsmith::settings().device_shadows);
	}
}

// An NPC's devices: the first npc_devices_max guns a frame within npc_devices_distance of the camera light up,
// unshadowed; their lasers only near the camera and in front of it.
void CWeapon::gs_update_npc_devices(CObject* holder)
{
	const gunsmith::SSettings& gs = gunsmith::settings();
	static u32 frame = u32(-1), lit = 0;
	if (frame != Device.dwFrame)
	{
		frame = Device.dwFrame;
		lit = 0;
	}
	Fvector to;
	to.sub(holder->Position(), Device.vCameraPosition);
	const float dist = to.magnitude();
	IKinematics* K = Visual() ? Visual()->dcast_PKinematics() : nullptr;
	if (!K || dist > gs.npc_devices_distance || lit >= gs.npc_devices_max)
	{
		m_gs_devices.off();
		return;
	}
	m_gs_devices.sync(m_gs_tree);
	if (m_gs_devices.empty())
		return;
	++lit;
	if (dist > EPS_L)
		to.div(dist);
	const bool lasers = dist <= gs.npc_laser_distance && to.dotproduct(Device.vCameraDirection) > 0.3f;
	m_gs_devices.update(m_gs_tree, m_gs_visuals, gunsmith::CVisuals::eWorld, K, XFORM(), nullptr, holder, false, 0, lasers);
}

bool CWeapon::hud_fire_point(IKinematics* model, const Fmatrix& xform, Fvector& pos)
{
	if (!m_gs_full_mode || !model)
		return false;
	m_gs_visuals.ensure(gunsmith::CVisuals::eHud, model, m_gs_tree, cNameSect().c_str(), HudSection().c_str(), true);
	return m_gs_visuals.fire_point(gunsmith::CVisuals::eHud, model, xform, pos);
}

void CWeapon::DeleteHudItemData()
{
	m_gs_visuals.clear(gunsmith::CVisuals::eHud); // the HUD model they mount on goes away
	inherited::DeleteHudItemData();
}

// Hooks from Weapon.cpp.

void CWeapon::gs_world_fire_point(const Fmatrix& parent)
{
	// The last part on the muzzle chain moves the fire point.
	IKinematics* K = m_gs_full_mode && Visual() ? Visual()->dcast_PKinematics() : nullptr;
	if (K)
	{
		Fvector gs_fp;
		m_gs_visuals.ensure(gunsmith::CVisuals::eWorld, K, m_gs_tree, cNameSect().c_str(), nullptr, true);
		if (m_gs_visuals.fire_point(gunsmith::CVisuals::eWorld, K, parent, gs_fp))
		{
			m_current_firedeps.vLastFP = gs_fp;
			m_current_firedeps.vLastFPSilencer = gs_fp;
		}
	}
}

bool CWeapon::gs_zoom_params(float zoom_multiple, u32& stepCount)
{
	gunsmith::SSightAim optic_aim; // a Gunsmith optic: its chosen sight and mode (a mode has its own zoom)
	if (!(gs_optic() && gs_sight_aim(optic_aim)))
		return false;

	const gunsmith::SSightAim* optic = &optic_aim;
	// Gunsmith optic: magnification relative to a fixed 1x FOV ([gunsmith_settings]
	// optic_base_fov), so a 4x scope zooms the same whatever the player's FOV setting, and
	// about as far as vanilla's scopes. Zoom factors are FOVs in degrees divided by 0.75.
	auto mag_to_zoom = [](float mag) {
		const float base = _min(float(g_fov), gunsmith::settings().optic_base_fov > 1.f ? gunsmith::settings().optic_base_fov : float(g_fov));
		const float half = deg2rad(base) * 0.5f;
		return rad2deg(2.f * atanf(tanf(half) / _max(mag, 0.01f))) / 0.75f;
	};
	m_zoom_params.m_fScopeZoomFactor = (optic->zoom_factor > 0.f ? optic->zoom_factor : mag_to_zoom(optic->magnification)) / zoom_multiple;
	m_zoom_params.m_bUseDynamicZoom = optic->magnification_min > 0.f && optic->magnification_min < optic->magnification;
	if (m_zoom_params.m_bUseDynamicZoom)
	{
		m_zoom_params.m_fMinBaseZoomFactor = mag_to_zoom(optic->magnification_min);
		stepCount = optic->zoom_steps ? optic->zoom_steps : 4;
	}
	return true;
}

extern CUIXml* pWpnScopeXml;

bool CWeapon::gs_scope_texture(shared_str& scope_tex_name)
{
	gunsmith::SSightAim gs_optic_aim;
	if (!(gs_optic() && gs_sight_aim(gs_optic_aim)))
		return false;

	scope_tex_name = gs_optic_aim.scope_texture; // empty: none (3D scopes look through the model)
	if (scope_tex_name.size() && pWpnScopeXml == nullptr)
	{
		pWpnScopeXml = new CUIXml();
		pWpnScopeXml->Load(CONFIG_PATH, UI_PATH, "scopes.xml");
	}
	return true;
}

void CWeapon::gs_load(LPCSTR section)
{
	m_gs_base_mag_size = iMagazineSize;
	if (gunsmith::is_full_mode(section))
	{
		// Full-mode guns take no old-style addons, whatever the section says: scopes, suppressors and
		// launchers are parts there. Vanilla attach code then refuses them and hides their bones.
		m_eScopeStatus = m_eSilencerStatus = m_eGrenadeLauncherStatus = ALife::eAddonDisabled;
	}
}

void CWeapon::gs_on_spawn(CSE_ALifeItemWeapon* E)
{
	E->gs_ensure_tree();
	m_gs_tree = E->gs_tree();
	m_gs_full_mode = gunsmith::is_full_mode(cNameSect().c_str());
	if (m_gs_full_mode)
		m_flagsAddOnState = 0; // no old-style addons on full-mode guns (Load disabled them); loadout scripts may still set the flags
}

void CWeapon::gs_after_spawn()
{
	gs_match_scope_part();
	gs_rebuild_stats(false); // InitAddons below picks the suppressed or plain effects
	// A new gun is spawned with its section's rounds (ammo_elapsed), which can be more than its magazine part
	// holds (a shotgun tube part shorter than the section's). What doesn't fit is dropped, or the magazine
	// would count as full past its size.
	if (m_gs_applied_mag_size >= 0 && iAmmoElapsed > iMagazineSize)
		iAmmoElapsed = iMagazineSize;
}

// A script may decide the shot (the weapon_on_shot callback; the Gunsmith Framework does Tarkov's malfunctions
// there): its wear and whether, how and by which part it malfunctions. Otherwise CheckForMisfire uses the
// receiver's own misfire curve (vanilla's misfire_* keys).
BOOL CWeapon::gs_misfire(u8 failure, u8 node)
{
	if (failure == gunsmith::eFailNone)
		return FALSE;

	m_gs_jam_type = failure;
	m_gs_jam_node = node < m_gs_tree.size() ? node : 0;
	StopShooting();
	bMisfire = true;
	gs_callback_failed(m_gs_jam_node, m_gs_jam_type);
	return TRUE;
}

bool CWeapon::gs_fire_blocked()
{
	if (!IsMisfire() && m_fire_blocked)
	{
		OnEmptyClick();
		return true;
	}
	return false;
}

// Magazine parts (CWeaponMagazined, CWeaponMagazinedWGrenade).

void CWeaponMagazined::gs_apply_mag_size(int size, bool live)
{
	inherited::gs_apply_mag_size(size, live);
	if (!live || iAmmoElapsed <= size)
		return;

	// A smaller magazine: the rounds that no longer fit go back to the inventory.
	xr_map<shared_str, u32> back;
	while (iAmmoElapsed > size && !m_magazine.empty())
	{
		back[m_magazine.back().m_ammoSect] += 1;
		m_magazine.pop_back();
		--iAmmoElapsed;
	}
	for (auto& it : back)
		SpawnAmmo(it.second, it.first.c_str());
	m_BriefInfo_CalcFrame = 0;
}

void CWeaponMagazinedWGrenade::gs_apply_mag_size(int size, bool live)
{
	iMagazineSize2 = size; // the rifle magazine's size, kept while the launcher is selected
	if (!m_bGrenadeMode)
		inherited::gs_apply_mag_size(size, live);
}

// Aiming through the fitted sight part. Called from UpdateHudAdditional with its camera, its aim offset index
// and the aim offset it is about to apply (position, rotation, aim rotation); corrects the position and aim
// rotation so the sight's aim point sits on the camera's axis (where the screen centre and a collimator's dot
// are) with its line along the view. The target is worked out every frame for the gun as the hands' animation
// holds it, without what the HUD adds on top (shooting shake, strafe, inertia, near-wall, movement and script
// layers), which move the sight off the axis on purpose. A new sight takes its target at once; then the
// correction follows the target at aim_speed while the gun is idle and in an aiming pose, and holds while it
// fires, reloads or is lowered. An optic with aim_eye_point also gets its eye relief (z).
void CWeapon::gs_update_aim(attachable_hud_item* hi, const Fmatrix& trans, u8 idx,
	Fvector& curr_offs, const Fvector& curr_rot, Fvector& curr_aim_rot)
{
	const bool placed = m_gs_hud_placed;
	m_gs_hud_placed = true;

	// Only aiming (offset index 1) a full-mode gun without modular attachments, in the main hand.
	if (!(idx == 1 && !m_modular_attachments && m_gs_full_mode) || !placed || hi != g_player_hud->attached_item(0))
		return;

	Fvector sight;
	gunsmith::CVisuals::SSightKey key;
	if (!m_gs_visuals.sight_point(hi->m_model, sight, key))
		return;

	// The receiver in the aim offset's frame as the animation holds it (last frame's pose): the hands' offset
	// times the animated anchor bone and the item's offset. player_hud's m_transform is the camera with all the
	// HUD's offsets (this one, shake, strafe, inertia, near-wall), the hands' offset and the movement and script
	// layers; m_item_transform is m_transform times the anchor and the item's offset.
	Fmatrix hands;
	{
		Fmatrix t_inv;
		t_inv.invert(g_player_hud->m_transform);
		hands.mul_43(g_player_hud->m_attach_offset, t_inv);
		hands.mulB_43(hi->m_item_transform);
	}

	// The aim offset as UpdateHudAdditional applies it: aim_rot, then (pos, rot Z*Y*X).
	auto offset_xform = [](const Fvector& aim_rot, const Fvector& rot, const Fvector& pos) {
		Fmatrix a, r, t;
		a.identity();
		a.setHPB(aim_rot.x, aim_rot.y, aim_rot.z);
		r.identity();
		r.rotateX(rot.x);
		t.identity();
		t.rotateY(rot.y);
		r.mulA_43(t);
		t.identity();
		t.rotateZ(rot.z);
		r.mulA_43(t);
		r.translate_over(pos);
		Fmatrix m;
		m.mul_43(a, r);
		return m;
	};

	const bool new_sight = key != m_gs_aim_sight;
	// The target aim rotation: the sight line at the aim offset turned onto the view (pitch, yaw; roll from
	// aim_roll_min), applied first in camera space. A line further off than aim_turn_max: the gun isn't in an
	// aiming pose (lowered, reloading) or the data is bad, so nothing follows it.
	Fvector rot_t = new_sight ? curr_aim_rot : m_gs_aim_rot;
	bool aiming_pose = true;
	float turn = 0.f, roll = 0.f;
	Fmatrix aimed;
	aimed.mul_43(offset_xform(curr_aim_rot, curr_rot, curr_offs), hands);
	Fvector fwd, up;
	if (m_gs_visuals.sight_dir(hi->m_model, fwd, up))
	{
		Fvector f, u;
		aimed.transform_dir(f, fwd);
		aimed.transform_dir(u, up);
		f.normalize_safe();
		u.normalize_safe();
		Fmatrix line, base, turned;
		gs_sight_line_rotation(f, u, line, turn, roll);
		aiming_pose = turn < deg2rad(gunsmith::settings().aim_turn_max);
		if (aiming_pose)
		{
			base.setHPB(curr_aim_rot.x, curr_aim_rot.y, curr_aim_rot.z);
			turned.mul_43(line, base);
			turned.getHPB(rot_t);
		}
	}

	// The target position correction: where the sight lands with that rotation and no correction (camera space),
	// moved onto the axis, and an optic's eye to its eye relief; expressed inside the aim rotation, where the
	// offset's position is applied.
	aimed.mul_43(offset_xform(rot_t, curr_rot, curr_offs), hands);
	Fvector s;
	aimed.transform_tiny(s, sight);
	Fvector d = {-s.x, -s.y, 0.f};
	Fvector eye;
	bool has_eye;
	const gunsmith::SPartVisual* pv = m_gs_visuals.sight_eye(hi->m_model, eye, has_eye);
	if (pv && pv->is_optic() && has_eye)
	{
		aimed.transform_tiny(eye);
		d.z = pv->eye_offset - eye.z;
	}
	Fmatrix a, a_inv;
	a.setHPB(rot_t.x, rot_t.y, rot_t.z);
	a_inv.invert(a);
	Fvector corr_t;
	a_inv.transform_dir(corr_t, d);

	if (new_sight)
	{
		m_gs_aim_sight = key;
		m_gs_aim_rot.set(rot_t);
		m_gs_aim_corr.set(corr_t);
	}
	else if (aiming_pose && GetState() == eIdle)
	{
		const float k = _min(1.f, Device.fTimeDelta * gunsmith::settings().aim_speed);
		m_gs_aim_corr.lerp(m_gs_aim_corr, corr_t, k);
		m_gs_aim_rot.x += angle_normalize_signed(rot_t.x - m_gs_aim_rot.x) * k;
		m_gs_aim_rot.y += angle_normalize_signed(rot_t.y - m_gs_aim_rot.y) * k;
		m_gs_aim_rot.z += angle_normalize_signed(rot_t.z - m_gs_aim_rot.z) * k;
	}
	curr_aim_rot.set(m_gs_aim_rot);
	curr_offs.add(m_gs_aim_corr);

	static u32 s_gs_aim_log = 0;
	if ((g_gs_debug & eGsDebugAim) && (new_sight || Device.dwTimeGlobal > s_gs_aim_log + 500))
	{
		s_gs_aim_log = Device.dwTimeGlobal;
		// where the sight is now, with everything the HUD adds (camera space: x right, y up, z forward)
		Fmatrix cam_inv;
		cam_inv.invert(trans);
		Fvector now;
		hi->m_item_transform.transform_tiny(now, sight);
		cam_inv.transform_tiny(now);
		Msg("- gs_aim: sight [%s]%s line %.2f deg off, roll %.2f deg%s; target %.4f, %.4f, %.4f, correction %.4f, %.4f, %.4f; "
			"now off axis %.4f, %.4f (dist %.3f); zoom %.2f, state %u",
			key.name(), new_sight ? " (new)" : "", rad2deg(turn), rad2deg(roll), aiming_pose ? "" : " (not an aiming pose: held)",
			corr_t.x, corr_t.y, corr_t.z, m_gs_aim_corr.x, m_gs_aim_corr.y, m_gs_aim_corr.z,
			now.x, now.y, now.z, m_zoom_params.m_fZoomRotationFactor, GetState());
	}
}
