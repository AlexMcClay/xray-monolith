////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_tree.cpp
//	Description : Gunsmith part tree - a weapon as a receiver plus nested parts
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_tree.h"
#include <atomic>

namespace gunsmith
{
	namespace
	{
		const u8 LEGACY_ADDONS[] = { eLegacyScope, eLegacySilencer, eLegacyLauncher };

		std::atomic<u32> g_revision{ 0 }; // the last revision handed out, for every tree
	}

	void CTree::touch_layout()
	{
		m_layout_rev = m_state_rev = ++g_revision;
	}

	void CTree::touch_state()
	{
		m_state_rev = ++g_revision;
	}

	int CTree::add_node(LPCSTR section, u8 parent, u8 slot)
	{
		if (!section || !*section || m_nodes.size() >= GS_MAX_NODES)
			return -1;
		if (m_nodes.empty() ? parent != GS_NO_PARENT : parent >= m_nodes.size())
			return -1;

		SNode n;
		n.section = section;
		n.parent = parent;
		n.slot = m_nodes.empty() ? 0 : slot;
		m_nodes.push_back(n);
		touch_layout();
		return int(m_nodes.size() - 1);
	}

	void CTree::remove_subtree(u8 index)
	{
		if (index >= m_nodes.size())
			return;
		if (index == 0)
		{
			clear();
			return;
		}

		const u32 count = m_nodes.size();
		xr_vector<u8> removed;
		removed.resize(count, 0);
		xr_vector<u8> remap;
		remap.resize(count, GS_NO_PARENT);
		NODES kept;
		kept.reserve(count);

		for (u32 i = 0; i < count; ++i)
		{
			// Parents come before children, so removed[parent] is already known.
			removed[i] = u8((i == index) || (i > 0 && removed[m_nodes[i].parent]));
			if (removed[i])
				continue;
			remap[i] = u8(kept.size());
			SNode n = m_nodes[i];
			if (i > 0)
				n.parent = remap[n.parent];
			kept.push_back(n);
		}
		m_nodes.swap(kept);

		// The selected sight follows its node's new number, or goes back to automatic with it removed.
		if (!m_part_root && !m_nodes.empty())
		{
			const u8 sel = u8(m_nodes[0].mode >> 1);
			if (sel)
			{
				const u8 to = sel < count && !removed[sel] ? remap[sel] : 0;
				m_nodes[0].mode = u8((m_nodes[0].mode & 1) | (to << 1));
			}
		}
		touch_layout();
	}

	int CTree::find_in_slot(u8 parent, u8 slot) const
	{
		for (u32 i = 1; i < m_nodes.size(); ++i)
			if (m_nodes[i].parent == parent && m_nodes[i].slot == slot)
				return int(i);
		return -1;
	}

	void CTree::set_mode(u8 index, u8 mode)
	{
		if (index < m_nodes.size() && m_nodes[index].mode != mode)
		{
			m_nodes[index].mode = mode;
			touch_state();
		}
	}

	void CTree::set_devices_on(bool on)
	{
		if (!m_part_root && !m_nodes.empty())
			set_mode(0, u8((m_nodes[0].mode & ~1) | (on ? 1 : 0)));
	}

	void CTree::set_selected_sight(u8 node)
	{
		if (!m_part_root && !m_nodes.empty())
			set_mode(0, u8((m_nodes[0].mode & 1) | (u8(_min(u32(node), 127u)) << 1)));
	}

	const SLOTS& CTree::node_slots(u8 index) const
	{
		static const SLOTS none;
		if (index >= m_nodes.size())
			return none;
		return slots_of(m_nodes[index].section, index == 0 && !m_part_root);
	}

	void CTree::write(NET_Packet& packet) const
	{
		packet.w_u16(GS_SAVE_VERSION);
		packet.w_u8(u8(m_nodes.size()));
		for (const SNode& n : m_nodes)
		{
			packet.w_stringZ(n.section);
			packet.w_u8(n.parent);
			packet.w_u8(n.slot);
			packet.w_float_q16(1.f, 0.f, 1.f); // unused, kept for the save layout
			packet.w_u8(n.mode);
		}
	}

	bool CTree::read(NET_Packet& packet)
	{
		clear();

		if (packet.r_elapsed() < sizeof(u16) + sizeof(u8))
			return false;

		const u16 version = packet.r_u16();
		if (version != GS_SAVE_VERSION)
		{
			Msg("! [gunsmith] part tree save version %u is not supported (engine reads %u), rebuilding from addon flags", u32(version), u32(GS_SAVE_VERSION));
			return false;
		}

		const u8 count = packet.r_u8();
		if (count > GS_MAX_NODES)
			goto corrupt; // a tree holds at most GS_MAX_NODES; the rest of the code relies on it
		for (u32 i = 0; i < count; ++i)
		{
			// The section string must end inside the packet, and the fixed fields must follow it.
			const u32 left = packet.r_elapsed();
			if (!left || !memchr(&packet.B.data[packet.r_pos], 0, left))
				goto corrupt;

			SNode n;
			packet.r_stringZ(n.section);
			if (packet.r_elapsed() < 3 * sizeof(u8) + sizeof(u16))
				goto corrupt;
			packet.r_u8(n.parent);
			packet.r_u8(n.slot);
			packet.r_u16(); // unused, kept for the save layout
			packet.r_u8(n.mode);

			if (n.section.size() == 0 || (i == 0 ? n.parent != GS_NO_PARENT : n.parent >= i))
				goto corrupt;
			m_nodes.push_back(n);
		}
		touch_layout();
		return !m_nodes.empty();

	corrupt:
		Msg("! [gunsmith] corrupt part tree in saved weapon data, rebuilding from addon flags");
		clear();
		return false;
	}

	namespace
	{
		bool lists_any(const xr_vector<shared_str>& list, const xr_vector<shared_str>& tags)
		{
			for (const shared_str& l : list)
				for (const shared_str& t : tags)
					if (l == t)
						return true;
			return false;
		}

		bool lists(const xr_vector<shared_str>& list, const shared_str& name)
		{
			for (const shared_str& l : list)
				if (l == name)
					return true;
			return false;
		}

		// `conflicts` names a part when it names one of its tags, its section, or (a weapon variant) its
		// parent_section: a conflict with a gun covers the gun's variants too.
		bool names_part(const xr_vector<shared_str>& conflicts, const shared_str& section, const SPartData& d)
		{
			return !conflicts.empty() && (lists_any(conflicts, d.tags) || lists(conflicts, section) ||
				(d.parent_section.size() && lists(conflicts, d.parent_section)));
		}

		// Two parts (or a part and the gun, node 0) conflict when either one's `conflicts` names the other.
		bool parts_conflict(const shared_str& sa, const SPartData& a, const shared_str& sb, const SPartData& b)
		{
			return names_part(a.conflicts, sb, b) || names_part(b.conflicts, sa, a);
		}
	}

	int CTree::place_in_free_slot(LPCSTR part)
	{
		if (conflict_for(part) || blocks_filled_slot(part))
			return -1;
		for (u32 i = 0; i < m_nodes.size(); ++i)
		{
			const SLOTS& slots = node_slots(u8(i));
			for (u32 s = 0; s < slots.size(); ++s)
			{
				if (slots[s].implicit || find_in_slot(u8(i), u8(s)) >= 0 || is_slot_blocked(slots[s].section))
					continue;
				if (part_fits_slot(part, slots[s], m_nodes[0].section.c_str(), true))
					return add_node(part, u8(i), u8(s));
			}
		}
		return -1;
	}

	void CTree::build_default(LPCSTR receiver, u8 addon_flags)
	{
		clear();
		add_node(receiver, GS_NO_PARENT, 0);

		LPCSTR build = READ_IF_EXISTS(pSettings, r_string, receiver, "gs_default_build", (LPCSTR)nullptr);
		if (build)
		{
			for (int i = 0, n = _GetItemCount(build); i < n; ++i)
			{
				string256 entry;
				_GetItem(build, i, entry);
				if (!xr_strlen(entry))
					continue;

				// "?entry": optional, skipped silently when its part section doesn't exist (a part from a pack
				// that isn't installed, such as a variant's sight from an optics pack).
				LPCSTR body = entry;
				const bool optional = entry[0] == '?';
				if (optional)
					++body;

				int added = -1;
				bool parent_missing = false; // an optional entry on an optional part that was skipped
				LPCSTR part = body;
				string256 part_str;
				if (_GetItemCount(body, ':') == 3)
				{
					// parent:slot:part_section; slot is an index into the parent's slots, or a slot's
					// mount_bone (Tarkov's slot name, e.g. mod_scope), so a part can go on another pack's part
					string256 parent_str, slot_str;
					_GetItem(body, 0, parent_str, ':');
					_GetItem(body, 1, slot_str, ':');
					_GetItem(body, 2, part_str, ':');
					part = part_str;
					const int parent = atoi(parent_str);
					const bool have_parent = parent >= 0 && parent < int(m_nodes.size());
					if (!have_parent)
						parent_missing = true;
					const SLOTS& slots = node_slots(have_parent ? u8(parent) : u8(GS_NO_PARENT));
					int slot = -1;
					if (slot_str[0] >= '0' && slot_str[0] <= '9')
						slot = atoi(slot_str);
					else
						for (u32 s = 0; s < slots.size() && slot < 0; ++s)
							if (!xr_strcmp(slots[s].mount_bone.c_str(), slot_str))
								slot = int(s);
					if (pSettings->section_exist(part) && slot >= 0 && slot < int(slots.size()) &&
						find_in_slot(u8(parent), u8(slot)) < 0 && !is_slot_blocked(slots[slot].section) &&
						part_fits_slot(part, slots[slot], receiver, false) && !conflict_for(part) && !blocks_filled_slot(part))
						added = add_node(part, u8(parent), u8(slot));
				}
				else if (pSettings->section_exist(part))
					added = place_in_free_slot(part);

				if (added < 0 && !(optional && (parent_missing || !pSettings->section_exist(part))))
					Msg("! [gunsmith] [%s] gs_default_build entry [%s] could not be placed", receiver, entry);
			}
		}

		sync_legacy_flags(addon_flags);
	}

	void CTree::validate(LPCSTR receiver, xr_vector<shared_str>* dropped)
	{
		if (m_nodes.empty())
			return;
		const bool quiet = dropped != nullptr;

		if (m_nodes[0].section != receiver)
		{
			if (!quiet)
				Msg("~ [gunsmith] part tree receiver [%s] renamed to [%s]", m_nodes[0].section.c_str(), receiver);
			m_nodes[0].section = receiver;
			touch_layout();
		}

		u32 i = 1;
		while (i < m_nodes.size())
		{
			SNode& n = m_nodes[i];
			LPCSTR reason = nullptr;

			if (!pSettings->section_exist(n.section))
				reason = "part section no longer exists";
			else
			{
				const SLOTS& slots = node_slots(n.parent);
				const bool slot_ok = n.slot < slots.size() && part_fits_slot(n.section.c_str(), slots[n.slot], receiver, false);
				if (!slot_ok)
				{
					// Slot list changed: move the part to a free slot it fits on the same parent.
					reason = "no slot on its parent accepts it";
					for (u32 s = 0; s < slots.size(); ++s)
					{
						if (find_in_slot(n.parent, u8(s)) < 0 && part_fits_slot(n.section.c_str(), slots[s], receiver, false))
						{
							if (!quiet)
								Msg("~ [gunsmith] [%s] part [%s] moved from slot %u to slot %u", receiver, n.section.c_str(), u32(n.slot), s);
							n.slot = u8(s);
							touch_layout();
							reason = nullptr;
							break;
						}
					}
				}
				else if (find_in_slot(n.parent, n.slot) != int(i))
					reason = "its slot is already taken";
			}

			// A part that conflicts with one fitted before it, or sits in a slot an earlier part blocks, goes (the
			// earlier part wins, as when attaching): an old save, or carried parts grafted onto a gun.
			string256 why;
			if (!reason)
			{
				const SPartData& p = part_data(n.section);
				const SLOTS& slots = node_slots(n.parent);
				const shared_str slot_sec = n.slot < slots.size() ? slots[n.slot].section : shared_str();
				for (u32 j = 0; j < i && !reason; ++j) // node 0 too: the gun itself
				{
					const SPartData& q = part_data(m_nodes[j].section);
					if (parts_conflict(n.section, p, m_nodes[j].section, q))
					{
						xr_sprintf(why, "it conflicts with [%s]", m_nodes[j].section.c_str());
						reason = why;
					}
					else if (slot_sec.size())
						for (const shared_str& b : q.blocks_slots)
							if (b == slot_sec)
							{
								xr_sprintf(why, "[%s] blocks its slot", m_nodes[j].section.c_str());
								reason = why;
								break;
							}
				}
			}

			if (reason)
			{
				if (quiet)
				{
					// The node and everything under it (parents come before their children).
					for (u32 j = i; j < m_nodes.size(); ++j)
					{
						u32 k = j;
						while (k > i)
							k = m_nodes[k].parent;
						if (k == i)
							dropped->push_back(m_nodes[j].section);
					}
				}
				else
					Msg("! [gunsmith] [%s] dropped part [%s] and its children: %s", receiver, n.section.c_str(), reason);
				remove_subtree(u8(i));
				continue;
			}
			++i;
		}
	}

	u8 CTree::node_legacy_addon(u8 index) const
	{
		if (index == 0 || index >= m_nodes.size())
			return eLegacyNone;
		const SLOTS& slots = node_slots(m_nodes[index].parent);
		return m_nodes[index].slot < slots.size() ? slots[m_nodes[index].slot].legacy_addon : u8(eLegacyNone);
	}

	u8 CTree::legacy_addon_flags() const
	{
		u8 flags = 0;
		for (u32 i = 1; i < m_nodes.size(); ++i)
			flags |= node_legacy_addon(u8(i));
		return flags;
	}

	u8 CTree::legacy_mask() const
	{
		u8 mask = 0;
		for (u32 i = 0; i < m_nodes.size(); ++i)
		{
			for (const SSlotInfo& s : node_slots(u8(i)))
				mask |= s.legacy_addon;
		}
		return mask;
	}

	int CTree::find_legacy(u8 legacy_addon) const
	{
		for (u32 i = 1; i < m_nodes.size(); ++i)
			if (node_legacy_addon(u8(i)) == legacy_addon)
				return int(i);
		return -1;
	}

	bool CTree::set_legacy_addon(u8 legacy_addon, LPCSTR part)
	{
		if (m_nodes.empty())
			return false;

		const int existing = find_legacy(legacy_addon);
		if (existing >= 0)
		{
			if (part && m_nodes[existing].section == part)
				return true;
			remove_subtree(u8(existing));
		}

		if (!part || !*part)
			return true;

		for (u32 i = 0; i < m_nodes.size(); ++i)
		{
			const SLOTS& slots = node_slots(u8(i));
			for (u32 s = 0; s < slots.size(); ++s)
				if (slots[s].legacy_addon == legacy_addon && find_in_slot(u8(i), u8(s)) < 0)
					return add_node(part, u8(i), u8(s)) >= 0;
		}

		Msg("! [gunsmith] [%s] has no free %s slot for [%s]", m_nodes[0].section.c_str(), legacy_addon_name(legacy_addon), part);
		return false;
	}

	void CTree::sync_legacy_flags(u8 addon_flags)
	{
		if (m_nodes.empty())
			return;

		const u8 mask = legacy_mask();
		for (u8 addon : LEGACY_ADDONS)
		{
			if (!(mask & addon))
				continue; // no slot for it: the flag stays authoritative on its own

			const bool want = !!(addon_flags & addon);
			const bool have = find_legacy(addon) >= 0;
			if (want && !have)
			{
				const shared_str part = legacy_default_part(m_nodes[0].section.c_str(), addon);
				if (part.size())
					set_legacy_addon(addon, part.c_str());
				else
					Msg("! [gunsmith] [%s] has the %s flag but no part section for it", m_nodes[0].section.c_str(), legacy_addon_name(addon));
			}
			else if (!want && have)
				set_legacy_addon(addon, nullptr);
		}
	}

	bool CTree::is_slot_blocked(const shared_str& slot_section) const
	{
		for (u32 i = 1; i < m_nodes.size(); ++i)
			for (const shared_str& blocked : part_data(m_nodes[i].section).blocks_slots)
				if (blocked == slot_section)
					return true;
		return false;
	}

	bool CTree::in_subtree(u32 index, u32 root) const
	{
		// Parents always come before their children.
		while (index > root && index < m_nodes.size())
			index = m_nodes[index].parent;
		return index == root;
	}

	LPCSTR CTree::conflict_for(LPCSTR part, int ignore) const
	{
		const shared_str sec(part);
		const SPartData& p = part_data(sec);
		for (u32 i = 0; i < m_nodes.size(); ++i) // node 0 too: a part can conflict with the gun itself
		{
			if (ignore >= 0 && in_subtree(i, u32(ignore)))
				continue;
			if (parts_conflict(sec, p, m_nodes[i].section, part_data(m_nodes[i].section)))
				return m_nodes[i].section.c_str();
		}
		return nullptr;
	}

	bool CTree::blocks_filled_slot(LPCSTR part, int ignore) const
	{
		const SPartData& p = part_data(part);
		if (p.blocks_slots.empty())
			return false;
		for (u32 i = 0; i < m_nodes.size(); ++i)
		{
			const SLOTS& slots = node_slots(u8(i));
			for (u32 s = 0; s < slots.size(); ++s)
			{
				const int occ = find_in_slot(u8(i), u8(s));
				if (occ < 0 || (ignore >= 0 && in_subtree(u32(occ), u32(ignore))))
					continue;
				for (const shared_str& b : p.blocks_slots)
					if (b == slots[s].section)
						return true;
			}
		}
		return false;
	}

	bool CTree::required_slots_filled() const
	{
		for (u32 i = 0; i < m_nodes.size(); ++i)
		{
			const SLOTS& slots = node_slots(u8(i));
			for (u32 s = 0; s < slots.size(); ++s)
				if (slots[s].required && find_in_slot(u8(i), u8(s)) < 0)
					return false;
		}
		return true;
	}

	void CTree::extract_subtree(u8 node, CTree& out) const
	{
		out.clear();
		out.m_part_root = true;
		if (node == 0 || node >= m_nodes.size())
			return;

		xr_vector<u8> remap;
		remap.resize(m_nodes.size(), GS_NO_PARENT);
		for (u32 i = node; i < m_nodes.size(); ++i)
		{
			const SNode& n = m_nodes[i];
			SNode c = n;
			if (i == node)
			{
				c.parent = GS_NO_PARENT;
				c.slot = 0;
			}
			else if (remap[n.parent] != GS_NO_PARENT) // parents come first, so this is known
				c.parent = remap[n.parent];
			else
				continue; // not mounted on `node`
			remap[i] = u8(out.m_nodes.size());
			out.m_nodes.push_back(c);
		}
		out.touch_layout();
	}

	int CTree::graft(const CTree& sub, u8 parent, u8 slot)
	{
		if (sub.empty() || parent >= m_nodes.size() || find_in_slot(parent, slot) >= 0 ||
			m_nodes.size() + sub.size() > GS_MAX_NODES)
			return -1;

		const u32 base = m_nodes.size();
		for (u32 i = 0; i < sub.size(); ++i)
		{
			SNode c = sub.node(u8(i));
			if (i == 0)
			{
				c.parent = parent;
				c.slot = slot;
			}
			else
				c.parent = u8(base + c.parent);
			m_nodes.push_back(c);
		}
		touch_layout();
		return int(base);
	}
}
