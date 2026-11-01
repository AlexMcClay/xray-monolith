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

		void take_part(LPCSTR section, u8 legacy_addon, SWeaponStats& out)
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
		}
	}

	void SWeaponStats::reset()
	{
		for (u32 s = 0; s < eStatCount; ++s)
		{
			add[s] = script_add[s] = 0.f;
			mul[s] = script_mul[s] = 1.f;
		}
		ergonomics = stock_ergonomics = 50.f;
		recoil_sum = stock_recoil_sum = 0.f;
		shoulder = stock_shoulder = false;
		weight_add = cost_add = 0.f;
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
				take_part(tree.node(u8(i)).section.c_str(), tree.node_legacy_addon(u8(i)), out);
			for (const SExtraPart& e : extra)
				take_part(e.section.c_str(), e.legacy_addon, out);

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
			t.build_default(section.c_str(), 0);
			compute_raw(t, xr_vector<SExtraPart>(), s);
			return cache.insert(mk_pair(section, s)).first->second;
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
}
