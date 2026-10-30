#include "stdafx.h"
#include "gunsmith_grip_ik.h"
#include "gunsmith_visuals.h"
#include "player_hud.h"
#include "Weapon.h"
#include "../xrEngine/bone.h"

int g_gs_grip_ik = 1;
int g_gs_grip_type = 0;
int g_gs_grip_debug = 0;

namespace gunsmith
{
	namespace
	{
		// Anomaly's left fingers, in the order of the grip_fingers_<type> keys (core\grip.py FINGERS)
		LPCSTR const FINGER_BONES[CGripIK::eFingers] = {
			"l_finger0", "l_finger01", "l_finger02",
			"bip01_l_finger1", "bip01_l_finger11", "bip01_l_finger12",
			"bip01_l_finger2", "bip01_l_finger21", "bip01_l_finger22",
			"bip01_l_finger3", "bip01_l_finger31", "bip01_l_finger32",
			"bip01_l_finger4", "bip01_l_finger41", "bip01_l_finger42"};

		const float NEAR_DIST = 0.03f; // the animated hand this close to the reference: full grip
		const float FAR_DIST = 0.10f;  // and this far: none

		// The smallest rotation taking direction `from` to `to` (axes as an Fmatrix, no translation).
		Fmatrix rotate_onto(Fvector from, Fvector to)
		{
			Fmatrix R;
			R.identity();
			from.normalize_safe();
			to.normalize_safe();
			Fvector k;
			k.crossproduct(from, to);
			const float s = k.magnitude();
			const float c = from.dotproduct(to);
			if (s < 1e-6f)
				return R;
			k.div(s);
			const float a = atan2f(s, c);
			const float sn = _sin(a), cs = _cos(a);
			auto rot = [&](const Fvector& v) {
				Fvector kv, out;
				kv.crossproduct(k, v);
				out.mad(Fvector().set(v).mul(cs), kv, sn);
				out.mad(k, k.dotproduct(v) * (1.f - cs));
				return out;
			};
			R.i = rot(Fvector().set(1.f, 0.f, 0.f));
			R.j = rot(Fvector().set(0.f, 1.f, 0.f));
			R.k = rot(Fvector().set(0.f, 0.f, 1.f));
			return R;
		}

		// Rotation of `a` toward `b` by t (0..1), translation kept from `a`.
		Fmatrix blend_rot(const Fmatrix& a, const Fmatrix& b, float t)
		{
			Fquaternion qa, qb, q;
			qa.set(a);
			qb.set(b);
			q.slerp(qa, qb, t);
			Fmatrix m;
			m.rotation(q);
			m.c = a.c;
			return m;
		}

		Fmatrix rot_only(const Fmatrix& m)
		{
			Fmatrix r = m;
			r.c.set(0.f, 0.f, 0.f);
			return r;
		}

		bool read_ref(LPCSTR sect, Fmatrix& out)
		{
			if (!sect || !pSettings->line_exist(sect, "gs_grip_ref"))
				return false;
			LPCSTR v = pSettings->r_string(sect, "gs_grip_ref");
			if (_GetItemCount(v) < 6)
				return false;
			float f[6];
			string64 item;
			for (int i = 0; i < 6; ++i)
				f[i] = (float)atof(_GetItem(v, i, item));
			out.setHPB(deg2rad(f[3]), deg2rad(f[4]), deg2rad(f[5]));
			out.translate_over(f[0], f[1], f[2]);
			return true;
		}
	}

	void _BCL CGripIK::callback(CBoneInstance* B)
	{
		SBone* b = static_cast<SBone*>(B->callback_param());
		if (b && b->active)
			B->mTransform.set(b->target);
	}

	void CGripIK::init(IKinematics* hands)
	{
		m_hands = hands;
		m_any = false;
		if (!hands)
			return;
		auto hook = [&](SBone& b, LPCSTR name) {
			b.id = hands->LL_BoneID(name);
			b.active = false;
			if (b.id != BI_NONE)
				hands->LL_GetBoneInstance(b.id).set_callback(bctCustom, callback, &b);
			return b.id != BI_NONE;
		};
		bool ok = hook(m_upper, "l_upperarm") & hook(m_fore, "l_forearm") & hook(m_hand, "l_hand");
		for (int f = 0; f < eFingers; ++f)
		{
			ok = hook(m_fingers[f], FINGER_BONES[f]) && ok;
			m_parent[f] = m_fingers[f].id != BI_NONE ? hands->LL_GetData(m_fingers[f].id).GetParentID() : BI_NONE;
		}
		if (!ok)
			Msg("! [gunsmith] grip IK: the hands model lacks left-arm bones; the left hand isn't fitted to grips");
	}

	void CGripIK::begin_frame()
	{
		if (!m_any)
			return;
		m_upper.active = m_fore.active = m_hand.active = false;
		for (SBone& f : m_fingers)
			f.active = false;
		m_any = false;
	}

	void CGripIK::update(IKinematics* hands, const Fmatrix& hands_xform, attachable_hud_item* item)
	{
		if (!g_gs_grip_ik || !hands || hands != m_hands || !item || m_upper.id == BI_NONE || m_fore.id == BI_NONE ||
			m_hand.id == BI_NONE)
			return;
		CWeapon* wpn = smart_cast<CWeapon*>(item->m_parent_hud_item);
		if (!wpn || !wpn->gs_full_mode() || !item->m_model)
			return;

		// the reference: where the animation has the hand at idle (receiver model space)
		if (m_ref_sect != item->m_sect_name)
		{
			m_ref_sect = item->m_sect_name;
			m_has_ref = read_ref(m_ref_sect.c_str(), m_ref);
		}
		if (!m_has_ref)
			return;

		// the grip: the fitted part with the highest grip_priority (ties: the last), its type or the other one
		const CTree& tree = wpn->gs_tree();
		int best = -1, best_prio = 0, type = 0;
		for (u32 n = 1; n < tree.size(); ++n)
		{
			const SPartVisual& pv = part_visual(tree.node(u8(n)).section.c_str());
			const int t = pv.has_grip[g_gs_grip_type ? 1 : 0] ? (g_gs_grip_type ? 1 : 0) : (pv.has_grip[0] ? 0 : (pv.has_grip[1] ? 1 : -1));
			if (t >= 0 && pv.grip_priority >= best_prio)
			{
				best = int(n);
				best_prio = pv.grip_priority;
				type = t;
			}
		}
		if (best < 0)
			return;
		Fmatrix part;
		if (!wpn->gs_visuals().part_frame(CVisuals::eHud, item->m_model, u8(best), part))
			return;
		const SPartVisual& pv = part_visual(tree.node(u8(best)).section.c_str());

		// everything in the left-arm model's space
		Fmatrix inv_hands, gun;
		inv_hands.invert(hands_xform);
		gun.mul_43(inv_hands, item->m_item_transform);
		Fmatrix target, ref, tmp;
		tmp.mul_43(part, pv.grip_hand[type]);
		target.mul_43(gun, tmp);
		ref.mul_43(gun, m_ref);

		const Fmatrix up0 = hands->LL_GetTransform(m_upper.id);
		const Fmatrix fo0 = hands->LL_GetTransform(m_fore.id);
		const Fmatrix ha0 = hands->LL_GetTransform(m_hand.id);

		const float d = ha0.c.distance_to(ref.c);
		const float w = 1.f - clampr((d - NEAR_DIST) / (FAR_DIST - NEAR_DIST), 0.f, 1.f);
		const float weight = w * w * (3.f - 2.f * w); // smoothstep
		if (g_gs_grip_debug)
		{
			static u32 last = 0;
			if (Device.dwTimeGlobal > last + 500)
			{
				last = Device.dwTimeGlobal;
				Msg("- gs_grip: [%s] type %d, hand %.1f cm from the reference, weight %.2f, target %.1f cm from the hand",
					tree.node(u8(best)).section.c_str(), type, d * 100.f, weight, target.c.distance_to(ha0.c) * 100.f);
			}
		}
		if (weight <= 0.f)
			return;

		// the hand: the animated hand moved as the reference moves onto the grip, then blended by the weight
		Fmatrix ref_inv, corr, full;
		ref_inv.invert(ref);
		corr.mul_43(ref_inv, target);
		full.mul_43(ha0, corr);
		Fmatrix hand = blend_rot(ha0, full, weight);
		hand.c.lerp(ha0.c, full.c, weight);

		// the arm: two bones, the elbow on the animation's side
		const Fvector S = up0.c, E0 = fo0.c, H0 = ha0.c;
		const float a = S.distance_to(E0), b = E0.distance_to(H0);
		Fvector dir;
		dir.sub(hand.c, S);
		float dist = dir.magnitude();
		if (dist < 1e-4f || a < 1e-4f || b < 1e-4f)
			return;
		dir.div(dist);
		clamp(dist, _abs(a - b) * 1.01f + 1e-4f, (a + b) * 0.9995f);
		Fvector pole;
		pole.sub(E0, S);
		pole.mad(dir, -pole.dotproduct(dir));
		pole.normalize_safe();
		const float ca = (a * a + dist * dist - b * b) / (2.f * a * dist);
		const float sa = _sqrt(_max(0.f, 1.f - ca * ca));
		Fvector E;
		E.mad(S, dir, a * ca).mad(pole, a * sa);
		Fvector W;
		W.mad(S, dir, dist);

		const Fmatrix R1 = rotate_onto(Fvector().sub(E0, S), Fvector().sub(E, S));
		Fvector fo_dir;
		R1.transform_dir(fo_dir, Fvector().sub(H0, E0));
		const Fmatrix R2 = rotate_onto(fo_dir, Fvector().sub(W, E));

		m_upper.target.mul_43(R1, rot_only(up0));
		m_upper.target.c = S;
		Fmatrix R21;
		R21.mul_43(R2, R1);
		m_fore.target.mul_43(R21, rot_only(fo0));
		m_fore.target.c = E;
		m_hand.target = hand;
		m_hand.target.c = W;

		// the fingers: their animated rotations blended to the grip's, from the new hand down
		Fmatrix anim_local[eFingers];
		for (int f = 0; f < eFingers; ++f)
		{
			if (m_fingers[f].id == BI_NONE || m_parent[f] == BI_NONE)
				continue;
			Fmatrix pinv;
			pinv.invert(hands->LL_GetTransform(m_parent[f]));
			anim_local[f].mul_43(pinv, hands->LL_GetTransform(m_fingers[f].id));
		}
		for (int f = 0; f < eFingers; ++f)
		{
			if (m_fingers[f].id == BI_NONE || m_parent[f] == BI_NONE)
				continue;
			const Fvector& g = pv.grip_fingers[type][f];
			Fmatrix grip_local;
			grip_local.setHPB(g.x, g.y, g.z);
			Fmatrix local = blend_rot(anim_local[f], grip_local, weight);
			const Fmatrix* parent = m_parent[f] == m_hand.id ? &m_hand.target : nullptr;
			for (int p = 0; p < eFingers && !parent; ++p)
				if (m_fingers[p].id == m_parent[f])
					parent = &m_fingers[p].target;
			if (!parent)
				continue;
			m_fingers[f].target.mul_43(*parent, local);
			m_fingers[f].active = true;
		}
		m_upper.active = m_fore.active = m_hand.active = true;
		m_any = true;
		hands->CalculateBones_Invalidate();
		hands->CalculateBones(TRUE);
	}
}
