#pragma once

#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/bone.h"

// An arm IK for the HUD hands: puts one hand of an arm model on a target, after the model is animated.
// player_hud has one per arm (index 0 the right arm, m_model; 1 the left arm, m_model_2). Its targets come from
// scripts (player_hud:set_ik_target) or from the engine (Gunsmith's grip IK, gunsmith_grip_ik.*).
//
// Each frame:
//   - begin_frame() before the arm model's bones are calculated: the animation alone;
//   - after the item is placed, at most one solve(): the arm reaches the target by turning the upper arm, then the
//     forearm, the least that does it (the elbow keeps the animation's side, swivelled toward where the animation's
//     wrist bend would put it, at most SWIVEL_MAX); the forearm twist bone takes TWIST of the hand's extra twist
//     about the forearm; the fingers (optional) blend to a pose given as each finger's rotation to its parent.
// The changed bones are written by bone callbacks, and the arm model's bones are calculated again.
class CHudHandIK
{
public:
	enum { eFingers = 15 };

	struct SPose
	{
		Fmatrix hand;                    // the hand bone's frame wanted, in the arm model's space
		float weight = 1.f;              // 0 the animated hand, 1 the target
		Fvector offset = {0.f, 0.f, 0.f}; // moves the hand in its own (blended) axes, by offset * weight
		const Fmatrix* fingers = nullptr; // eFingers rotations to their parents (finger_bone order), or none
		float finger_weight = 0.f;       // 0 the animated fingers, 1 the pose
	};

	// After the arm model is created. left: the left arm's bones (l_*), otherwise the right's (r_*).
	// The right arm's fingers are not hooked (player_hud's FingerCallback has its thumb), so it takes no finger pose.
	void init(IKinematics* arm, bool left);
	// Before the arm's bones are calculated each frame.
	void begin_frame();
	bool ready(IKinematics* arm) const { return arm && arm == m_arm && m_ok; }
	// The hand bone as the arm model has it now (before solve(): as animated).
	u16 hand_bone() const { return m_hand.id; }
	// false when the arm can't reach anything (missing bones, a degenerate arm)
	bool solve(const SPose& pose);

	// The left fingers in the order of SPose::fingers (Gunsmith's grip_fingers_<type> keys use it too):
	// thumb, index, middle, ring, little; base joint first.
	static LPCSTR finger_bone(int f);

private:
	struct SBone
	{
		u16 id = BI_NONE;
		bool active = false;
		Fmatrix target;
		// The callback the bone had before this one (upstream's arm IK, IKHandCallback), run while this bone isn't
		// active, so a script can still use level.set_arm_ik_enabled / set_hand_target_transform
		BoneCallback prev = nullptr;
		void* prev_param = nullptr;
		u32 prev_type = 0;
		BOOL prev_overwrite = FALSE;
	};
	static void _BCL callback(CBoneInstance* B);
	bool hook(SBone& b, LPCSTR name);

	IKinematics* m_arm = nullptr;
	bool m_ok = false;
	SBone m_upper, m_fore, m_hand;
	SBone m_twist; // the forearm twist bone (optional)
	SBone m_fingers[eFingers];
	u16 m_parent[eFingers] = {}; // each finger's parent bone
	bool m_any = false;
};
