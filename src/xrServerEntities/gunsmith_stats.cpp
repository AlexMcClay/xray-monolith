////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_stats.cpp
//	Description : Gunsmith part tree - weapon stats combined from its parts
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_stats.h"

namespace gunsmith
{
	namespace
	{
		// Which old *_k multipliers each old addon type applied
		// (CWeaponMagazined::LoadSilencerKoeffs / LoadScopeKoeffs, LoadLauncherKoeffs).
		// crosshair_inertion_k was loaded but never applied, so it stays unapplied.
		bool legacy_applies(u8 legacy_addon, u8 stat)
		{
			switch (stat)
			{
			case eStatCamDispersion:
			case eStatCamDispersionInc:
			case eStatPdmBase:
			case eStatPdmAccel:
			case eStatPdmVel:
			case eStatZoomRotateTime:
				return legacy_addon != eLegacyNone;
			case eStatFireDispersion:
			case eStatHitPower:
			case eStatHitImpulse:
			case eStatBulletSpeed:
			case eStatConditionShotDec:
				return legacy_addon == eLegacySilencer;
			default:
				return false;
			}
		}

		void take_part(LPCSTR section, u8 legacy_addon, float condition, SWeaponStats& out)
		{
			const SPartData& p = part_data(section);

			for (u32 s = 0; s < eStatCount; ++s)
			{
				out.add[s] += p.add[s];
				if (p.has_mul[s])
					out.mul[s] *= p.mul[s];
				else if (legacy_applies(legacy_addon, u8(s)))
					out.mul[s] *= p.legacy_k[s];
			}

			if (legacy_addon != eLegacyNone)
			{
				out.weight_add += p.legacy_weight;
				out.cost_add += p.legacy_cost;
			}
			else
			{
				out.weight_add += p.weight;
				out.cost_add += p.cost;
			}

			if (p.suppressor)
				out.suppressor_part = true;
			if (p.ammo_mag_size >= 0)
				out.mag_size = p.ammo_mag_size;

			if (p.condition_dispersion_factor > 0.f)
				out.mul[eStatFireDispersion] *= 1.f + p.condition_dispersion_factor * (1.f - condition);

			if (!xr_strcmp(p.kind.c_str(), "muzzle"))
			{
				out.barrel_wear_mul *= p.barrel_wear_mul;
				if (p.suppression_min_condition > 0.f)
				{
					out.muzzle_condition = _min(out.muzzle_condition, condition);
					if (condition < p.suppression_min_condition)
						out.suppressor_worn = true;
				}
			}
			else if (!xr_strcmp(p.kind.c_str(), "sight") && condition < p.jam_start_condition)
				out.sight_sway += (p.jam_start_condition - condition) * settings().sight_sway_factor;
		}
	}

	void SWeaponStats::reset()
	{
		for (u32 s = 0; s < eStatCount; ++s)
		{
			add[s] = 0.f;
			mul[s] = 1.f;
		}
		ergonomics = 50.f;
		weight_add = cost_add = 0.f;
		barrel_wear_mul = 1.f;
		muzzle_condition = 1.f;
		suppressor_worn = false;
		sight_sway = 1.f;
		can_fire = true;
		suppressor_part = false;
		mag_size = -1;
	}

	void compute(const CTree& tree, const xr_vector<SExtraPart>& extra, SWeaponStats& out)
	{
		out.reset();

		for (u32 i = 1; i < tree.size(); ++i)
			take_part(tree.node(u8(i)).section.c_str(), tree.node_legacy_addon(u8(i)), tree.node(u8(i)).condition, out);
		for (const SExtraPart& e : extra)
			take_part(e.section.c_str(), e.legacy_addon, 1.f, out);

		// Ergonomics: 50 is neutral. Lower is slower to aim and sways more.
		const SSettings& cfg = settings();
		const float base = tree.empty() ? cfg.ergo_base
			: READ_IF_EXISTS(pSettings, r_float, tree.node(0).section.c_str(), "ergonomics", cfg.ergo_base);
		out.ergonomics = out.apply(eStatErgonomics, base);
		clamp(out.ergonomics, 0.f, 100.f);
		const float delta = (cfg.ergo_base - out.ergonomics) / 100.f;
		out.mul[eStatZoomRotateTime] *= _max(0.1f, 1.f + cfg.ergo_aim_k * delta);
		out.mul[eStatPdmBase] *= _max(0.1f, 1.f + cfg.ergo_sway_k * delta);

		out.can_fire = tree.empty() || tree.required_slots_filled();
	}

	void dump(const SWeaponStats& s)
	{
		Msg("- [gunsmith] stats: ergonomics=%.1f weight+%.3f cost+%.0f can_fire=%s", s.ergonomics, s.weight_add, s.cost_add, s.can_fire ? "yes" : "NO");
		Msg("-   barrel_wear_mul=%.3f muzzle_condition=%.3f suppressor_worn=%s sight_sway=%.3f",
		    s.barrel_wear_mul, s.muzzle_condition, s.suppressor_worn ? "yes" : "no", s.sight_sway);
		for (u32 i = 0; i < eStatCount; ++i)
			if (!fsimilar(s.mul[i], 1.f) || !fis_zero(s.add[i]))
				Msg("-   %s: add %+.5f mul x%.4f", stat_name(u8(i)), s.add[i], s.mul[i]);
	}
}
