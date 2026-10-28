////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_tree.h
//	Description : Gunsmith part tree - a weapon as a receiver plus nested parts
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_config.h"

namespace gunsmith
{
	// Save layout version written after the weapon's legacy fields. Increment it
	// only with a migration path in CTree::read.
	const u16 GS_SAVE_VERSION = 1;
	const u8 GS_NO_PARENT = 0xFF;
	const u8 GS_MAX_NODES = 64;

	struct SNode
	{
		shared_str section;
		u8 parent; // index of the parent node, GS_NO_PARENT for the receiver
		u8 slot; // index into the parent's slot list
		float condition; // 0..1

		SNode() : parent(GS_NO_PARENT), slot(0), condition(1.f) {}
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

		void clear() { m_nodes.clear(); }

		// Returns the new node's index, or -1 when the tree is full or the parent is invalid.
		int add_node(LPCSTR section, u8 parent, u8 slot, float condition);

		// Removes the node and all its descendants. Removing node 0 clears the tree.
		void remove_subtree(u8 index);

		// Index of the node occupying parent's slot, or -1.
		int find_in_slot(u8 parent, u8 slot) const;

		void set_condition(u8 index, float condition);

		// Slots that node `index` exposes.
		void node_slots(u8 index, SLOTS& out) const;

		// Save layout: gs_version u16, node_count u8, then per node
		// section stringZ, parent u8, slot u8, condition u16 (0..65535 = 0..1).
		void write(NET_Packet& packet) const;

		// Reads a tree written by write(). Never reads past the end of the packet.
		// Returns false and leaves the tree empty on an unknown version or bad data.
		bool read(NET_Packet& packet);

		// Builds a fresh tree: the receiver, its gs_default_build, then the parts
		// that the old addon flags stand for. Every node takes `condition`.
		void build_default(LPCSTR receiver, u8 addon_flags, float condition);

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
		bool set_legacy_addon(u8 legacy_addon, LPCSTR part, float condition);

		// Adds or removes legacy parts so the tree matches the old addon flags.
		void sync_legacy_flags(u8 addon_flags, float condition);

		void dump(LPCSTR label) const;

		// True when a part already in the tree lists this slot section in its blocks_slots.
		bool is_slot_blocked(const shared_str& slot_section) const;

		// Every required slot of every node is filled.
		bool required_slots_filled() const;

		// A tree whose node 0 is a part rather than a weapon: a detached part carrying
		// the parts mounted on it. Node 0's slots then come from its `slots` key, and
		// it gets no implicit addon slots. Not saved: owners set it after reading.
		void set_part_root(bool part_root) { m_part_root = part_root; }
		bool is_part_root() const { return m_part_root; }

		// Copies node `node` and everything mounted on it into `out`, re-rooted so
		// that part is node 0 (a part-root tree).
		void extract_subtree(u8 node, CTree& out) const;

		// Adds a part-root tree under `parent`'s `slot`: its node 0 becomes the part
		// in that slot (with `root_condition`), its other nodes keep their conditions.
		// Returns the new index of that part, or -1 if the slot is taken, the parent
		// doesn't exist or the tree would get too big.
		int graft(const CTree& sub, u8 parent, u8 slot, float root_condition);

	private:
		int place_in_free_slot(LPCSTR part, float condition);

		NODES m_nodes;
		bool m_part_root = false;
	};
}
