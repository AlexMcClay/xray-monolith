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
	// exactly as its section says (vanilla's numbers), and parts change it from there.
	//
	// On top, a script layer (script_add, script_mul) that scripts set whenever the stats are recomputed
	// (CWeapon::gs_script_stats, the weapon_on_recalc_stats callback). The parts' recoil percents and
	// ergonomics are only summed here, with the stock build's; the Gunsmith Framework turns them into
	// multipliers with Tarkov's formulas and the handling settings (gunsmith_handling.script; 1 Nov 2026,
	// before that in this file).
	struct SWeaponStats
	{
		float add[eStatCount];
		float mul[eStatCount];
		// The scripts' terms, added and multiplied on top of the parts' (0 and 1 without a script).
		float script_add[eStatCount];
		float script_mul[eStatCount];

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

		// A stat's multiplier on top of the parts' (the scripts').
		float k(u8 stat) const { return script_mul[stat]; }
		float apply(u8 stat, float base) const { return (base + add[stat] + script_add[stat]) * mul[stat] * script_mul[stat]; }
		float horz_k() const { return mul[eStatCamHorz] * script_mul[eStatCamHorz]; } // horizontal: multiplier only
		// Takes another result's script layer (the structural half and the per-shot check don't ask scripts again).
		void copy_script(const SWeaponStats& from);
	};

	void compute(const CTree& tree, const xr_vector<SExtraPart>& extra, SWeaponStats& out);

	// compute() in two halves for a shot, which changes only part conditions (1 Nov 2026): the stats as if every
	// part were new (relative to the stock build, as compute()), then the condition terms of the tree's parts
	// (spread from worn parts, a worn suppressor, sight sway). Together they make compute()'s result, up to the
	// order of the float multiplications.
	void compute_structure(const CTree& tree, const xr_vector<SExtraPart>& extra, SWeaponStats& out);
	void apply_conditions(const CTree& tree, SWeaponStats& out);

	void dump(const SWeaponStats& stats);
}
