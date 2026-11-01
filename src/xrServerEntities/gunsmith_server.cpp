////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_server.cpp
//	Description : Gunsmith trees on the server objects: a weapon's part tree and a part item's sub-tree,
//	              saved and loaded (CSE_ALifeItem, CSE_ALifeItemWeapon in xrServer_Objects_ALife_Items.cpp)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrMessages.h"
#include "xrServer_Objects_ALife_Items.h"
#include "gunsmith_tree.h"
#include "clsid_game.h"

namespace
{
	// A saved block (CSE_Abstract::ext_*) as a packet to read; false when the object has none.
	bool read_block(CSE_Abstract& se, LPCSTR name, NET_Packet& p)
	{
		const xr_vector<u8>* b = se.ext_block(name);
		if (!b)
			return false;
		p.B.count = u32(b->size());
		CopyMemory(p.B.data, b->data(), b->size());
		p.read_start();
		return true;
	}
}

void CSE_ALifeItem::gs_set_subtree(const gunsmith::CTree* tree)
{
	if (!tree || tree->size() <= 1)
	{
		xr_delete(m_gs_subtree);
		return;
	}
	if (!m_gs_subtree)
		m_gs_subtree = xr_new<gunsmith::CTree>();
	*m_gs_subtree = *tree;
	m_gs_subtree->set_part_root(true);
}

void CSE_ALifeItem::gs_write_subtree()
{
	// A part item carries the parts mounted on it (plain part items only), in the "gs_subtree" block
	// after the state, not in it.
	if (m_tClassID == CLSID_IITEM_ATTACH && m_gs_subtree)
	{
		NET_Packet p;
		p.write_start();
		m_gs_subtree->write(p);
		ext_set("gs_subtree", p.B.data, p.B.count);
	}
	else
		ext_remove("gs_subtree");
}

void CSE_ALifeItem::gs_read_subtree(NET_Packet& tNetPacket)
{
	// Packets without the block (vanilla saves, script-built state packets) leave the item with what it had.
	NET_Packet block;
	if (m_tClassID != CLSID_IITEM_ATTACH || !read_block(*this, "gs_subtree", block))
		return;
	gunsmith::CTree tree;
	tree.set_part_root(true);
	if (tree.read(block))
		tree.validate(*s_name);
	gs_set_subtree(&tree);
}

gunsmith::CTree& CSE_ALifeItemWeapon::gs_tree()
{
	if (!m_gs_tree)
		m_gs_tree = xr_new<gunsmith::CTree>();
	return *m_gs_tree;
}

void CSE_ALifeItemWeapon::gs_ensure_tree()
{
	gunsmith::CTree& tree = gs_tree();
	if (tree.empty())
		tree.build_default(*s_name, m_addon_flags.get());
	else
		tree.sync_legacy_flags(m_addon_flags.get());
}

void CSE_ALifeItemWeapon::gs_set_tree(const gunsmith::CTree& tree)
{
	gunsmith::CTree& t = gs_tree();
	t = tree;
	t.validate(*s_name);

	// Flags the tree has slots for follow the tree (a part whose pack was
	// removed clears its flag). Other flags are kept as they were.
	const u8 mask = t.legacy_mask();
	m_addon_flags.flags = u8((m_addon_flags.flags & ~mask) | (t.legacy_addon_flags() & mask));
}

void CSE_ALifeItemWeapon::gs_clone_tree(CSE_ALifeItemWeapon* parent)
{
	if (!parent || !parent->m_gs_tree)
	{
		xr_delete(m_gs_tree);
		return;
	}
	// The clone may be a different weapon section: re-root the tree and drop parts that no longer fit.
	gs_tree() = *parent->m_gs_tree;
	m_gs_tree->validate(*s_name);
}

void CSE_ALifeItemWeapon::gs_write_tree()
{
	// The "gs_tree" block after the state, not in it. A weapon that never spawned has no tree yet: it is built
	// when it does, from the addon flags saved with it.
	if (!m_gs_tree || m_gs_tree->empty())
	{
		ext_remove("gs_tree");
		return;
	}
	m_gs_tree->sync_legacy_flags(m_addon_flags.get());
	NET_Packet p;
	p.write_start();
	m_gs_tree->write(p);
	ext_set("gs_tree", p.B.data, p.B.count);
}

void CSE_ALifeItemWeapon::gs_read_tree(NET_Packet& tNetPacket)
{
	// Packets without the block (vanilla saves, script-built state packets) leave the weapon's tree as it is.
	NET_Packet block;
	if (!read_block(*this, "gs_tree", block))
		return;
	gunsmith::CTree tree;
	if (tree.read(block))
		gs_set_tree(tree);
}
