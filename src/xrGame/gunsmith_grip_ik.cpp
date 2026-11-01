#include "stdafx.h"
#include "gunsmith_grip_ik.h"
#include "gunsmith_visuals.h"
#include "player_hud.h"
#include "Weapon.h"

int g_gs_grip_ik = 1;
int g_gs_grip_type = -1; // -1: each part's grip_type; 0, 1: that Tarkov GripType on every part (for comparing)

namespace gunsmith
{
	namespace
	{
		const float NEAR_DIST = 0.02f; // the animated hand this close to its idle place: full grip
		const float FAR_DIST = 0.16f;  // and this far: none (10 cm let the hand snap onto a vertical foregrip in 0.1 s)
		const float RISE = 4.f;        // the weight's fastest rise, per second

		float ease(float x) // smoothstep
		{
			clamp(x, 0.f, 1.f);
			return x * x * (3.f - 2.f * x);
		}

		bool read_xform(LPCSTR sect, LPCSTR key, Fmatrix& out)
		{
			if (!sect || !pSettings->line_exist(sect, key))
				return false;
			LPCSTR v = pSettings->r_string(sect, key);
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

	bool CGripIK::update(CHudHandIK& ik, IKinematics* hands, const Fmatrix& hands_xform, attachable_hud_item* item)
	{
		if (!g_gs_grip_ik || !item || !ik.ready(hands))
			return false;
		CWeapon* wpn = smart_cast<CWeapon*>(item->m_parent_hud_item);
		if (!wpn || !wpn->gs_full_mode() || !item->m_model)
			return false;

		// the receiver's idle hand (HUD section)
		if (m_section != item->m_sect_name)
		{
			m_section = item->m_sect_name;
			m_idle_ok = read_xform(m_section.c_str(), "gs_grip_idle", m_idle);
		}
		if (!m_idle_ok)
			return false;

		// the grip: the fitted part with the highest grip_priority (ties: the last), its type or the other one
		const CTree& tree = wpn->gs_tree();
		// Tarkov's first person takes GripType 0 (Common), 1 (Alternative) being the menu character's relaxed hold;
		// on Anomaly's hand either may look better depending on the part, so each part names its own (grip_type,
		// the pipeline's pick). gs_grip_type 0 or 1 takes that type on every part, for comparing.
		int best = -1, best_prio = 0, type = 0;
		for (u32 n = 1; n < tree.size(); ++n)
		{
			const SPartVisual& pv = part_visual(tree.node(u8(n)).section);
			const int want = g_gs_grip_type >= 0 ? g_gs_grip_type : pv.grip_type;
			const int t = pv.has_grip[want] ? want : (pv.has_grip[0] ? 0 : (pv.has_grip[1] ? 1 : -1));
			if (t >= 0 && pv.grip_priority >= best_prio)
			{
				best = int(n);
				best_prio = pv.grip_priority;
				type = t;
			}
		}
		if (best < 0)
			return false;
		Fmatrix part;
		if (!wpn->gs_visuals().part_frame(CVisuals::eHud, item->m_model, u8(best), part))
			return false;
		const SPartVisual& pv = part_visual(tree.node(u8(best)).section);

		// everything in the left-arm model's space
		Fmatrix inv_hands, gun;
		inv_hands.invert(hands_xform);
		gun.mul_43(inv_hands, item->m_item_transform);
		Fmatrix target, idle, tmp;
		tmp.mul_43(part, pv.grip_hand[type]);
		target.mul_43(gun, tmp);
		idle.mul_43(gun, m_idle);

		const Fmatrix ha0 = hands->LL_GetTransform(ik.hand_bone());

		const float d = ha0.c.distance_to(idle.c);
		const float to = ease(1.f - (d - NEAR_DIST) / (FAR_DIST - NEAR_DIST));
		// falls with the animation, rises at most RISE a second (from 0 after a frame without a grip)
		if (m_weight_frame + 1 < Device.dwFrame)
			m_weight = 0.f;
		if (m_weight_frame != Device.dwFrame)
			m_weight = _min(to, m_weight + RISE * Device.fTimeDelta);
		m_weight_frame = Device.dwFrame;
		const float weight = m_weight;
		if (g_gs_debug & eGsDebugGrip)
		{
			static u32 last = 0;
			if (Device.dwTimeGlobal > last + 500)
			{
				last = Device.dwTimeGlobal;
				Msg("- gs_grip: [%s] type %d, hand %.1f cm from its idle place, weight %.2f, moved %.1f cm",
					tree.node(u8(best)).section.c_str(), type, d * 100.f, weight, target.c.distance_to(idle.c) * 100.f);
			}
		}
		if (weight <= 0.f)
			return false;

		// the target: the animated hand moved as the idle hand moves onto the grip
		CHudHandIK::SPose pose;
		Fmatrix idle_inv, corr;
		idle_inv.invert(idle);
		corr.mul_43(idle_inv, target);
		pose.hand.mul_43(ha0, corr);
		pose.weight = weight;
		// the fingers: the grip's, from the hand down (the hand arrives open, then closes)
		Fmatrix fingers[CHudHandIK::eFingers];
		for (int f = 0; f < CHudHandIK::eFingers; ++f)
		{
			const Fvector& g = pv.grip_fingers[type][f];
			fingers[f].setHPB(g.x, g.y, g.z);
		}
		pose.fingers = fingers;
		pose.finger_weight = ease((weight - 0.5f) / 0.5f);
		return ik.solve(pose);
	}
}
