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
	//
	// They are also relative to the gun's stock build (its gs_default_build): a stock gun handles
	// exactly as its section says (vanilla's numbers), and parts change it from there. Recoil and
	// ergonomics are kept as Tarkov's totals and turned into multipliers when read, with the current
	// handling settings (recoil_k, ergo_k), so changing those settings takes effect at once.
	struct SWeaponStats
	{
		float add[eStatCount];
		float mul[eStatCount];

		float ergonomics; // 0..100, receiver `ergonomics` (default 50) plus parts
		float recoil_sum; // parts' `recoil` percents, summed (Tarkov)
		bool shoulder; // a fitted part has shoulder_contact
		// The stock build's, for the relative effects:
		float stock_ergonomics, stock_recoil_sum;
		bool stock_shoulder;
		float weight_add; // parts' weight
		float cost_add; // parts' cost
		float barrel_wear_mul; // product of muzzle parts' barrel_wear_mul
		float muzzle_condition; // lowest condition of muzzle parts with a suppression threshold, 1 if none
		bool suppressor_worn; // a muzzle part is below its suppression_min_condition
		float sight_sway; // zoomed movement sway multiplier from worn sights, 1 if none
		bool can_fire; // every required slot is filled
		bool suppressor_part; // a part with `suppressor = true` is fitted
		int mag_size; // magazine size from the fitted magazine part (`ammo_mag_size`), -1 if none

		SWeaponStats() { reset(); }
		void reset();

		// Vertical and horizontal recoil multiplier: summed recoil against the stock build's (times
		// recoil_strength), and no_stock_recoil when the stock build rests on the shoulder and this doesn't.
		float recoil_k() const;
		// Aim time (aim_k) or sway (sway_k) multiplier from ergonomics against the stock build's.
		float ergo_k(float k) const;
		// Every stat's multiplier, the live ones included.
		float k(u8 stat) const;
		float apply(u8 stat, float base) const { return (base + add[stat]) * mul[stat] * k(stat); }
		float horz_k() const { return mul[eStatCamHorz] * k(eStatCamHorz); } // horizontal: multiplier only
	};

	void compute(const CTree& tree, const xr_vector<SExtraPart>& extra, SWeaponStats& out);

	void dump(const SWeaponStats& stats);
}
