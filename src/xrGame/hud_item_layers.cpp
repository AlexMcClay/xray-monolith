// Animation layers on a HUD item's model (player_hud.h, attachable_hud_item::layer_play; Gunsmith Docs
// animations-and-sounds.md, "Animation layers").
//
// Every HUD motion plays on channel 0 of the item's model, on each of its bone partitions (anim_play), and a new one
// fades only channel 0's (CKinematicsAnimated::LL_PlayCycle). A motion on channel 1 overrides channel 0 for the bones
// it plays on (animation::channels: channel 1 mixes by lerp at factor 1, and one blend in a channel is that channel's
// whole key). So a motion played on channel 1 on one partition holds that partition's bones whatever the item plays:
// a gun's jam kept on its bolt through an inspect, its fire selector kept where it was switched to (as Tarkov's
// Malfunction and Selector animator layers). The partitions come from the model's motions file (bone parts).

#include "stdafx.h"
#include "player_hud.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"

namespace
{
	const u8 LAYER_CHANNEL = 1;

	u16 find_part(IKinematicsAnimated* ka, const shared_str& name)
	{
		const CPartition& P = ka->partitions();
		for (u16 i = 0; i < P.count(); ++i)
		{
			const CPartDef* pd = P.part(i);
			if (pd && pd->Name == name)
				return i;
		}
		return BI_NONE;
	}
}

bool attachable_hud_item::layer_play(const shared_str& part, const shared_str& anm, float speed, bool loop)
{
	IKinematicsAnimated* ka = m_model ? m_model->dcast_PKinematicsAnimated() : nullptr;
	if (!ka || !m_hand_motions || !anm.size() || strstr(anm.c_str(), "anm_") != anm.c_str())
		return false;
	const u16 pid = find_part(ka, part);
	if (pid == BI_NONE)
		return false;
	// (find_motion would assert on a key the HUD section lacks)
	player_hud_motion* m = m_hand_motions->find_motion(anm);
	if (!m || m->m_animations.empty())
		return false;
	const shared_str& name = m->m_base_name != m->m_additional_name ? m->m_additional_name : m->m_animations[0].name;
	const MotionID motion = ka->ID_Cycle_Safe(name);
	if (!motion.valid())
		return false;
	if (speed <= 0.f)
		speed = m->m_anim_speed != 0.f ? m->m_anim_speed : 1.f;
	ka->LL_CloseCycle(pid, 1 << LAYER_CHANNEL);
	if (!ka->LL_PlayCycle(pid, motion, FALSE, 1.f, 1.f, speed, loop ? FALSE : TRUE, 0, 0, LAYER_CHANNEL))
		return false;
	m_model->CalculateBones_Invalidate();
	SLayer& l = m_layers[part];
	l.anm = anm;
	l.motion = motion;
	return true;
}

void attachable_hud_item::layer_stop(const shared_str& part)
{
	m_layers.erase(part);
	IKinematicsAnimated* ka = m_model ? m_model->dcast_PKinematicsAnimated() : nullptr;
	const u16 pid = ka ? find_part(ka, part) : BI_NONE;
	if (pid == BI_NONE)
		return;
	ka->LL_CloseCycle(pid, 1 << LAYER_CHANNEL);
	m_model->CalculateBones_Invalidate();
}

// The layer still playing on the partition (anything that closes every channel, or a new model, drops it)
shared_str attachable_hud_item::layer(const shared_str& part)
{
	auto it = m_layers.find(part);
	IKinematicsAnimated* ka = m_model ? m_model->dcast_PKinematicsAnimated() : nullptr;
	const u16 pid = it != m_layers.end() && ka ? find_part(ka, part) : BI_NONE;
	if (pid == BI_NONE)
		return shared_str();
	for (u32 i = 0, n = ka->LL_PartBlendsCount(pid); i < n; ++i)
	{
		const CBlend* B = ka->LL_PartBlend(pid, i);
		if (B && B->channel == LAYER_CHANNEL && B->motionID == it->second.motion)
			return it->second.anm;
	}
	m_layers.erase(it);
	return shared_str();
}

bool player_hud::play_item_layer(u32 item, LPCSTR part, LPCSTR anm, float speed, bool loop)
{
	attachable_hud_item* hi = item <= SCOPE_ATTACH_IDX ? m_attached_items[item] : nullptr;
	return hi && part && anm && hi->layer_play(part, anm, speed, loop);
}

void player_hud::stop_item_layer(u32 item, LPCSTR part)
{
	attachable_hud_item* hi = item <= SCOPE_ATTACH_IDX ? m_attached_items[item] : nullptr;
	if (hi && part)
		hi->layer_stop(part);
}

LPCSTR player_hud::item_layer(u32 item, LPCSTR part)
{
	attachable_hud_item* hi = item <= SCOPE_ATTACH_IDX ? m_attached_items[item] : nullptr;
	if (!hi || !part)
		return "";
	static shared_str keep; // (the returned string must outlive the call)
	keep = hi->layer(part);
	return keep.size() ? keep.c_str() : "";
}
