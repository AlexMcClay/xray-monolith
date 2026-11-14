#include "stdafx.h"
#include "hud_hand_ik.h"
#include "player_hud.h"
#include "gunsmith_grip_ik.h"

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

	// upstream's arm order (player_hud::m_arm_ik, m_ik_transforms): 0 the left arm, 1 the right
	int upstream_arm(bool left) { return left ? 0 : 1; }
}

LPCSTR CHudHandIK::finger_bone(int f)
{
	return f >= 0 && f < eFingers ? LEFT_FINGERS[f] : nullptr;
}

// A posed finger: its animated place on its parent (the parent as it ends up, after any arm IK: the engine has just
// put this bone at parent x animated local), its rotation blended toward the pose
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
	m_left = left;
	m_ok = false;
	m_target = false;
	m_enabled = false;
	m_hand = BI_NONE;
	for (SFinger& f : m_fingers)
		f = SFinger();
	if (!arm)
		return;
	m_hand = arm->LL_BoneID(left ? "l_hand" : "r_hand");
	m_ok = m_hand != BI_NONE;
	bool fingers = true;
	for (int f = 0; left && f < eFingers; ++f)
		fingers = hook(m_fingers[f], LEFT_FINGERS[f]) && fingers;
	if (!m_ok)
		Msg("! hand IK: the hands model lacks the %s hand bone; that hand takes no IK target", left ? "left" : "right");
	else if (!fingers)
		Msg("! hand IK: the hands model lacks some left finger bones; those keep the animation");
}

void CHudHandIK::begin_frame()
{
	m_target = false;
	for (SFinger& f : m_fingers)
		f.active = false;
}

void CHudHandIK::end_frame()
{
	if (m_target || !m_enabled || !g_player_hud)
		return;
	g_player_hud->set_arm_ik_enabled(m_left ? 1 : 0, false); // (script numbering: 0 the right hand, 1 the left)
	m_enabled = false;
}

bool CHudHandIK::solve(const SPose& pose)
{
	if (!m_ok || !m_arm || pose.weight <= 0.f || !g_player_hud)
		return false;
	const float weight = _min(pose.weight, 1.f);

	// the hand: the animated hand moved toward the target by the weight, for upstream's arm IK to reach
	const Fmatrix ha0 = m_arm->LL_GetTransform(m_hand);
	Fmatrix hand = blend_rot(ha0, pose.hand, weight);
	hand.c.lerp(ha0.c, pose.hand.c, weight);
	Fvector extra;
	hand.transform_dir(extra, pose.offset);
	hand.c.mad(extra, weight);
	const int arm = upstream_arm(m_left);
	g_player_hud->m_ik_transforms[arm * 4 + 3].first = hand;
	g_player_hud->m_ik_transforms[arm * 4 + 3].second = true;
	if (!g_player_hud->m_arm_ik[arm].enabled)
	{
		g_player_hud->set_arm_ik_enabled(m_left ? 1 : 0, true);
		m_enabled = true;
	}
	m_target = true;

	// the fingers: blended to the pose by finger_weight wherever the hand ends up (without a pose they keep the
	// animation and follow the hand)
	if (pose.fingers && pose.finger_weight > 0.f)
	{
		for (int f = 0; f < eFingers; ++f)
		{
			SFinger& fi = m_fingers[f];
			if (fi.id == BI_NONE || fi.parent == BI_NONE)
				continue;
			fi.rel = pose.fingers[f];
			fi.weight = _min(pose.finger_weight, 1.f);
			fi.active = true;
		}
	}
	return true;
}

// player_hud's hand IK: script targets and the Gunsmith grip.

namespace
{
	const float IK_BLEND = 4.f; // a script target's weight moves at most this much a second (set, cleared, reweighted)
}

bool player_hud::ik_item_frame(u8 hand, u8 item, const shared_str& bone, Fmatrix& out)
{
	attachable_hud_item* it = item <= SCOPE_ATTACH_IDX ? m_attached_items[item] : nullptr;
	if (!it || !it->m_model)
		return false;
	Fmatrix frame = it->m_item_transform;
	if (bone.size())
	{
		const u16 id = it->m_model->LL_BoneID(bone.c_str());
		if (id == BI_NONE)
			return false;
		frame.mulB_43(it->m_model->LL_GetTransform(id));
	}
	Fmatrix inv;
	inv.invert(hand == 0 ? m_transform : m_transform_2);
	out.mul_43(inv, frame);
	return true;
}

void player_hud::update_hand_ik()
{
	static u32 blend_frame = u32(-1); // the script weights move once a frame
	const bool new_frame = blend_frame != Device.dwFrame;
	blend_frame = Device.dwFrame;
	IKinematics* arms[2] = {m_model->dcast_PKinematics(), m_model_2->dcast_PKinematics()};
	bool script[2] = {false, false};
	for (u8 h = 0; h < 2; ++h)
	{
		CHudHandIK& ik = m_hand_ik[h];
		m_ik_anim_ok[h] = ik.ready(arms[h]);
		if (!m_ik_anim_ok[h])
			continue;
		m_ik_anim_hand[h] = arms[h]->LL_GetTransform(ik.hand_bone());

		SScriptIK& s = m_script_ik[h];
		if (new_frame)
		{
			const float want = s.set ? s.want : 0.f;
			if (s.weight < want)
				s.weight = _min(want, s.weight + IK_BLEND * Device.fTimeDelta);
			else
				s.weight = _max(want, s.weight - IK_BLEND * Device.fTimeDelta);
		}
		if (!s.set && s.weight <= 0.f)
			continue;
		script[h] = true; // the script has this hand, even when its item or bone is missing this frame
		Fmatrix frame;
		if (s.weight <= 0.f || !ik_item_frame(h, s.item, s.bone, frame))
			continue;
		CHudHandIK::SPose pose;
		pose.hand.mul_43(frame, s.offset);
		pose.weight = s.weight;
		ik.solve(pose);
	}

	// Gunsmith: the left hand on the fitted grip (not while the left hand holds something else, a script
	// animation has the hands, or a script target has the left hand)
	if (!script[1] && m_attached_items[0] && !m_attached_items[1] && script_anim_part == u8(-1))
	{
		if (!m_gs_grip)
			m_gs_grip = xr_new<gunsmith::CGripIK>();
		m_gs_grip->update(m_hand_ik[1], arms[1], m_transform_2, m_attached_items[0]);
	}
	// upstream's arm IK off for an arm that had no target this frame
	m_hand_ik[0].end_frame();
	m_hand_ik[1].end_frame();
}

void player_hud::free_hand_ik()
{
	xr_delete(m_gs_grip);
}

void player_hud::set_ik_target(u32 hand, const Fmatrix& m)
{
	set_ik_target_bone(hand, m, 0, nullptr);
}

void player_hud::set_ik_target_bone(u32 hand, const Fmatrix& m, u32 item, LPCSTR bone)
{
	if (hand > 1 || item > SCOPE_ATTACH_IDX)
	{
		Msg("! player_hud:set_ik_target: hand %u, item %u: the hand is 0 (right) or 1 (left), the item 0 to 2", hand, item);
		return;
	}
	SScriptIK& s = m_script_ik[hand];
	s.set = true;
	s.offset = m;
	s.item = u8(item);
	s.bone = bone && bone[0] ? bone : nullptr;
}

void player_hud::clear_ik_target(u32 hand)
{
	if (hand > 1)
		return;
	m_script_ik[hand].set = false;
	m_script_ik[hand].want = 1.f;
}

void player_hud::set_ik_weight(u32 hand, float weight)
{
	if (hand > 1)
		return;
	clamp(weight, 0.f, 1.f);
	m_script_ik[hand].want = weight;
}

float player_hud::get_ik_weight(u32 hand)
{
	return hand > 1 ? 0.f : m_script_ik[hand].weight;
}

Fmatrix player_hud::get_ik_hand(u32 hand)
{
	return get_ik_hand_bone(hand, 0, nullptr);
}

Fmatrix player_hud::get_ik_hand_bone(u32 hand, u32 item, LPCSTR bone)
{
	Fmatrix out;
	out.identity();
	Fmatrix frame, inv;
	if (hand > 1 || item > SCOPE_ATTACH_IDX || !m_ik_anim_ok[hand] ||
		!ik_item_frame(u8(hand), u8(item), shared_str(bone && bone[0] ? bone : nullptr), frame))
		return out;
	inv.invert(frame);
	out.mul_43(inv, m_ik_anim_hand[hand]);
	return out;
}
