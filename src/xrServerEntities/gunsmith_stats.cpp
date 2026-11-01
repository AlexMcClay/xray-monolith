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
		// Which addon *_k multipliers vanilla applies per addon type
		// (CWeaponMagazined::LoadSilencerKoeffs / LoadScopeKoeffs, LoadLauncherKoeffs).
		// crosshair_inertion_k is loaded but never applied there, so it stays unapplied.
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

		// with_addons false: an old-style addon's multipliers, weight and cost are left to vanilla's code
		void take_part(LPCSTR section, u8 legacy_addon, bool with_addons, SWeaponStats& out)
		{
			const SPartData& p = part_data(section);

			for (u32 s = 0; s < eStatCount; ++s)
			{
				out.add[s] += p.add[s];
				if (!with_addons && legacy_applies(legacy_addon, u8(s)))
					continue;
				if (p.has_mul[s])
					out.mul[s] *= p.mul[s];
				else if (legacy_applies(legacy_addon, u8(s)))
					out.mul[s] *= p.legacy_k[s];
			}

			if (legacy_addon != eLegacyNone)
			{
				if (with_addons)
				{
					out.weight_add += p.legacy_weight;
					out.cost_add += p.legacy_cost;
				}
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
		}
	}

	void SWeaponStats::reset()
	{
		for (u32 s = 0; s < eStatCount; ++s)
		{
			add[s] = script_add[s] = 0.f;
			mul[s] = script_mul[s] = 1.f;
		}
		ergonomics = -1.f;
		no_stock = false;
		weight_add = cost_add = 0.f;
		can_fire = true;
		suppressor_part = false;
		mag_size = -1;
	}

	namespace
	{
		// The parts' modifiers as they are, before relating them to the stock build.
		void compute_raw(const CTree& tree, const xr_vector<SExtraPart>& extra, bool with_addons, SWeaponStats& out)
		{
			out.reset();

			for (u32 i = 1; i < tree.size(); ++i)
				take_part(tree.node(u8(i)).section.c_str(), tree.node_legacy_addon(u8(i)), with_addons, out);
			for (const SExtraPart& e : extra)
				take_part(e.section.c_str(), e.legacy_addon, with_addons, out);

			out.can_fire = tree.empty() || tree.required_slots_filled();
		}

		// A section's stock build (its gs_default_build, no old addon flags), computed once per section.
		const SWeaponStats& stock_stats(const shared_str& section)
		{
			static xr_map<shared_str, SWeaponStats> cache;
			auto it = cache.find(section);
			if (it != cache.end())
				return it->second;
			SWeaponStats s;
			CTree t;
			t.build_default(section.c_str(), 0);
			compute_raw(t, xr_vector<SExtraPart>(), true, s);
			return cache.insert(mk_pair(section, s)).first->second;
		}
	}

	void compute(const CTree& tree, const xr_vector<SExtraPart>& extra, SWeaponStats& out, bool with_addons)
	{
		compute_raw(tree, extra, with_addons, out);
		if (tree.empty())
			return;

		// Relative to the stock build: its parts' modifiers are already in the gun's own numbers.
		const SWeaponStats& st = stock_stats(tree.node(0).section);
		for (u32 s = 0; s < eStatCount; ++s)
		{
			out.add[s] -= st.add[s];
			if (!fis_zero(st.mul[s]))
				out.mul[s] /= st.mul[s];
		}
	}

	const xr_vector<shared_str>& stock_parts(const shared_str& section)
	{
		static xr_map<shared_str, xr_vector<shared_str>> cache;
		auto it = cache.find(section);
		if (it != cache.end())
			return it->second;
		xr_vector<shared_str> parts;
		CTree t;
		t.build_default(section.c_str(), 0);
		for (u32 i = 1; i < t.size(); ++i)
			parts.push_back(t.node(u8(i)).section);
		return cache.insert(mk_pair(section, parts)).first->second;
	}
}
