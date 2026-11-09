#ifndef ParticlesObjectH
#define ParticlesObjectH

#include "../xrEngine/PS_instance.h"

extern const Fvector zero_vel;

class CParticlesObject : public CPS_Instance
{
	typedef CPS_Instance inherited;

	u32 dwLastTime;
	void Init(LPCSTR p_name, IRender_Sector* S, BOOL bAutoRemove);
	void UpdateSpatial();

protected:
	bool m_bLooped; //флаг, что система зациклена
	bool m_bStopping; //вызвана функция Stop()

protected:
	u32 mt_dt;

	// parallel particle update (r__mt_particles): the scheduler only asks for an update (m_mt_want), and an effect
	// drawn last frame (m_mt_render_frame) is due every frame; UpdateAllMT runs them all at once on worker threads
	bool m_mt_want;
	u32 m_mt_render_frame;

protected:
	virtual ~CParticlesObject();

public:
	CParticlesObject(LPCSTR p_name, BOOL bAutoRemove, bool destroy_on_game_load);

	virtual bool shedule_Needed() { return true; };
	virtual float shedule_Scale();
	virtual void shedule_Update(u32 dt);
	virtual void renderable_Render();
	void PerformAllTheWork(u32 dt);
	// once a frame before the world is drawn (CLevel::OnRender): update every due effect in parallel
	static void UpdateAllMT();
	// once a frame on the main thread, before the effects update (UpdateAllMT) and in the scheduler: an effect that
	// follows something moves its emitter here (script particles attached to a bone)
	virtual void UpdateFollow() {}
	void __stdcall PerformAllTheWork_mt();

	Fvector& Position();
	void SetXFORM(const Fmatrix& m);
	IC Fmatrix& XFORM() { return renderable.xform; }
	void UpdateParent(const Fmatrix& m, const Fvector& vel);

	void play_at_pos(const Fvector& pos, BOOL xform = FALSE);
	virtual void Play(bool bHudMode);
	void Stop(BOOL bDefferedStop = TRUE);
	virtual BOOL Locked() { return mt_dt; }

	bool IsLooped() { return m_bLooped; }
	bool IsAutoRemove();
	bool IsPlaying();
	void SetAutoRemove(bool auto_remove);
	void SetHudMode(bool bHudMode);

	const shared_str Name();
public:
	static CParticlesObject* Create(LPCSTR p_name, BOOL bAutoRemove = TRUE, bool remove_on_game_load = true)
	{
		return xr_new<CParticlesObject>(p_name, bAutoRemove, remove_on_game_load);
	}

	static void Destroy(CParticlesObject*& p)
	{
		if (p)
		{
			p->PSI_destroy();
			p = 0;
		}
	}
};

#endif /*ParticlesObjectH*/
