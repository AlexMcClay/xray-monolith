#pragma once

#include "hud_hand_ik.h"

struct attachable_hud_item;

extern int g_gs_grip_ik;     // gs_grip_ik 0/1: the left hand on the fitted handguard or foregrip
extern int g_gs_grip_type;   // gs_grip_type -1/0/1: each part's grip_type, or that Tarkov GripType (0 Common, 1 Alternative) on every part

namespace gunsmith
{
	// The left hand on the fitted grip, like Tarkov's runtime IK: one source of targets
	// for player_hud's left-arm IK (hud_hand_ik.h), which does the arm, twist and fingers.
	//
	// Tarkov's handguards and foregrips carry the left hand's pose (GripPose). The pipeline turns it into the pose
	// of Anomaly's left hand on the part (part keys grip_hand_<type>, grip_fingers_<type>, grip_priority, grip_type),
	// and the receiver's HUD section holds where the animation has the hand at idle (gs_grip_idle). Each frame, after
	// the hands are animated and the gun is placed:
	//   - the grip is the fitted part with the highest grip_priority (a foregrip over the handguard);
	//   - the target is the animated hand moved by how the grip's pose differs from the idle hand;
	//   - its weight is 1 while the animated hand is near its idle place (within 2 cm) and falls to 0 by 16 cm, so
	//     reloads, checks and inspects take the hand away as animated. It rises at most 4 per second, so a hand
	//     coming back settles onto a grip far from the animation's (a vertical foregrip) instead of snapping onto it;
	//   - the fingers blend to the grip's in the second half of the weight, so the hand arrives open and then closes.
	// core\anim_retarget.py grip_ik is the same in Python.
	class CGripIK
	{
	public:
		// After the gun is placed. ik: the left arm's IK; hands_xform: the left-arm model's transform.
		// false when it set no target (no grip, off, or the weight is 0).
		bool update(CHudHandIK& ik, IKinematics* hands, const Fmatrix& hands_xform, attachable_hud_item* item);

	private:
		shared_str m_section; // the receiver whose gs_grip_idle is loaded
		bool m_idle_ok = false;
		Fmatrix m_idle;       // where the animation holds the hand at idle (receiver model space)
		float m_weight = 0.f; // the weight, rate-limited when rising
		u32 m_weight_frame = 0;
	};
}
