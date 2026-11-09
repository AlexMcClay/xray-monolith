////////////////////////////////////////////////////////////////////////////
//	Module 		: script_sound.cpp
//	Created 	: 06.02.2004
//  Modified 	: 06.02.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script sound class
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "script_particles.h"
#include "../xrEngine/objectanimator.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../Include/xrRender/ParticleCustom.h"
#include "../Include/xrRender/Kinematics.h"
#include "script_game_object.h"
#include "script_wallmarks_manager.h"
#include "GamePersistent.h"
#include "Level.h"

CScriptParticlesCustom::CScriptParticlesCustom(CScriptParticles* owner, LPCSTR caParticlesName): CParticlesObject(
	caParticlesName,FALSE, true)
{
	m_owner = owner;
	m_animator = 0;
	m_follow_id = u16(-1);
	m_follow_bone = 0;
	m_follow_local.identity();
	m_follow_prev.set(0, 0, 0);
	m_follow_frame = 0;
}

CScriptParticlesCustom::~CScriptParticlesCustom()
{
	xr_delete(m_animator);
}

void CScriptParticlesCustom::PSI_internal_delete()
{
	if (m_owner)
		m_owner->m_particles = NULL;
	CParticlesObject::PSI_internal_delete();
}

void CScriptParticlesCustom::PSI_destroy()
{
	if (m_owner)
		m_owner->m_particles = NULL;
	CParticlesObject::PSI_destroy();
}

static bool bone_world(CObject* O, u16& bone, Fmatrix& out)
{
	IKinematics* K = O && O->Visual() ? O->Visual()->dcast_PKinematics() : nullptr;
	if (!K)
		return false;
	if (bone >= K->LL_BoneCount())
		bone = K->LL_GetBoneRoot();
	K->CalculateBones();
	out.mul_43(O->XFORM(), K->LL_GetTransform(bone));
	return true;
}

bool CScriptParticlesCustom::AttachToBone(CObject* O, u16 bone, const Fmatrix& world)
{
	Fmatrix bw, inv;
	if (!bone_world(O, bone, bw))
		return false;
	inv.invert(bw);
	m_follow_local.mul_43(inv, world);
	m_follow_id = O->ID();
	m_follow_bone = bone;
	m_follow_prev.set(world.c);
	m_follow_frame = 0;
	return true;
}

void CScriptParticlesCustom::UpdateFollow()
{
	if (m_follow_id == u16(-1) || m_follow_frame == Device.dwFrame || m_bDead)
		return;
	m_follow_frame = Device.dwFrame;
	CObject* O = g_pGameLevel ? Level().Objects.net_Find(m_follow_id) : nullptr;
	Fmatrix bw;
	if (!O || O->getDestroy() || !bone_world(O, m_follow_bone, bw))
	{
		m_follow_id = u16(-1);
		return;
	}
	Fmatrix m;
	m.mul_43(bw, m_follow_local);
	Fvector vel;
	vel.set(0, 0, 0);
	if (Device.fTimeDelta > EPS_S)
		vel.sub(m.c, m_follow_prev).div(Device.fTimeDelta);
	m_follow_prev.set(m.c);
	UpdateParent(m, vel);
}

void CScriptParticlesCustom::shedule_Update(u32 _dt)
{
	UpdateFollow();
	CParticlesObject::shedule_Update(_dt);
	if (m_animator)
	{
		float dt = float(_dt) / 1000.f;
		Fvector prev_pos = m_animator->XFORM().c;
		m_animator->Update(dt);
		Fvector vel;
		vel.sub(m_animator->XFORM().c, prev_pos).div(dt);
		UpdateParent(m_animator->XFORM(), vel);
	}
}

void CScriptParticlesCustom::LoadPath(LPCSTR caPathName)
{
	if (!m_animator) m_animator = xr_new<CObjectAnimator>();
	if ((0 == m_animator->Name()) || (0 != xr_strcmp(m_animator->Name(), caPathName)))
	{
		m_animator->Clear();
		m_animator->Load(caPathName);
	}
}

void CScriptParticlesCustom::StartPath(bool looped)
{
	VERIFY(m_animator);
	m_animator->Play(looped);
}

void CScriptParticlesCustom::PausePath(bool val)
{
	VERIFY(m_animator);
	m_animator->Pause(val);
}

void CScriptParticlesCustom::StopPath()
{
	VERIFY(m_animator);
	m_animator->Stop();
}

void CScriptParticlesCustom::remove_owner()
{
	R_ASSERT(m_owner);
	m_owner = 0;
}

CScriptParticles::CScriptParticles(LPCSTR caParticlesName)
{
	m_particles = xr_new<CScriptParticlesCustom>(this, caParticlesName);
	m_transform.identity();
}

CScriptParticles::~CScriptParticles()
{
	if (m_particles)
	{
		// destroy particles
		m_particles->remove_owner();
		m_particles->PSI_destroy();
		m_particles = 0;
	}
}

void CScriptParticles::Play(bool bHudMode)
{
	VERIFY(m_particles);
	m_particles->Play(bHudMode);
}

void CScriptParticles::PlayAtPos(const Fvector& position, bool bHudMode)
{
	VERIFY(m_particles);
	//m_particles->play_at_pos	(position);
	m_transform.translate_over(position);
	m_particles->UpdateParent(m_transform, zero_vel);
	m_particles->Play(bHudMode);
	m_particles->UpdateParent(m_transform, zero_vel);
}

void CScriptParticles::Stop()
{
	VERIFY(m_particles);
	m_particles->Stop(FALSE);
}

void CScriptParticles::StopDeffered()
{
	VERIFY(m_particles);
	m_particles->Stop(TRUE);
}

void CScriptParticles::MoveTo(const Fvector& pos, const Fvector& vel)
{
	VERIFY(m_particles);
	m_transform.translate_over(pos);
	m_particles->UpdateParent(m_transform, vel);
}

void CScriptParticles::XFORMMoveTo(const Fvector& pos)
{
	VERIFY(m_particles);
	m_transform.translate_over(pos);
	m_particles->SetXFORM(m_transform);
}

void CScriptParticles::SetDirection(const Fvector& dir)
{
	Fmatrix matrix;
	matrix.identity();
	matrix.k.set(dir);
	Fvector::generate_orthonormal_basis_normalized(matrix.k, matrix.j, matrix.i);
	matrix.translate_over(m_transform.c);
	m_transform.set(matrix);
	m_particles->UpdateParent(matrix, zero_vel);
}

void CScriptParticles::SetOrientation(float yaw, float pitch, float roll)
{
	Fmatrix matrix = Fmatrix().set(m_transform);
	matrix.setHPB(yaw, pitch, roll); // ?????????? matrix.c
	matrix.translate_over(m_transform.c);
	m_transform.set(matrix);
	m_particles->SetXFORM(m_transform);
}

static IParticleCustom* script_particles_visual(CScriptParticlesCustom* p)
{
	return p ? smart_cast<IParticleCustom*>(p->renderable.visual) : nullptr;
}

void CScriptParticles::SetColor(float r, float g, float b, float a)
{
	if (IParticleCustom* V = script_particles_visual(m_particles))
		V->SetColorScale(r, g, b, a);
}

void CScriptParticles::SetSizeScale(float k)
{
	if (IParticleCustom* V = script_particles_visual(m_particles))
		V->SetSizeScale(k);
}

void CScriptParticles::SetSpeedScale(float k)
{
	if (IParticleCustom* V = script_particles_visual(m_particles))
		V->SetSpeedScale(k);
}

void CScriptParticles::SetTimeScale(float k)
{
	if (IParticleCustom* V = script_particles_visual(m_particles))
		V->SetTimeScale(k);
}

void CScriptParticles::SetRateScale(float k)
{
	if (IParticleCustom* V = script_particles_visual(m_particles))
		V->SetRateScale(k);
}

bool CScriptParticles::AttachToBone(CScriptGameObject* obj, u16 bone)
{
	if (!m_particles || !obj)
		return false;
	return m_particles->AttachToBone(&obj->object(), bone, m_transform);
}

void CScriptParticles::Detach()
{
	if (m_particles)
		m_particles->Detach();
}

bool CScriptParticles::Attached() const
{
	return m_particles && m_particles->Attached();
}

void CScriptParticles::SetCollisionMarks(LPCSTR section, float chance, float size, float ttl, int max_marks)
{
	IParticleCustom* V = script_particles_visual(m_particles);
	if (!V)
		return;
	IWallMarkArray* marks = nullptr;
	if (section && section[0] && g_pGamePersistent)
	{
		if (!pSettings->section_exist(section) || !pSettings->line_exist(section, "wallmarks"))
		{
			Msg("! particles_object:set_collision_marks: no section [%s] with wallmarks", section);
			return;
		}
		marks = GamePersistent().GetWallmarksManager().FindSection(section);
	}
	V->SetCollisionMarks(marks, chance, size, ttl, max_marks);
}

u32 CScriptParticles::ParticlesCount() const
{
	IParticleCustom* V = script_particles_visual(m_particles);
	return V ? V->ParticlesCount() : 0;
}

void CScriptParticles::SetHudMode(bool bHudMode)
{
	m_particles->SetHudMode(bHudMode);
}

bool CScriptParticles::IsPlaying() const
{
	VERIFY(m_particles);
	return m_particles->IsPlaying();
}

bool CScriptParticles::IsLooped() const
{
	VERIFY(m_particles);
	return m_particles->IsLooped();
}

void CScriptParticles::LoadPath(LPCSTR caPathName)
{
	VERIFY(m_particles);
	m_particles->LoadPath(caPathName);
}

void CScriptParticles::StartPath(bool looped)
{
	m_particles->StartPath(looped);
}

void CScriptParticles::StopPath()
{
	m_particles->StopPath();
}

void CScriptParticles::PausePath(bool val)
{
	m_particles->PausePath(val);
}
