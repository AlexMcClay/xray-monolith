////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_tree.h
//	Description : Gunsmith part tree - a weapon as a receiver plus nested parts
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_config.h"

namespace gunsmith
{
	// Save layout version. CTree::read reads this one only.
	const u16 GS_SAVE_VERSION = 2;
	const u8 GS_NO_PARENT = 0xFF;
	const u8 GS_MAX_NODES = 64;

	struct SNode
	{
		shared_str section;
		u8 parent; // index of the parent node, GS_NO_PARENT for the receiver
		u8 slot; // index into the parent's slot list
		// A tactical device's selected mode (index into its tactical_modes), or a sight part's chosen sight and
		// mode (gunsmith_visuals.h). On node 0 of a weapon tree, the weapon's own state (see devices_on).
		u8 mode;

		SNode() : parent(GS_NO_PARENT), slot(0), mode(0) {}
	};

	// Node 0 is the receiver (the weapon section itself). Every other node's parent
	// index is lower than its own, so parents always come before their children.
	class CTree
	{
	public:
		typedef xr_vector<SNode> NODES;

		bool empty() const { return m_nodes.empty(); }
		u8 size() const { return u8(m_nodes.size()); }
		const SNode& node(u8 index) const { return m_nodes[index]; }
		const NODES& nodes() const { return m_nodes; }

		void clear() { m_nodes.clear(); touch_layout(); }

		// Change counters: a new number, never reused in the session, whenever the tree changes, so
		// users can tell "changed?" with one compare instead of walking the tree. layout_revision: the parts, where
		// they sit, the part-root flag. state_revision: that and the nodes' modes (the selected sight, sight and
		// device modes). A copied tree keeps its numbers.
		u32 layout_revision() const { return m_layout_rev; }
		u32 state_revision() const { return m_state_rev; }

		// Returns the new node's index, or -1 when the tree is full or the parent is invalid.
		int add_node(LPCSTR section, u8 parent, u8 slot);

		// Removes the node and all its descendants. Removing node 0 clears the tree.
		void remove_subtree(u8 index);

		// Index of the node occupying parent's slot, or -1.
		int find_in_slot(u8 parent, u8 slot) const;

		void set_mode(u8 index, u8 mode);

		// A weapon tree's own state, saved in node 0's mode: bit 0 its tactical devices are on, bits 1-7 the node the
		// player selected to aim through (0: automatic). Not for a part-root tree, whose node 0 is a part with a mode
		// of its own. The selection follows its node when nodes are removed and renumbered (remove_subtree), and
		// goes back to automatic when that node is removed.
		bool devices_on() const { return !m_part_root && !m_nodes.empty() && (m_nodes[0].mode & 1) != 0; }
		void set_devices_on(bool on);
		u8 selected_sight() const { return m_part_root || m_nodes.empty() ? 0 : u8(m_nodes[0].mode >> 1); }
		void set_selected_sight(u8 node);

		// A node's slots (the cached list of its section; empty for a node that doesn't exist)
		const SLOTS& node_slots(u8 index) const;

		// Save layout: gs_version u16, node_count u8, then per node
		// section stringZ, parent u8, slot u8, an unused u16 (written as 1.0, ignored on read), mode u8.
		void write(NET_Packet& packet) const;

		// Reads a tree written by write(). Never reads past the end of the packet.
		// Returns false and leaves the tree empty on an unknown version or bad data.
		bool read(NET_Packet& packet);

		// Builds a fresh tree: the receiver, its gs_default_build, then the parts
		// that the old addon flags stand for.
		void build_default(LPCSTR receiver, u8 addon_flags);

		// Drops nodes whose section no longer exists or which no longer fit their
		// slot (moving them to another free fitting slot on the same parent first).
		// Each drop is logged with the section name; with `dropped` nothing is logged and
		// the sections of every dropped node (children included) are added to it instead.
		void validate(LPCSTR receiver, xr_vector<shared_str>* dropped = nullptr);

		// Legacy addon flag a node stands for, from its slot's legacy_addon.
		u8 node_legacy_addon(u8 index) const;

		// Old addon flags derived from the tree.
		u8 legacy_addon_flags() const;

		// Old addon flags the tree has a slot for. Flags outside this mask are not
		// represented by the tree and must be kept as they are.
		u8 legacy_mask() const;

		// Node standing for an old addon flag, or -1.
		int find_legacy(u8 legacy_addon) const;

		// Puts `part` into the slot for this old addon flag, replacing what was there.
		// A null or empty part only removes. Returns false if there is no free slot.
		bool set_legacy_addon(u8 legacy_addon, LPCSTR part);

		// Adds or removes legacy parts so the tree matches the old addon flags.
		void sync_legacy_flags(u8 addon_flags);

		// True when a part already in the tree lists this slot section in its blocks_slots.
		bool is_slot_blocked(const shared_str& slot_section) const;

		// Every required slot of every node is filled.
		bool required_slots_filled() const;

		// A tree whose node 0 is a part rather than a weapon: a detached part carrying
		// the parts mounted on it. Node 0's slots then come from its `slots` key, and
		// it gets no implicit addon slots. Not saved: owners set it after reading.
		void set_part_root(bool part_root)
		{
			if (m_part_root != part_root)
			{
				m_part_root = part_root;
				touch_layout();
			}
		}

		// Copies node `node` and everything mounted on it into `out`, re-rooted so
		// that part is node 0 (a part-root tree).
		void extract_subtree(u8 node, CTree& out) const;

		// Adds a part-root tree under `parent`'s `slot`: its node 0 becomes the part
		// in that slot.
		// Returns the new index of that part, or -1 if the slot is taken, the parent
		// doesn't exist or the tree would get too big.
		int graft(const CTree& sub, u8 parent, u8 slot);

	private:
		int place_in_free_slot(LPCSTR part);

		void touch_layout();
		void touch_state();

		NODES m_nodes;
		bool m_part_root = false;
		u32 m_layout_rev = 0;
		u32 m_state_rev = 0;
	};
}
