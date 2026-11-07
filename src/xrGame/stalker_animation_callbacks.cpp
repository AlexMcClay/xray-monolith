////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_animation_callbacks.cpp
//	Created 	: 25.02.2003
//  Modified 	: 19.11.2004
//	Author		: Dmitriy Iassenev
//	Description : Stalker animation manager : bone callbacks
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "stalker_animation_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "sight_manager.h"
#include "stalker_movement_manager_smart_cover.h"
#include "game_object_space.h"
#include "effectorshot.h"

typedef CStalkerAnimationManager::callback_params callback_params;

// The lean (game_object:set_lean), on the spine bone after the sight's turn: a roll around the torso's facing (flat),
// so the aim direction stays, and the upper body shifted sideways; > 0 to the right. The bones above follow,
// with the gun, the head (the eyes) and the hit shapes.
static void apply_lean(CBoneInstance* bone, callback_params const* parameter, Fvector const& position)
{
	if (!parameter->m_lean_roll || !parameter->m_lean_shift)
		return;
	float const roll = *parameter->m_lean_roll;
	float const shift = *parameter->m_lean_shift;
	if (fis_zero(roll) && fis_zero(shift))
		return;
	Fvector forward = Fvector().set(0.f, 0.f, 1.f);
	if (parameter->m_rotation && _valid(*parameter->m_rotation))
		parameter->m_rotation->transform_dir(forward);
	forward.y = 0.f;
	if (forward.square_magnitude() < EPS_L)
		forward.set(0.f, 0.f, 1.f);
	forward.normalize();
	Fvector right;
	right.crossproduct(Fvector().set(0.f, 1.f, 0.f), forward);
	Fmatrix lean;
	lean.rotation(forward, roll);
	// whichever way the axis-angle convention turns, the head must go toward `right` for roll > 0
	Fvector up = Fvector().set(0.f, 1.f, 0.f);
	lean.transform_dir(up);
	if (up.dotproduct(right) * roll < 0.f)
		lean.rotation(forward, -roll);
	bone->mTransform.mulA_43(lean);
	bone->mTransform.c.mad(position, right, shift);
}

static void _BCL callback_rotation(CBoneInstance* bone)
{
	if (!_valid( bone->mTransform )) return;
	R_ASSERT(_valid( bone->mTransform ));
	callback_params* parameter = static_cast<callback_params*>(bone->callback_param());
	VERIFY(parameter);
	VERIFY(parameter->m_rotation);
	VERIFY(parameter->m_object);

	if (!parameter || !_valid(*parameter->m_rotation))
	{
		return;
	}

	CAI_Stalker const* object = parameter->m_object;
	if (!object->sight().enabled())
		return;

	Fvector position = bone->mTransform.c;
	R_ASSERT(_valid( *parameter->m_rotation ));
	bone->mTransform.mulA_43(*parameter->m_rotation);
	CWeaponShotEffector& effector = object->weapon_shot_effector();
	if (!effector.IsActive())
	{
		bone->mTransform.c = position;
		apply_lean(bone, parameter, position);
		R_ASSERT(_valid( bone->mTransform ));
		return;
	}

	Fvector angles;
	effector.GetDeltaAngle(angles);
	angles.x = angle_normalize_signed(angles.x);
	angles.y = angle_normalize_signed(angles.y);
	angles.z = angle_normalize_signed(angles.z);

	if (object->movement().current_params().cover())
		angles.mul(0.f);
	else
		angles.mul(.1f);

	Fmatrix effector_transform;
	effector_transform.setXYZ(angles);
	R_ASSERT(_valid( effector_transform ));
	bone->mTransform.mulA_43(effector_transform);
	bone->mTransform.c = position;
	apply_lean(bone, parameter, position);
	R_ASSERT(_valid( bone->mTransform ));
}

static void _BCL callback_rotation_blend(CBoneInstance* const bone)
{
	R_ASSERT(_valid( bone->mTransform ));

	callback_params* parameter = static_cast<callback_params*>(bone->callback_param());
	VERIFY(parameter);
	VERIFY(parameter->m_rotation);
	VERIFY(parameter->m_object);
	VERIFY(parameter->m_blend);
	//	VERIFY2							( *parameter->m_blend, make_string( "%d %s[%s]", Device.dwTimeGlobal, parameter->m_object->cName().c_str(), parameter->m_object->g_Alive() ? "+" : "-") );

	float multiplier = 1.f;
	if (*parameter->m_blend)
	{
		CBlend const& blend = **parameter->m_blend;
		multiplier = blend.timeCurrent / blend.timeTotal;
	}

	VERIFY(multiplier >= 0.f);
	VERIFY(multiplier <= 1.f);
	multiplier = parameter->m_forward ? multiplier : (1.f - multiplier);

#if 0
	Fmatrix rotation				= *parameter->m_rotation;
	Fvector							angles;
	rotation.getXYZ					(angles);
	angles.mul						(multiplier);
	rotation.setXYZ					(angles);
#else // #if 0
	Fquaternion left;
	left.set(Fidentity);

	Fquaternion right;
	right.set(*parameter->m_rotation);

	Fquaternion result;
	result.slerp(left, right, multiplier);

	Fmatrix rotation;
	rotation.rotation(result);
#endif // #if 0

	Fvector position = bone->mTransform.c;
	R_ASSERT(_valid( rotation ));
	bone->mTransform.mulA_43(rotation);
	bone->mTransform.c = position;
	apply_lean(bone, parameter, position);
	R_ASSERT(_valid( bone->mTransform ));
}

void CStalkerAnimationManager::assign_bone_callbacks()
{
	IKinematics* kinematics = smart_cast<IKinematics*>(m_visual);
	VERIFY(kinematics);

#ifdef DEBUG
	m_head_params.invalidate		();
	m_shoulder_params.invalidate	();
	m_spine_params.invalidate		();
#endif // #ifdef DEBUG

	LPCSTR section = *object().cNameSect();

	m_head_params.m_rotation = &object().sight().current_head_rotation();
	m_head_params.m_object = &object();
	m_head_params.m_blend = 0;
	m_head_params.m_forward = true;

	int head_bone = kinematics->LL_BoneID(pSettings->r_string(section, "bone_head"));
	kinematics->LL_GetBoneInstance(u16(head_bone)).set_callback(bctCustom, &callback_rotation, &m_head_params);

	m_shoulder_params.m_rotation = &object().sight().current_shoulder_rotation();
	m_shoulder_params.m_object = &object();
	m_shoulder_params.m_blend = 0;
	m_shoulder_params.m_forward = true;

	int shoulder_bone = kinematics->LL_BoneID(pSettings->r_string(section, "bone_shoulder"));
	kinematics->LL_GetBoneInstance(u16(shoulder_bone)).set_callback(bctCustom, &callback_rotation, &m_shoulder_params);

	m_spine_params.m_rotation = &object().sight().current_spine_rotation();
	m_spine_params.m_object = &object();
	m_spine_params.m_blend = 0;
	m_spine_params.m_forward = true;
	m_spine_params.m_lean_roll = &m_lean_roll;
	m_spine_params.m_lean_shift = &m_lean_shift;

	int spine_bone = kinematics->LL_BoneID(pSettings->r_string(section, "bone_spin"));
	kinematics->LL_GetBoneInstance(u16(spine_bone)).set_callback(bctCustom, &callback_rotation, &m_spine_params);

	//	remove_bone_callbacks	();
}

void CStalkerAnimationManager::assign_bone_blend_callbacks(bool const& forward_direction)
{
	IKinematics* kinematics = smart_cast<IKinematics*>(m_visual);
	VERIFY(kinematics);

#ifdef DEBUG
	m_head_params.invalidate		();
	m_shoulder_params.invalidate	();
	m_spine_params.invalidate		();
#endif // #ifdef DEBUG

	LPCSTR section = *object().cNameSect();

	m_head_params.m_rotation = &object().sight().current_head_rotation();
	m_head_params.m_object = &object();
	m_head_params.m_blend = &global().blend();
	m_head_params.m_forward = forward_direction;

	int head_bone = kinematics->LL_BoneID(pSettings->r_string(section, "bone_head"));
	kinematics->LL_GetBoneInstance(u16(head_bone)).set_callback(bctCustom, &callback_rotation_blend, &m_head_params);

	m_shoulder_params.m_rotation = &object().sight().current_shoulder_rotation();
	m_shoulder_params.m_object = &object();
	m_shoulder_params.m_blend = &global().blend();
	m_shoulder_params.m_forward = forward_direction;

	int shoulder_bone = kinematics->LL_BoneID(pSettings->r_string(section, "bone_shoulder"));
	kinematics->LL_GetBoneInstance(u16(shoulder_bone)).set_callback(bctCustom, &callback_rotation_blend,
	                                                                &m_shoulder_params);

	m_spine_params.m_rotation = &object().sight().current_spine_rotation();
	m_spine_params.m_object = &object();
	m_spine_params.m_blend = &global().blend();
	m_spine_params.m_forward = forward_direction;
	m_spine_params.m_lean_roll = &m_lean_roll;
	m_spine_params.m_lean_shift = &m_lean_shift;

	int spine_bone = kinematics->LL_BoneID(pSettings->r_string(section, "bone_spin"));
	kinematics->LL_GetBoneInstance(u16(spine_bone)).set_callback(bctCustom, &callback_rotation_blend, &m_spine_params);

	//	remove_bone_callbacks	();
}

void CStalkerAnimationManager::remove_bone_callbacks()
{
	IKinematics* kinematics = smart_cast<IKinematics*>(m_visual);
	VERIFY(kinematics);

#ifdef DEBUG
	m_head_params.invalidate		();
	m_shoulder_params.invalidate	();
	m_spine_params.invalidate		();
#endif // #ifdef DEBUG

	LPCSTR section = *object().cNameSect();

	int head_bone = kinematics->LL_BoneID(pSettings->r_string(section, "bone_head"));
	kinematics->LL_GetBoneInstance(u16(head_bone)).set_callback(bctCustom, 0, 0);

	int shoulder_bone = kinematics->LL_BoneID(pSettings->r_string(section, "bone_shoulder"));
	kinematics->LL_GetBoneInstance(u16(shoulder_bone)).set_callback(bctCustom, 0, 0);

	int spin_bone = kinematics->LL_BoneID(pSettings->r_string(section, "bone_spin"));
	kinematics->LL_GetBoneInstance(u16(spin_bone)).set_callback(bctCustom, 0, 0);
}

void CStalkerAnimationManager::set_lean(float roll, float shift)
{
	clamp(roll, -PI_DIV_4, PI_DIV_4);
	clamp(shift, -0.4f, 0.4f);
	m_lean_roll_target = roll;
	m_lean_shift_target = shift;
}

// Toward the target at LEAN_ROLL_SPEED rad/s and LEAN_SHIFT_SPEED m/s (a full lean in about a quarter second)
void CStalkerAnimationManager::update_lean()
{
	static float const LEAN_ROLL_SPEED = 1.6f;
	static float const LEAN_SHIFT_SPEED = 0.8f;
	float const dt = Device.fTimeDelta;
	float d = m_lean_roll_target - m_lean_roll;
	float step = LEAN_ROLL_SPEED * dt;
	m_lean_roll += _abs(d) <= step ? d : (d > 0.f ? step : -step);
	d = m_lean_shift_target - m_lean_shift;
	step = LEAN_SHIFT_SPEED * dt;
	m_lean_shift += _abs(d) <= step ? d : (d > 0.f ? step : -step);
}

bool CStalkerAnimationManager::forward_blend_callbacks() const
{
	if (!m_head_params.m_blend)
		return (false);

	return (m_head_params.m_forward);
}

bool CStalkerAnimationManager::backward_blend_callbacks() const
{
	if (!m_head_params.m_blend)
		return (false);

	return (!m_head_params.m_forward);
}

void CStalkerAnimationManager::clear_unsafe_callbacks()
{
	if (!m_head_params.m_blend)
		return;

	assign_bone_callbacks();
}
