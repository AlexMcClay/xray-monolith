#pragma once

#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/bone.h"

// A finger pose for the HUD hands, on top of whatever puts the hand (the animation, upstream's arm IK). player_hud has
// one per arm (index 0 the right arm, m_model; 1 the left arm, m_model_2); only the left arm's fingers are hooked
// (player_hud's FingerCallback has the right thumb). The arm itself is upstream's IK (player_hud::solve_hand_ik),
// its targets set from Lua (hud_hand_ik.script, Gunsmith Docs research/44 phase 4).
//
// A pose is each finger's rotation to its parent (finger_bone order), blended in by a weight; it holds for the frame
// it is set in (begin_frame clears it), so a script sets it every frame, from upstream's per-frame IK hook
// (_G.COnHudIKTarget) where the hands' bones are calculated right after. Each posed finger keeps its animated place on
// its parent, wherever the hand ends up.
class CHudHandIK
{
public:
	enum { eFingers = 15 };

	// After the arm model is created. left: the left arm (l_*), otherwise the right (r_*, no fingers hooked).
	void init(IKinematics* arm, bool left);
	// Before the arm's bones are calculated each frame: the pose is cleared.
	void begin_frame();
	// This frame's pose: eFingers rotations to their parents (finger_bone order), blended in by weight (0..1)
	void set_fingers(const Fmatrix* rel, float weight);

	// The left fingers in pose order: thumb, index, middle, ring, little; base joint first.
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
	SFinger m_fingers[eFingers];
};
