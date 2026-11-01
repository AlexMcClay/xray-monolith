#include "stdafx.h"
#include "scope_timers.h"
#include "hud_hand_ik.h"

namespace
{
	LPCSTR const LEFT_FINGERS[CHudHandIK::eFingers] = {
		"l_finger0", "l_finger01", "l_finger02",
		"bip01_l_finger1", "bip01_l_finger11", "bip01_l_finger12",
		"bip01_l_finger2", "bip01_l_finger21", "bip01_l_finger22",
		"bip01_l_finger3", "bip01_l_finger31", "bip01_l_finger32",
		"bip01_l_finger4", "bip01_l_finger41", "bip01_l_finger42"};

	// The elbow swivels about the shoulder-hand line toward where the hand's own forearm direction (the
	// animation's wrist bend) would put it, at most this far, so a hand turned onto a target bends the wrist about as
	// the animation does instead of all of the turn landing there
	const float SWIVEL_MAX = deg2rad(60.f);
	const float TWIST = 0.6f; // share of the hand's extra twist about the forearm that the forearm twist bone takes

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
}

LPCSTR CHudHandIK::finger_bone(int f)
{
	return f >= 0 && f < eFingers ? LEFT_FINGERS[f] : nullptr;
}

void _BCL CHudHandIK::callback(CBoneInstance* B)
{
	SBone* b = static_cast<SBone*>(B->callback_param());
	if (b && b->active)
		B->mTransform.set(b->target);
}

bool CHudHandIK::hook(SBone& b, LPCSTR name)
{
	b.id = m_arm->LL_BoneID(name);
	b.active = false;
	if (b.id != BI_NONE)
		m_arm->LL_GetBoneInstance(b.id).set_callback(bctCustom, callback, &b);
	return b.id != BI_NONE;
}

void CHudHandIK::init(IKinematics* arm, bool left)
{
	m_arm = arm;
	m_left = left;
	m_ok = false;
	m_any = false;
	m_upper = m_fore = m_hand = m_twist = SBone();
	for (int f = 0; f < eFingers; ++f)
	{
		m_fingers[f] = SBone();
		m_parent[f] = BI_NONE;
	}
	if (!arm)
		return;
	m_ok = hook(m_upper, left ? "l_upperarm" : "r_upperarm") & hook(m_fore, left ? "l_forearm" : "r_forearm") &
		hook(m_hand, left ? "l_hand" : "r_hand");
	hook(m_twist, left ? "l_forearm_twist" : "r_forearm_twist");
	bool fingers = true;
	for (int f = 0; left && f < eFingers; ++f)
	{
		fingers = hook(m_fingers[f], LEFT_FINGERS[f]) && fingers;
		m_parent[f] = m_fingers[f].id != BI_NONE ? arm->LL_GetData(m_fingers[f].id).GetParentID() : BI_NONE;
	}
	if (!m_ok)
		Msg("! hand IK: the hands model lacks %s arm bones; that hand takes no IK target", left ? "left" : "right");
	else if (!fingers)
		Msg("! hand IK: the hands model lacks some left finger bones; those keep the animation");
}

void CHudHandIK::begin_frame()
{
	if (!m_any)
		return;
	m_upper.active = m_fore.active = m_hand.active = m_twist.active = false;
	for (SBone& f : m_fingers)
		f.active = false;
	m_any = false;
}

bool CHudHandIK::solve(const SPose& pose)
{
	SCOPE_TIMER("hud.hand_ik.solve");
	if (!m_ok || !m_arm || pose.weight <= 0.f)
		return false;
	IKinematics* arm = m_arm;
	const float weight = _min(pose.weight, 1.f);

	const Fmatrix up0 = arm->LL_GetTransform(m_upper.id);
	const Fmatrix fo0 = arm->LL_GetTransform(m_fore.id);
	const Fmatrix ha0 = arm->LL_GetTransform(m_hand.id);

	// the hand: the animated hand blended toward the target by the weight
	Fmatrix hand = blend_rot(ha0, pose.hand, weight);
	hand.c.lerp(ha0.c, pose.hand.c, weight);
	Fvector extra;
	hand.transform_dir(extra, pose.offset);
	hand.c.mad(extra, weight);

	// the arm: two bones, the elbow on the animation's side
	const Fvector S = up0.c, E0 = fo0.c, H0 = ha0.c;
	const float a = S.distance_to(E0), b = E0.distance_to(H0);
	Fvector dir;
	dir.sub(hand.c, S);
	float dist = dir.magnitude();
	if (dist < 1e-4f || a < 1e-4f || b < 1e-4f)
		return false;
	dir.div(dist);
	clamp(dist, _abs(a - b) * 1.01f + 1e-4f, (a + b) * 0.9995f);
	Fvector pole;
	pole.sub(E0, S);
	pole.mad(dir, -pole.dotproduct(dir));
	pole.normalize_safe();
	{
		// the swivel (SWIVEL_MAX): the elbow that keeps the animation's forearm direction in the hand's axes
		Fvector fa, f_loc, want_f, Ed, n2;
		fa.sub(H0, E0).normalize_safe();
		f_loc.set(fa.dotproduct(ha0.i), fa.dotproduct(ha0.j), fa.dotproduct(ha0.k));
		want_f.mul(hand.i, f_loc.x).mad(hand.j, f_loc.y).mad(hand.k, f_loc.z);
		Ed.mad(S, dir, dist).mad(want_f, -b);
		n2.sub(Ed, S);
		n2.mad(dir, -n2.dotproduct(dir));
		if (n2.magnitude() > 1e-6f)
		{
			n2.normalize();
			Fvector c;
			c.crossproduct(pole, n2);
			float ang = atan2f(dir.dotproduct(c), pole.dotproduct(n2));
			clamp(ang, -SWIVEL_MAX, SWIVEL_MAX);
			Fmatrix sw;
			sw.rotation(dir, ang * weight);
			sw.transform_dir(pole);
			pole.normalize_safe();
		}
	}
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

	// the forearm twist: its share of the hand's twist about the forearm beyond the animation's (the hand as the
	// animation holds it relative to the new forearm is R21 ha0)
	if (m_twist.id != BI_NONE)
	{
		const Fmatrix tw0 = arm->LL_GetTransform(m_twist.id);
		Fmatrix fo_inv, tw_local;
		fo_inv.invert(fo0);
		tw_local.mul_43(fo_inv, tw0);
		Fmatrix kept, kept_inv, extra_rot;
		kept.mul_43(R21, rot_only(ha0));
		kept_inv.invert(kept);
		extra_rot.mul_43(rot_only(hand), kept_inv);
		Fvector ax;
		ax.sub(W, E).normalize_safe();
		Fvector v;
		v.crossproduct(ax, _abs(ax.y) < 0.9f ? Fvector().set(0.f, 1.f, 0.f) : Fvector().set(1.f, 0.f, 0.f));
		v.normalize_safe();
		Fvector dv;
		extra_rot.transform_dir(dv, v);
		dv.mad(ax, -dv.dotproduct(ax));
		dv.normalize_safe();
		Fvector c;
		c.crossproduct(v, dv);
		const float ang = atan2f(ax.dotproduct(c), v.dotproduct(dv));
		Fmatrix twist, tw;
		twist.rotation(ax, ang * TWIST);
		tw.mul_43(m_fore.target, tw_local);
		const Fvector at = tw.c;
		m_twist.target.mul_43(twist, rot_only(tw));
		m_twist.target.c = at;
		m_twist.active = true;
	}

	// the fingers: the animated ones blended to the pose by finger_weight, from the new hand down (without a pose
	// they keep the animation and follow the hand)
	if (pose.fingers)
	{
		Fmatrix anim_local[eFingers];
		for (int f = 0; f < eFingers; ++f)
		{
			if (m_fingers[f].id == BI_NONE || m_parent[f] == BI_NONE)
				continue;
			Fmatrix pinv;
			pinv.invert(arm->LL_GetTransform(m_parent[f]));
			anim_local[f].mul_43(pinv, arm->LL_GetTransform(m_fingers[f].id));
		}
		for (int f = 0; f < eFingers; ++f)
		{
			if (m_fingers[f].id == BI_NONE || m_parent[f] == BI_NONE)
				continue;
			Fmatrix local = blend_rot(anim_local[f], pose.fingers[f], pose.finger_weight);
			const Fmatrix* parent = m_parent[f] == m_hand.id ? &m_hand.target : nullptr;
			for (int p = 0; p < eFingers && !parent; ++p)
				if (m_fingers[p].id == m_parent[f])
					parent = &m_fingers[p].target;
			if (!parent)
				continue;
			m_fingers[f].target.mul_43(*parent, local);
			m_fingers[f].active = true;
		}
	}
	m_upper.active = m_fore.active = m_hand.active = true;
	m_any = true;
	arm->CalculateBones_Invalidate();
	arm->CalculateBones(TRUE);
	return true;
}
