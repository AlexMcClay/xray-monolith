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

			out.recoil_sum += p.recoil;
			if (p.shoulder_contact)
				out.shoulder = true;
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
			else if ((!xr_strcmp(p.kind.c_str(), "sight") || !xr_strcmp(p.kind.c_str(), "optic")) && condition < p.jam_start_condition)
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
		ergonomics = stock_ergonomics = 50.f;
		recoil_sum = stock_recoil_sum = 0.f;
		shoulder = stock_shoulder = false;
		weight_add = cost_add = 0.f;
		barrel_wear_mul = 1.f;
		muzzle_condition = 1.f;
		suppressor_worn = false;
		sight_sway = 1.f;
		can_fire = true;
		suppressor_part = false;
		mag_size = -1;
	}

	namespace
	{
		// The parts' modifiers as they are, before relating them to the stock build.
		void compute_raw(const CTree& tree, const xr_vector<SExtraPart>& extra, SWeaponStats& out)
		{
			out.reset();

			for (u32 i = 1; i < tree.size(); ++i)
				take_part(tree.node(u8(i)).section.c_str(), tree.node_legacy_addon(u8(i)), tree.node(u8(i)).condition, out);
			for (const SExtraPart& e : extra)
				take_part(e.section.c_str(), e.legacy_addon, 1.f, out);

			const SSettings& cfg = settings();
			const float base = tree.empty() ? cfg.ergo_base
				: READ_IF_EXISTS(pSettings, r_float, tree.node(0).section.c_str(), "ergonomics", cfg.ergo_base);
			out.ergonomics = (base + out.add[eStatErgonomics]) * out.mul[eStatErgonomics];
			clamp(out.ergonomics, 0.f, 100.f);

			out.can_fire = tree.empty() || tree.required_slots_filled();
		}

		// A section's stock build (gs_default_build, new parts), computed once per section.
		const SWeaponStats& stock_stats(const shared_str& section)
		{
			static xr_map<shared_str, SWeaponStats> cache;
			auto it = cache.find(section);
			if (it != cache.end())
				return it->second;
			SWeaponStats s;
			CTree t;
			t.build_default(section.c_str(), 0, 1.f);
			compute_raw(t, xr_vector<SExtraPart>(), s);
			return cache.insert(mk_pair(section, s)).first->second;
		}
	}

	float SWeaponStats::recoil_k() const
	{
		const SSettings& cfg = settings();
		const float now = _max(0.1f, 1.f + cfg.recoil_strength * recoil_sum / 100.f);
		const float stock = _max(0.1f, 1.f + cfg.recoil_strength * stock_recoil_sum / 100.f);
		float k = now / stock;
		if (stock_shoulder && !shoulder)
			k *= cfg.no_stock_recoil;
		return k;
	}

	float SWeaponStats::ergo_k(float k) const
	{
		// Above the stock build's ergonomics: faster aiming and less sway; below: the reverse.
		return _max(0.1f, 1.f + k * settings().ergo_strength * (stock_ergonomics - ergonomics) / 100.f);
	}

	float SWeaponStats::k(u8 stat) const
	{
		switch (stat)
		{
		case eStatCamDispersion:
		case eStatCamDispersionInc:
		case eStatCamHorz: return recoil_k();
		case eStatZoomRotateTime: return ergo_k(settings().ergo_aim_k);
		case eStatPdmBase: return ergo_k(settings().ergo_sway_k);
		default: return 1.f;
		}
	}

	void compute(const CTree& tree, const xr_vector<SExtraPart>& extra, SWeaponStats& out)
	{
		compute_raw(tree, extra, out);
		if (tree.empty())
			return;

		// Relative to the stock build: its parts' modifiers are already in the gun's own numbers.
		const SWeaponStats& st = stock_stats(tree.node(0).section);
		for (u32 s = 0; s < eStatCount; ++s)
		{
			if (s == eStatErgonomics)
				continue; // ergonomics stays absolute (its effect is relative, ergo_k)
			out.add[s] -= st.add[s];
			if (!fis_zero(st.mul[s]))
				out.mul[s] /= st.mul[s];
		}
		out.stock_ergonomics = st.ergonomics;
		out.stock_recoil_sum = st.recoil_sum;
		out.stock_shoulder = st.shoulder;
	}

	void dump(const SWeaponStats& s)
	{
		Msg("- [gunsmith] stats: ergonomics=%.1f (stock %.1f) weight+%.3f cost+%.0f can_fire=%s", s.ergonomics, s.stock_ergonomics,
		    s.weight_add, s.cost_add, s.can_fire ? "yes" : "NO");
		Msg("-   recoil %+.1f%% (stock %+.1f%%) shoulder=%s (stock %s): recoil x%.3f, aim time x%.3f, sway x%.3f",
		    s.recoil_sum, s.stock_recoil_sum, s.shoulder ? "yes" : "no", s.stock_shoulder ? "yes" : "no", s.recoil_k(),
		    s.k(eStatZoomRotateTime), s.k(eStatPdmBase));
		Msg("-   barrel_wear_mul=%.3f muzzle_condition=%.3f suppressor_worn=%s sight_sway=%.3f",
		    s.barrel_wear_mul, s.muzzle_condition, s.suppressor_worn ? "yes" : "no", s.sight_sway);
		for (u32 i = 0; i < eStatCount; ++i)
			if (!fsimilar(s.mul[i], 1.f) || !fis_zero(s.add[i]))
				Msg("-   %s: add %+.5f mul x%.4f", stat_name(u8(i)), s.add[i], s.mul[i]);
	}
}
