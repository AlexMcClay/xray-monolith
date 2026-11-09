////////////////////////////////////////////////////////////////////////////
//	Module 		: script_particles.h
//	Created 	: 27.07.2004
//  Modified 	: 27.07.2004
//	Author		: Alexander Maximchuk
//	Description : XRay Script particles class
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "script_export_space.h"
#include "particlesobject.h"

// refs
class CObjectAnimator;
class CScriptParticles;
class CScriptGameObject;

class CScriptParticlesCustom : public CParticlesObject
{
	CObjectAnimator* m_animator;
	virtual ~CScriptParticlesCustom();
	CScriptParticles* m_owner;

	// following a bone (attach_to_bone): the object, its bone, the effect's transform in the bone's space
	u16 m_follow_id;
	u16 m_follow_bone;
	Fmatrix m_follow_local;
	Fvector m_follow_prev;
	u32 m_follow_frame;
public:
	CScriptParticlesCustom(CScriptParticles* owner, LPCSTR caParticlesName);
	virtual void shedule_Update(u32 dt);
	virtual void UpdateFollow();
	bool AttachToBone(CObject* O, u16 bone, const Fmatrix& world);
	void Detach() { m_follow_id = u16(-1); }
	bool Attached() const { return m_follow_id != u16(-1); }

	void LoadPath(LPCSTR caPathName);
	void StartPath(bool looped);
	void StopPath();
	void PausePath(bool val);
	virtual void PSI_internal_delete();
	virtual void PSI_destroy();
	void remove_owner();
};

class CScriptParticles
{
private:
	Fmatrix m_transform;
public:
	CScriptParticlesCustom* m_particles;
	CScriptParticles(LPCSTR caParticlesName);
	virtual ~CScriptParticles();

	void Play(bool bHudMode = false);
	void PlayAtPos(const Fvector& pos, bool bHudMode = false);
	void Stop();
	void StopDeffered();

	bool IsPlaying() const;
	bool IsLooped() const;

	void MoveTo(const Fvector& pos, const Fvector& vel);
	void XFORMMoveTo(const Fvector & pos);
	void SetDirection(const Fvector& dir);
	void SetOrientation(float yaw, float pitch, float roll);
	void SetHudMode(bool bHudMode);
	// per-instance controls (Gunsmith Docs vfx.md, E5): colour and alpha multipliers, size, time and rate
	void SetColor(float r, float g, float b, float a);
	void SetSizeScale(float k);
	void SetSpeedScale(float k);
	void SetTimeScale(float k);
	void SetRateScale(float k);
	u32 ParticlesCount() const;
	// follow a bone of obj (bone id; an unknown id = the root bone) keeping the effect's current place and
	// direction relative to it: the emitter moves with the bone every frame (particles already out stay where they
	// are). Ends by itself when the object goes away. detach() stops following.
	bool AttachToBone(CScriptGameObject* obj, u16 bone);
	void Detach();
	bool Attached() const;
	// decals where its particles hit the level (effects with collision on): wallmark section (as wallmarks_manager),
	// chance per particle, size (x 0.75-1.25), ttl (0 = default), most per play. section "" turns it off.
	void SetCollisionMarks(LPCSTR section, float chance, float size, float ttl, int max_marks);
	Fvector LastPosition() const { return m_transform.c; }
	void LoadPath(LPCSTR caPathName);
	void StartPath(bool looped);
	void StopPath();
	void PausePath(bool val);

DECLARE_SCRIPT_REGISTER_FUNCTION
};

#include "script_particles_inline.h"
