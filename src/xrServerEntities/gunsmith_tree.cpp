////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_tree.cpp
//	Description : Gunsmith part tree - a weapon as a receiver plus nested parts
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_tree.h"

namespace gunsmith
{
	namespace
	{
		const u8 LEGACY_ADDONS[] = { eLegacyScope, eLegacySilencer, eLegacyLauncher };
	}

	int CTree::add_node(LPCSTR section, u8 parent, u8 slot, float condition)
	{
		if (!section || !*section || m_nodes.size() >= GS_MAX_NODES)
			return -1;
		if (m_nodes.empty() ? parent != GS_NO_PARENT : parent >= m_nodes.size())
			return -1;

		SNode n;
		n.section = section;
		n.parent = parent;
		n.slot = m_nodes.empty() ? 0 : slot;
		n.condition = clampr(condition, 0.f, 1.f);
		m_nodes.push_back(n);
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
	}

	int CTree::find_in_slot(u8 parent, u8 slot) const
	{
		for (u32 i = 1; i < m_nodes.size(); ++i)
			if (m_nodes[i].parent == parent && m_nodes[i].slot == slot)
				return int(i);
		return -1;
	}

	void CTree::set_condition(u8 index, float condition)
	{
		if (index < m_nodes.size())
			m_nodes[index].condition = clampr(condition, 0.f, 1.f);
	}

	void CTree::node_slots(u8 index, SLOTS& out) const
	{
		out.clear();
		if (index < m_nodes.size())
			slots_of(m_nodes[index].section.c_str(), index == 0, out);
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
			packet.w_float_q16(clampr(n.condition, 0.f, 1.f), 0.f, 1.f);
		}
	}

	bool CTree::read(NET_Packet& packet)
	{
		clear();

		if (packet.r_elapsed() < sizeof(u16) + sizeof(u8))
			return false;

		const u16 version = packet.r_u16();
		if (version == 0 || version > GS_SAVE_VERSION)
		{
			Msg("! [gunsmith] part tree save version %u is not supported (engine reads up to %u), rebuilding from addon flags", u32(version), u32(GS_SAVE_VERSION));
			return false;
		}

		const u8 count = packet.r_u8();
		for (u32 i = 0; i < count; ++i)
		{
			// The section string must end inside the packet, and the fixed fields must follow it.
			const u32 left = packet.r_elapsed();
			if (!left || !memchr(&packet.B.data[packet.r_pos], 0, left))
				goto corrupt;

			SNode n;
			packet.r_stringZ(n.section);
			if (packet.r_elapsed() < 2 * sizeof(u8) + sizeof(u16))
				goto corrupt;
			packet.r_u8(n.parent);
			packet.r_u8(n.slot);
			packet.r_float_q16(n.condition, 0.f, 1.f);

			if (n.section.size() == 0 || (i == 0 ? n.parent != GS_NO_PARENT : n.parent >= i))
				goto corrupt;
			m_nodes.push_back(n);
		}
		return !m_nodes.empty();

	corrupt:
		Msg("! [gunsmith] corrupt part tree in saved weapon data, rebuilding from addon flags");
		clear();
		return false;
	}

	int CTree::place_in_free_slot(LPCSTR part, float condition)
	{
		SLOTS slots;
		for (u32 i = 0; i < m_nodes.size(); ++i)
		{
			node_slots(u8(i), slots);
			for (u32 s = 0; s < slots.size(); ++s)
			{
				if (slots[s].implicit || find_in_slot(u8(i), u8(s)) >= 0)
					continue;
				if (part_fits_slot(part, slots[s], m_nodes[0].section.c_str(), true))
					return add_node(part, u8(i), u8(s), condition);
			}
		}
		return -1;
	}

	void CTree::build_default(LPCSTR receiver, u8 addon_flags, float condition)
	{
		clear();
		add_node(receiver, GS_NO_PARENT, 0, condition);

		LPCSTR build = READ_IF_EXISTS(pSettings, r_string, receiver, "gs_default_build", (LPCSTR)nullptr);
		if (build)
		{
			for (int i = 0, n = _GetItemCount(build); i < n; ++i)
			{
				string256 entry;
				_GetItem(build, i, entry);
				if (!xr_strlen(entry))
					continue;

				int added = -1;
				LPCSTR part = entry;
				string256 part_str;
				if (_GetItemCount(entry, ':') == 3)
				{
					// parent:slot:part_section
					string256 parent_str, slot_str;
					_GetItem(entry, 0, parent_str, ':');
					_GetItem(entry, 1, slot_str, ':');
					_GetItem(entry, 2, part_str, ':');
					part = part_str;
					const int parent = atoi(parent_str);
					const int slot = atoi(slot_str);
					SLOTS slots;
					if (parent >= 0 && parent < int(m_nodes.size()))
						node_slots(u8(parent), slots);
					if (pSettings->section_exist(part) && slot >= 0 && slot < int(slots.size()) &&
						find_in_slot(u8(parent), u8(slot)) < 0 && part_fits_slot(part, slots[slot], receiver, false))
						added = add_node(part, u8(parent), u8(slot), condition);
				}
				else if (pSettings->section_exist(part))
					added = place_in_free_slot(part, condition);

				if (added < 0)
					Msg("! [gunsmith] [%s] gs_default_build entry [%s] could not be placed", receiver, entry);
			}
		}

		sync_legacy_flags(addon_flags, condition);
	}

	void CTree::validate(LPCSTR receiver)
	{
		if (m_nodes.empty())
			return;

		if (m_nodes[0].section != receiver)
		{
			Msg("~ [gunsmith] part tree receiver [%s] renamed to [%s]", m_nodes[0].section.c_str(), receiver);
			m_nodes[0].section = receiver;
		}

		SLOTS slots;
		u32 i = 1;
		while (i < m_nodes.size())
		{
			SNode& n = m_nodes[i];
			LPCSTR reason = nullptr;

			if (!pSettings->section_exist(n.section))
				reason = "part section no longer exists";
			else
			{
				node_slots(n.parent, slots);
				const bool slot_ok = n.slot < slots.size() && part_fits_slot(n.section.c_str(), slots[n.slot], receiver, false);
				if (!slot_ok)
				{
					// Slot list changed: move the part to a free slot it fits on the same parent.
					reason = "no slot on its parent accepts it";
					for (u32 s = 0; s < slots.size(); ++s)
					{
						if (find_in_slot(n.parent, u8(s)) < 0 && part_fits_slot(n.section.c_str(), slots[s], receiver, false))
						{
							Msg("~ [gunsmith] [%s] part [%s] moved from slot %u to slot %u", receiver, n.section.c_str(), u32(n.slot), s);
							n.slot = u8(s);
							reason = nullptr;
							break;
						}
					}
				}
				else if (find_in_slot(n.parent, n.slot) != int(i))
					reason = "its slot is already taken";
			}

			if (reason)
			{
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
		SLOTS slots;
		node_slots(m_nodes[index].parent, slots);
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
		SLOTS slots;
		for (u32 i = 0; i < m_nodes.size(); ++i)
		{
			node_slots(u8(i), slots);
			for (const SSlotInfo& s : slots)
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

	bool CTree::set_legacy_addon(u8 legacy_addon, LPCSTR part, float condition)
	{
		if (m_nodes.empty())
			return false;

		const int existing = find_legacy(legacy_addon);
		if (existing >= 0)
		{
			if (part && m_nodes[existing].section == part)
			{
				set_condition(u8(existing), condition);
				return true;
			}
			remove_subtree(u8(existing));
		}

		if (!part || !*part)
			return true;

		SLOTS slots;
		for (u32 i = 0; i < m_nodes.size(); ++i)
		{
			node_slots(u8(i), slots);
			for (u32 s = 0; s < slots.size(); ++s)
				if (slots[s].legacy_addon == legacy_addon && find_in_slot(u8(i), u8(s)) < 0)
					return add_node(part, u8(i), u8(s), condition) >= 0;
		}

		Msg("! [gunsmith] [%s] has no free %s slot for [%s]", m_nodes[0].section.c_str(), legacy_addon_name(legacy_addon), part);
		return false;
	}

	void CTree::sync_legacy_flags(u8 addon_flags, float condition)
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
					set_legacy_addon(addon, part.c_str(), condition);
				else
					Msg("! [gunsmith] [%s] has the %s flag but no part section for it", m_nodes[0].section.c_str(), legacy_addon_name(addon));
			}
			else if (!want && have)
				set_legacy_addon(addon, nullptr, 0.f);
		}
	}

	void CTree::dump(LPCSTR label) const
	{
		Msg("- [gunsmith] %s tree: %u node(s), legacy flags 0x%02x", label, u32(m_nodes.size()), u32(legacy_addon_flags()));
		SLOTS slots;
		for (u32 i = 0; i < m_nodes.size(); ++i)
		{
			const SNode& n = m_nodes[i];
			if (i == 0)
				Msg("-   #0 [%s] receiver cond=%.4f", n.section.c_str(), n.condition);
			else
				Msg("-   #%u [%s] parent=#%u slot=%u cond=%.4f", i, n.section.c_str(), u32(n.parent), u32(n.slot), n.condition);

			node_slots(u8(i), slots);
			for (u32 s = 0; s < slots.size(); ++s)
			{
				const int occupant = find_in_slot(u8(i), u8(s));
				string32 occ;
				if (occupant >= 0)
					xr_sprintf(occ, "#%d", occupant);
				else
					xr_strcpy(occ, "empty");
				Msg("-       slot %u [%s]%s%s%s -> %s", s, slots[s].section.c_str(),
				    slots[s].implicit ? " implicit" : "",
				    slots[s].required ? " required" : "",
				    slots[s].legacy_addon ? (slots[s].legacy_addon == eLegacyScope ? " legacy=scope" : slots[s].legacy_addon == eLegacySilencer ? " legacy=silencer" : " legacy=launcher") : "",
				    occ);
			}
		}
	}
}
