////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_item.cpp
//	Description : CInventoryItemObject's Gunsmith code: a part item carrying the parts mounted on it
//	              (inventory_item_object.h)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "inventory_item_object.h"
#include "gunsmith_tree.h"
#include "gunsmith_visuals.h"
#include "clsid_game.h"
#include "xrServer_Objects_ALife_Items.h"
#include "../Include/xrRender/Kinematics.h"

bool CInventoryItemObject::gs_carries_parts() const
{
	return m_gs_subtree && m_gs_subtree->size() > 1;
}

const gunsmith::CTree& CInventoryItemObject::gs_subtree() const
{
	static const gunsmith::CTree empty; // before the first spawn
	return m_gs_subtree ? *m_gs_subtree : empty;
}

gunsmith::CVisuals& CInventoryItemObject::gs_part_visuals()
{
	if (!m_gs_visuals)
		m_gs_visuals = xr_new<gunsmith::CVisuals>();
	return *m_gs_visuals;
}

void CInventoryItemObject::RenderAttachments()
{
	CPhysicItem::RenderAttachments();
	IKinematics* K = gs_carries_parts() && Visual() ? Visual()->dcast_PKinematics() : nullptr;
	if (!K)
		return;
	gunsmith::CVisuals& v = gs_part_visuals();
	v.ensure(gunsmith::CVisuals::eWorld, K, *m_gs_subtree, cNameSect().c_str(), nullptr, true);
	v.render(gunsmith::CVisuals::eWorld, K, XFORM());
}

void CInventoryItemObject::gs_spawn_parts(CSE_Abstract* DC)
{
	// Gunsmith: the parts a part item carries come with its spawn data.
	if (!m_gs_subtree)
		m_gs_subtree = xr_new<gunsmith::CTree>();
	m_gs_subtree->clear();
	m_gs_subtree->set_part_root(true);
	CSE_ALifeItem* se = smart_cast<CSE_ALifeItem*>(DC);
	if (se && se->m_tClassID == CLSID_IITEM_ATTACH && se->m_gs_subtree.size() > 1)
	{
		*m_gs_subtree = se->m_gs_subtree;
		m_gs_subtree->set_part_root(true);
	}
}
