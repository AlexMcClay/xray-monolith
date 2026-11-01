////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_server.cpp
//	Description : Gunsmith trees on the server objects: a weapon's part tree and a part item's sub-tree,
//	              saved and loaded (CSE_ALifeItem, CSE_ALifeItemWeapon in xrServer_Objects_ALife_Items.cpp)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "xrMessages.h"
#include "xrServer_Objects_ALife_Items.h"
#include "clsid_game.h"

void CSE_ALifeItem::gs_write_subtree()
{
	// Gunsmith: a part item carries the parts mounted on it (plain part items only), in the "gs_subtree" block
	// after the state (CSE_Abstract::ext_*), not in it.
	if (m_tClassID == CLSID_IITEM_ATTACH && m_gs_subtree.size() > 1)
	{
		NET_Packet p;
		p.write_start();
		m_gs_subtree.write(p);
		ext_set("gs_subtree", p.B.data, p.B.count);
	}
	else
		ext_remove("gs_subtree");
}

void CSE_ALifeItem::gs_read_subtree(NET_Packet& tNetPacket)
{
	// Gunsmith sub-tree: in the state itself in saves of SPAWN_VERSION 130 (29 Oct to 1 Nov 2026), in the
	// "gs_subtree" block since. Packets with neither (vanilla saves, script-built state packets) leave the item
	// with what it had.
	if (m_tClassID != CLSID_IITEM_ATTACH)
		return;
	NET_Packet block;
	NET_Packet* from = nullptr;
	if (m_wVersion > 129 && !tNetPacket.r_eof())
		from = &tNetPacket;
	else if (const xr_vector<u8>* b = ext_block("gs_subtree"))
	{
		block.B.count = u32(b->size());
		CopyMemory(block.B.data, b->data(), b->size());
		block.read_start();
		from = &block;
	}
	if (!from)
		return;
	gunsmith::CTree tree;
	tree.set_part_root(true);
	if (tree.read(*from))
	{
		tree.validate(*s_name);
		m_gs_subtree = tree;
	}
	else
	{
		m_gs_subtree.clear();
		m_gs_subtree.set_part_root(true);
	}
}

void CSE_ALifeItemWeapon::gs_ensure_tree()
{
	if (m_gs_tree.empty())
		m_gs_tree.build_default(*s_name, m_addon_flags.get());
	else
		m_gs_tree.sync_legacy_flags(m_addon_flags.get());
}

void CSE_ALifeItemWeapon::gs_set_tree(const gunsmith::CTree& tree)
{
	m_gs_tree = tree;
	m_gs_tree.validate(*s_name);

	// Flags the tree has slots for follow the tree (a part whose pack was
	// removed clears its flag). Other flags are kept as they were.
	const u8 mask = m_gs_tree.legacy_mask();
	m_addon_flags.flags = u8((m_addon_flags.flags & ~mask) | (m_gs_tree.legacy_addon_flags() & mask));
}

void CSE_ALifeItemWeapon::gs_write_tree()
{
	gs_ensure_tree();
	NET_Packet p; // the "gs_tree" block after the state (CSE_Abstract::ext_*), not in it
	p.write_start();
	m_gs_tree.write(p);
	ext_set("gs_tree", p.B.data, p.B.count);
}

void CSE_ALifeItemWeapon::gs_read_tree(NET_Packet& tNetPacket)
{
	// Gunsmith part tree: in the state itself in saves of SPAWN_VERSION 129 and 130 (29 Oct to 1 Nov 2026), in
	// the "gs_tree" block since. Packets with neither (vanilla saves, script-built state packets) leave the
	// weapon's tree as it is: it is built from the addon flags on the next write or spawn when empty.
	if (m_wVersion > 128 && !tNetPacket.r_eof())
	{
		gunsmith::CTree tree;
		if (tree.read(tNetPacket))
			gs_set_tree(tree);
	}
	else if (const xr_vector<u8>* b = ext_block("gs_tree"))
	{
		NET_Packet block;
		block.B.count = u32(b->size());
		CopyMemory(block.B.data, b->data(), b->size());
		block.read_start();
		gunsmith::CTree tree;
		if (tree.read(block))
			gs_set_tree(tree);
	}
}
