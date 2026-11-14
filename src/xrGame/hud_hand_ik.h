#pragma once

#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/bone.h"

// The HUD hands' IK on upstream's arm IK (Gunsmith Docs research/44, phase 4). player_hud has one per arm (index 0
// the right arm, m_model; 1 the left arm, m_model_2). Its targets come from scripts (player_hud:set_ik_target) or from
// the engine (Gunsmith's grip IK, gunsmith_grip_ik.*).
//
// Each frame:
//   - begin_frame() before the arm model's bones are calculated;
//   - after the item is placed, at most one solve(): the hand target, the animated hand moved toward it by the weight,
//     goes to upstream's arm IK (player_hud::solve_hand_ik puts the upper arm, forearm, twist and hand later this
//     frame), switched on for the arm while it has a target; and the fingers (optional) blend to a pose given as
//     each finger's rotation to its parent, applied by bone callbacks relative to wherever the hand ends up;
//   - end_frame() after every target: upstream's arm IK off again for an arm without one (only if this switched it
//     on, so a script driving upstream's IK itself keeps it).
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

	// After the arm model is created. left: the left arm (l_*), otherwise the right (r_*). The right arm's fingers
	// are not hooked (player_hud's FingerCallback has its thumb), so it takes no finger pose.
	void init(IKinematics* arm, bool left);
	// Before the arm's bones are calculated each frame.
	void begin_frame();
	// After this frame's targets (player_hud::update_hand_ik).
	void end_frame();
	bool ready(IKinematics* arm) const { return arm && arm == m_arm && m_ok; }
	// The hand bone (as animated until upstream's IK solves).
	u16 hand_bone() const { return m_hand; }
	// false without a hand bone or with no weight
	bool solve(const SPose& pose);

	// The left fingers in the order of SPose::fingers (Gunsmith's grip_fingers_<type> keys use it too):
	// thumb, index, middle, ring, little; base joint first.
	static LPCSTR finger_bone(int f);

private:
	struct SFinger
	{
		u16 id = BI_NONE;
		u16 parent = BI_NONE;
		bool active = false;
		Fmatrix rel;                     // the pose: its rotation to its parent
		float weight = 0.f;
		IKinematics* arm = nullptr;
		// The callback the bone had before this one, run while this finger isn't posed
		BoneCallback prev = nullptr;
		void* prev_param = nullptr;
		u32 prev_type = 0;
		BOOL prev_overwrite = FALSE;
	};
	static void _BCL callback(CBoneInstance* B);
	bool hook(SFinger& f, LPCSTR name);

	IKinematics* m_arm = nullptr;
	bool m_ok = false;
	bool m_left = false;
	u16 m_hand = BI_NONE;
	SFinger m_fingers[eFingers];
	bool m_target = false;  // a target this frame
	bool m_enabled = false; // upstream's arm IK switched on by this
};
