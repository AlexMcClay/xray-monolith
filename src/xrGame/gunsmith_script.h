////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_script.h
//	Description : Gunsmith part tree <-> Lua table conversion
//	              (include after pch_script.h)
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_tree.h"

namespace gunsmith
{
	// Node numbers are 0-based everywhere (node 0 is the receiver), as in gs_dump_tree.
	// The returned array is 1-based, so element n + 1 is node n:
	//   { index, section, parent, slot, condition, kind, legacy_addon, slots = {
	//       { index, section, accepts = {tag, ...}, required, field_swappable, implicit,
	//         legacy_addon, occupant, blocked }, ... } }
	// parent and slot are absent on the receiver; legacy_addon and occupant are absent when unset.
	::luabind::object tree_to_lua(const CTree& tree);

	// Builds a tree from an array of { section, parent, slot, condition }.
	// Element 1 is the receiver: its section, if given, must be `receiver`, and its
	// condition is the weapon's. Every other element needs a section, a parent node
	// number lower than its own, and a slot; condition defaults to 1.
	// Returns false with a message in `error` when the build is malformed.
	bool tree_from_lua(const ::luabind::object& nodes, LPCSTR receiver, float receiver_condition, CTree& out,
	                   xr_string& error);
}
