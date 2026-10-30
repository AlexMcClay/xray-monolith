#pragma once

#include "../Include/xrRender/Kinematics.h"
#include "../xrEngine/bone.h"

struct attachable_hud_item;

extern int g_gs_grip_ik;     // gs_grip_ik 0/1: the left hand on the fitted handguard or foregrip
extern int g_gs_grip_type;   // gs_grip_type 0/1: which of Tarkov's grip poses (GripType) the hand takes
extern int g_gs_grip_debug;  // gs_grip_debug 0/1: log the grip, weight and how far the hand moves
extern Fvector g_gs_grip_offset; // gs_grip_offset x,y,z: moves the hand on the grip, in the hand's axes (metres; tuning)

namespace gunsmith
{
	// The left hand on the fitted grip (Gunsmith Docs 19, phase 3), like Tarkov's runtime IK.
	//
	// Tarkov's handguards and foregrips carry the left hand's pose (GripPose). The pipeline turns it into the pose
	// of Anomaly's left hand on the part (part keys grip_hand_<type>, grip_fingers_<type>, grip_priority), and the
	// receiver's HUD section holds where the animation has the hand at idle and the stock grip's pose. Each frame, after the
	// hands are animated and the gun is placed:
	//   - the grip is the fitted part with the highest grip_priority (a foregrip over the handguard);
	//   - the hand's correction is how the fitted grip's pose differs from the stock build's (gs_grip_ref_<type>),
	//     applied to the animated hand: the stock handguard keeps the animation's hold, which was tuned on it, and
	//     the hand keeps the animation's small motions (firing, sway). (The first version moved the hand onto the
	//     grip pose itself; Tarkov's poses are made for its bigger hand and looked wrong: user, 30 Oct 2026);
	//   - its weight is 1 while the animated hand is near its idle place (gs_grip_idle, within 3 cm) and falls to
	//     0 by 10 cm, so reloads, checks and inspects take the hand away as animated;
	//   - the arm reaches the hand by turning the upper arm, then the forearm, the least that does it (the elbow
	//     keeps the animation's side); the fingers blend to the grip's.
	// Only the left arm's model (m_model_2) is changed, through bone callbacks: the left hand's bones are
	// calculated again with the results written over them. core\anim_retarget.py grip_ik is the same in Python.
	class CGripIK
	{
	public:
		enum { eFingers = 15 };

		// After the hands' left-arm model is created (player_hud::load).
		void init(IKinematics* hands);
		// Before the hands' bones are calculated each frame: the animation alone.
		void begin_frame();
		// After the gun is placed. hands_xform: the left-arm model's transform.
		void update(IKinematics* hands, const Fmatrix& hands_xform, attachable_hud_item* item);

	private:
		struct SBone
		{
			u16 id = BI_NONE;
			bool active = false;
			Fmatrix target;
		};
		static void _BCL callback(CBoneInstance* B);

		IKinematics* m_hands = nullptr;
		SBone m_upper, m_fore, m_hand;
		SBone m_fingers[eFingers];
		u16 m_parent[eFingers] = {}; // each finger's parent bone
		bool m_any = false;
		// The receiver's references, from its HUD section (receiver model space):
		//   gs_grip_idle                 where the animation holds the hand at idle (the weight is measured from it)
		//   gs_grip_ref_<type>           the stock build's grip pose: the hand on it, and its fingers' rotations
		//   gs_grip_ref_fingers_<type>   (grip_fingers_<type> of the stock grip). The fitted grip moves the hand and
		//                                fingers by how it differs from these, so the stock grip keeps the animation.
		struct SRef
		{
			shared_str section;
			bool ok = false;
			Fmatrix idle;
			bool has[2] = {false, false};
			Fmatrix hand[2];
			Fmatrix fingers[2][eFingers];
			void load(LPCSTR sect);
		};
		SRef m_ref;
	};
}
