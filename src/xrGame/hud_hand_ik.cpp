#include "stdafx.h"
#include "hud_hand_ik.h"
#include "player_hud.h"

namespace
{
	LPCSTR const LEFT_FINGERS[CHudHandIK::eFingers] = {
		"l_finger0", "l_finger01", "l_finger02",
		"bip01_l_finger1", "bip01_l_finger11", "bip01_l_finger12",
		"bip01_l_finger2", "bip01_l_finger21", "bip01_l_finger22",
		"bip01_l_finger3", "bip01_l_finger31", "bip01_l_finger32",
		"bip01_l_finger4", "bip01_l_finger41", "bip01_l_finger42"};

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
}

LPCSTR CHudHandIK::finger_bone(int f)
{
	return f >= 0 && f < eFingers ? LEFT_FINGERS[f] : nullptr;
}

// A posed finger: its animated place on its parent (the parent as it ends up: the engine has just put this bone at
// parent x animated local), its rotation blended toward the pose
void _BCL CHudHandIK::callback(CBoneInstance* B)
{
	SFinger* f = static_cast<SFinger*>(B->callback_param());
	if (!f)
		return;
	if (f->active && f->weight > 0.f && f->arm && f->parent != BI_NONE)
	{
		const Fmatrix& P = f->arm->LL_GetTransform(f->parent);
		Fmatrix pinv, anim;
		pinv.invert(P);
		anim.mul_43(pinv, B->mTransform);
		const Fmatrix local = blend_rot(anim, f->rel, f->weight);
		B->mTransform.mul_43(P, local);
		return;
	}
	if (f->prev)
	{
		// the earlier callback reads its own parameter from the bone: put it back for the call
		B->set_callback(f->prev_type, f->prev, f->prev_param, f->prev_overwrite);
		f->prev(B);
		B->set_callback(bctCustom, callback, f);
	}
}

bool CHudHandIK::hook(SFinger& f, LPCSTR name)
{
	f = SFinger();
	f.id = m_arm->LL_BoneID(name);
	if (f.id == BI_NONE)
		return false;
	f.arm = m_arm;
	f.parent = m_arm->LL_GetData(f.id).GetParentID();
	CBoneInstance& bi = m_arm->LL_GetBoneInstance(f.id);
	if (bi.callback() && bi.callback() != callback)
	{
		f.prev = bi.callback();
		f.prev_param = bi.callback_param();
		f.prev_type = bi.callback_type();
		f.prev_overwrite = bi.callback_overwrite();
	}
	bi.set_callback(bctCustom, callback, &f);
	return true;
}

void CHudHandIK::init(IKinematics* arm, bool left)
{
	m_arm = arm;
	for (SFinger& f : m_fingers)
		f = SFinger();
	if (!arm || !left)
		return;
	bool all = true;
	for (int f = 0; f < eFingers; ++f)
		all = hook(m_fingers[f], LEFT_FINGERS[f]) && all;
	if (!all)
		Msg("! hand IK: the hands model lacks some left finger bones; those keep the animation");
}

void CHudHandIK::begin_frame()
{
	for (SFinger& f : m_fingers)
		f.active = false;
}

void CHudHandIK::set_fingers(const Fmatrix* rel, float weight)
{
	clamp(weight, 0.f, 1.f);
	for (int f = 0; f < eFingers; ++f)
	{
		SFinger& fi = m_fingers[f];
		if (fi.id == BI_NONE || fi.parent == BI_NONE)
			continue;
		fi.rel = rel[f];
		fi.weight = weight;
		fi.active = weight > 0.f;
	}
}

// player_hud's script side (player_hud:...): hand 0 right, 1 left.

// This frame's finger pose: 45 numbers (h, p, b in degrees of the 15 fingers, CHudHandIK::finger_bone order; the
// format of Gunsmith's grip_fingers_<type> keys), blended in by weight. Only the left hand has fingers to pose.
void player_hud::set_ik_fingers(u32 hand, LPCSTR pose, float weight)
{
	if (hand > 1 || !pose || _GetItemCount(pose) < 3 * CHudHandIK::eFingers)
	{
		Msg("! player_hud:set_ik_fingers: hand %u: the hand is 0 or 1, the pose 45 numbers (h, p, b of 15 fingers)", hand);
		return;
	}
	Fmatrix rel[CHudHandIK::eFingers];
	string64 item;
	for (int f = 0; f < CHudHandIK::eFingers; ++f)
		rel[f].setHPB(deg2rad((float)atof(_GetItem(pose, f * 3, item))), deg2rad((float)atof(_GetItem(pose, f * 3 + 1, item))),
		              deg2rad((float)atof(_GetItem(pose, f * 3 + 2, item))));
	m_hand_ik[hand].set_fingers(rel, weight);
}

// The arm model's transform (HUD space): hand 0 the right arm (m_transform), 1 the left (m_transform_2). Upstream's
// hud_adjust.get_hud_transform() gives the left one only.
Fmatrix player_hud::get_arm_transform(u32 hand)
{
	return hand == 0 ? m_transform : m_transform_2;
}

// The hand bone as animated (before any IK) in its arm model's space: hand 0 r_hand of the right arm, 1 l_hand of the
// left (upstream's arm IK bones). Identity without the bone. Upstream's hud_adjust.hands_bone_anim_pos needs the bone's
// id and picks the model by it (ids up to 20 the left arm), which Lua can't look up for the HUD hands.
Fmatrix player_hud::get_hand_anim(u32 hand)
{
	Fmatrix res = Fidentity;
	if (hand > 1)
		return res;
	const u16 bone = m_arm_ik[1 - hand].bones[3];
	IKinematicsAnimated* model = hand == 0 ? m_model : m_model_2;
	IKinematics* K = model ? model->dcast_PKinematics() : nullptr;
	if (K && bone != BI_NONE && bone < K->LL_BoneCount())
		K->Bone_GetAnimPos(res, bone, u8(-1), true);
	return res;
}

// A HUD item in the hands (item 0 the main one, 1 the left hand's such as a detector, 2 the scope) and its model's
// transform (HUD space; identity without one). game_object:xform(true) has no check for an item not in the hands.
bool player_hud::item_attached(u32 item)
{
	return item <= SCOPE_ATTACH_IDX && m_attached_items[item] && m_attached_items[item]->m_model;
}

Fmatrix player_hud::get_item_transform(u32 item)
{
	return item_attached(item) ? m_attached_items[item]->m_item_transform : Fidentity;
}
