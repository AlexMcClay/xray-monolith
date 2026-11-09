//----------------------------------------------------
// file: PSObject.cpp
//----------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "ParticlesObject.h"
#include "../xrEngine/defines.h"
#include "../Include/xrRender/RenderVisual.h"
#include "../Include/xrRender/ParticleCustom.h"
#include "../xrEngine/render.h"
#include "../xrEngine/IGame_Persistent.h"
#include "../xrEngine/environment.h"

#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

const Fvector zero_vel = {0.f, 0.f, 0.f};

// console r__mt_particles: 1 = particle effects update in parallel once a frame (UpdateAllMT), 0 = one by one as
// before (scheduler and draw)
int ps_mt_particles = 1;

CParticlesObject::CParticlesObject(LPCSTR p_name, BOOL bAutoRemove, bool destroy_on_game_load) :
	inherited(destroy_on_game_load)
{
	Init(p_name, 0, bAutoRemove);
}

void CParticlesObject::Init(LPCSTR p_name, IRender_Sector* S, BOOL bAutoRemove)
{
	m_bLooped = false;
	m_bStopping = false;
	m_bAutoRemove = bAutoRemove;
	float time_limit = 0.0f;

	if (!g_dedicated_server)
	{
		// create visual
		renderable.visual = Render->model_CreateParticles(p_name);
		VERIFY(renderable.visual);
		IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
		VERIFY(V);
		time_limit = V->GetTimeLimit();
		// effects with a lit shader get the object lighting probe: sky and sun visibility at the effect (E1)
		renderable.pROS_Allowed = V->IsLit();
	}
	else
	{
		time_limit = 1.0f;
	}

	if (time_limit > 0.f)
	{
		m_iLifeTime = iFloor(time_limit * 1000.f);
	}
	else
	{
		if (bAutoRemove)
		{
			R_ASSERT3(!m_bAutoRemove, "Can't set auto-remove flag for looped particle system.", p_name);
		}
		else
		{
			m_iLifeTime = 0;
			m_bLooped = true;
		}
	}


	// spatial
	spatial.type = 0;
	spatial.sector = S;

	// sheduled
	shedule.t_min = 20;
	shedule.t_max = 50;
	shedule_register();

	dwLastTime = Device.dwTimeGlobal;
	mt_dt = 0;
	m_mt_want = false;
	m_mt_render_frame = 0;
}

CParticlesObject::~CParticlesObject()
{
	VERIFY(0==mt_dt);

	//	we do not need this since CPS_Instance does it
	//	shedule_unregister		();
}

void CParticlesObject::UpdateSpatial()
{
	if (g_dedicated_server) return;

	// spatial	(+ workaround occasional bug inside particle-system)
	vis_data& vis = renderable.visual->getVisData();
	if (_valid(vis.sphere))
	{
		Fvector P;
		float R;
		renderable.xform.transform_tiny(P, vis.sphere.P);
		R = vis.sphere.R;
		if (0 == spatial.type)
		{
			// First 'valid' update - register
			spatial.type = STYPE_RENDERABLE;
			spatial.sphere.set(P, R);
			spatial_register();
		}
		else
		{
			BOOL bMove = FALSE;
			if (!P.similar(spatial.sphere.P, EPS_L * 10.f)) bMove = TRUE;
			if (!fsimilar(R, spatial.sphere.R, 0.15f)) bMove = TRUE;
			if (bMove)
			{
				spatial.sphere.set(P, R);
				spatial_move();
			}
		}
	}
}

const shared_str CParticlesObject::Name()
{
	if (g_dedicated_server) return "";

	IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
	VERIFY(V);
	return (V) ? V->Name() : "";
}

void CParticlesObject::Play(bool bHudMode)
{
	if (g_dedicated_server) return;

	IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
	VERIFY(V);
	if (bHudMode)
		V->SetHudMode(bHudMode);

	V->Play();
	dwLastTime = Device.dwTimeGlobal - 33ul;
	mt_dt = 0;
	PerformAllTheWork(0);
	m_bStopping = false;
}

void CParticlesObject::play_at_pos(const Fvector& pos, BOOL xform)
{
	if (g_dedicated_server) return;

	IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
	VERIFY(V);
	Fmatrix m;
	m.translate(pos);
	V->UpdateParent(m, zero_vel, xform);
	V->Play();
	dwLastTime = Device.dwTimeGlobal - 33ul;
	mt_dt = 0;
	PerformAllTheWork(0);
	m_bStopping = false;
}

void CParticlesObject::Stop(BOOL bDefferedStop)
{
	if (g_dedicated_server) return;

	IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
	VERIFY(V);
	V->Stop(bDefferedStop);
	m_bStopping = true;
}

void CParticlesObject::shedule_Update(u32 _dt)
{
	inherited::shedule_Update(_dt);

	if (g_dedicated_server) return;

	// Update
	if (m_bDead) return;
	u32 dt = Device.dwTimeGlobal - dwLastTime;
	if (dt)
	{
		if (0)
		{
			//.psDeviceFlags.test(mtParticles))	{    //. AlexMX comment this line// NO UNCOMMENT - DON'T WORK PROPERLY
			mt_dt = dt;
			fastdelegate::FastDelegate0<> delegate(this, &CParticlesObject::PerformAllTheWork_mt);
			Device.seqParallel.push_back(delegate);
		}
		else if (ps_mt_particles)
		{
			// done with the others in UpdateAllMT before the world is drawn (it also updates the spatial)
			m_mt_want = true;
			return;
		}
		else
		{
			mt_dt = 0;
			IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
			VERIFY(V);
			V->OnFrame(dt);
		}
		dwLastTime = Device.dwTimeGlobal;
	}
	UpdateSpatial();
}

void CParticlesObject::PerformAllTheWork(u32 _dt)
{
	if (g_dedicated_server) return;

	// Update
	u32 dt = Device.dwTimeGlobal - dwLastTime;
	if (dt)
	{
		IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
		VERIFY(V);
		V->OnFrame(dt);
		dwLastTime = Device.dwTimeGlobal;
	}
	UpdateSpatial();
}

// Parallel particle update (r__mt_particles). Every effect the scheduler asked for and every effect drawn last frame
// is updated here at once, spread over worker threads (tbb), instead of one by one (in shedule_Update and while
// drawing). Each effect touches only its own data; what effects share is made safe in the renderer: child effects
// created and deleted from inside an update go through the model pool's lock (CModelPool), the particle manager's
// lists don't grow while this runs (reserved), collision ray picks lock the object space themselves, and the
// turbulence noise is set up once. Spatial updates stay on this thread, after. Effects that come into view this
// frame are still updated while drawing (renderable_Render), as before.
void CParticlesObject::UpdateAllMT()
{
	if (!ps_mt_particles || g_dedicated_server || !g_pGamePersistent)
		return;
	static xr_vector<CParticlesObject*> due;
	due.clear();
	const u32 now = Device.dwTimeGlobal;
	const u32 frame = Device.dwFrame;
	for (CPS_Instance* psi : g_pGamePersistent->ps_active)
	{
		// CParticlesObject is the only CPS_Instance
		CParticlesObject* P = static_cast<CParticlesObject*>(psi);
		if (P->m_bDead || !P->renderable.visual || P->dwLastTime == now)
			continue;
		if (P->m_mt_want || P->m_mt_render_frame + 1 >= frame)
			due.push_back(P);
	}
	if (due.empty())
		return;

	auto update = [now](CParticlesObject* P)
	{
		IParticleCustom* V = smart_cast<IParticleCustom*>(P->renderable.visual);
		if (V)
			V->OnFrame(now - P->dwLastTime);
		P->dwLastTime = now;
	};
	if (due.size() == 1)
		update(due[0]);
	else
		tbb::parallel_for(tbb::blocked_range<size_t>(0, due.size(), 2), [&](const tbb::blocked_range<size_t>& r)
		{
			for (size_t i = r.begin(); i != r.end(); ++i)
				update(due[i]);
		});

	for (CParticlesObject* P : due)
		if (P->m_mt_want)
		{
			P->m_mt_want = false;
			P->UpdateSpatial();
		}
}

void CParticlesObject::PerformAllTheWork_mt()
{
	if (g_dedicated_server) return;

	if (0 == mt_dt) return; //???
	IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
	VERIFY(V);
	V->OnFrame(mt_dt);
	mt_dt = 0;
}

void CParticlesObject::SetXFORM(const Fmatrix& m)
{
	if (g_dedicated_server) return;

	IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
	VERIFY(V);
	V->UpdateParent(m, zero_vel,TRUE);
	renderable.xform.set(m);
	UpdateSpatial();
}

void CParticlesObject::UpdateParent(const Fmatrix& m, const Fvector& vel)
{
	if (g_dedicated_server) return;

	IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
	VERIFY(V);
	V->UpdateParent(m, vel,FALSE);
	UpdateSpatial();
}

Fvector& CParticlesObject::Position()
{
	if (g_dedicated_server)
	{
		static Fvector _pos = Fvector().set(0, 0, 0);
		return _pos;
	}
	vis_data& vis = renderable.visual->getVisData();
	return vis.sphere.P;
}

float CParticlesObject::shedule_Scale()
{
	if (g_dedicated_server) return 5.0f;

	return Device.vCameraPosition.distance_to(Position()) / 200.f;
}

void CParticlesObject::renderable_Render()
{
	VERIFY(renderable.visual);
	m_mt_render_frame = Device.dwFrame; // due again next frame (UpdateAllMT)
	u32 dt = Device.dwTimeGlobal - dwLastTime;
	if (dt)
	{
		IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
		VERIFY(V);
		V->OnFrame(dt);
		dwLastTime = Device.dwTimeGlobal;
	}

	::Render->set_Transform(&renderable.xform);
	::Render->add_Visual(renderable.visual);
}

bool CParticlesObject::IsAutoRemove()
{
	if (m_bAutoRemove) return true;
	else return false;
}

void CParticlesObject::SetAutoRemove(bool auto_remove)
{
	VERIFY(m_bStopping || !IsLooped());
	m_bAutoRemove = auto_remove;
}

void CParticlesObject::SetHudMode(bool bHudMode)
{
	if (g_dedicated_server) return;

	IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
	VERIFY(V);
	V->SetHudMode(bHudMode);
}

//играются ли партиклы, отличается от PSI_Alive, тем что после
//остановки Stop партиклы могут еще доигрывать анимацию IsPlaying = true
bool CParticlesObject::IsPlaying()
{
	if (g_dedicated_server) return false;

	IParticleCustom* V = smart_cast<IParticleCustom*>(renderable.visual);
	VERIFY(V);
	return !!V->IsPlaying();
}
