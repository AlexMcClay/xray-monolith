////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_stats.h
//	Description : Gunsmith part tree - weapon stats combined from its parts
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_tree.h"

namespace gunsmith
{
	// An old-style addon the weapon has attached but the tree has no slot for
	// (for example a scope made attachable by an upgrade). It still counts, so
	// stats stay exactly as they were before the tree existed.
	struct SExtraPart
	{
		shared_str section;
		u8 legacy_addon;
	};

	// Modifiers from every part, computed once per tree or condition change.
	// Stats are kept relative to the weapon's own values (which upgrades and
	// scripts change in place), so read sites use apply(stat, base_value).
	struct SWeaponStats
	{
		float add[eStatCount];
		float mul[eStatCount];

		float ergonomics; // 0..100, receiver `ergonomics` (default 50) plus parts
		float weight_add; // parts' weight
		float cost_add; // parts' cost
		float barrel_wear_mul; // product of muzzle parts' barrel_wear_mul
		float muzzle_condition; // lowest condition of muzzle parts with a suppression threshold, 1 if none
		bool suppressor_worn; // a muzzle part is below its suppression_min_condition
		float sight_sway; // zoomed movement sway multiplier from worn sights, 1 if none
		bool can_fire; // every required slot is filled

		SWeaponStats() { reset(); }
		void reset();

		float apply(u8 stat, float base) const { return (base + add[stat]) * mul[stat]; }
	};

	void compute(const CTree& tree, const xr_vector<SExtraPart>& extra, SWeaponStats& out);

	void dump(const SWeaponStats& stats);
}
