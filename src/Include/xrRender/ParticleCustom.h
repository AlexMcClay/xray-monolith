#ifndef	ParticleCustom_included
#define	ParticleCustom_included
#pragma once

class IParticleCustom
{
public:
	virtual ~IParticleCustom() { ; }

	virtual void OnDeviceCreate() =0;
	virtual void OnDeviceDestroy() =0;

	virtual void UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM) =0;
	virtual void OnFrame(u32 dt) =0;

	virtual void Play() =0;
	virtual void Stop(BOOL bDefferedStop = TRUE) =0;
	virtual BOOL IsPlaying() =0;

	virtual u32 ParticlesCount() =0;

	virtual float GetTimeLimit() =0;
	virtual BOOL IsLooped() { return GetTimeLimit() < 0.f; }

	virtual const shared_str Name() =0;
	virtual void SetHudMode(BOOL b) =0;
	virtual BOOL GetHudMode() =0;

	// the effect (or one of a group's) asks for the object lighting probe (ROS) for its lit shader (E1)
	virtual BOOL IsLit() { return FALSE; }
	// per-instance controls (script particles_object; E5): colour and alpha multipliers, a size multiplier, the
	// speed time runs at, and the emission rate multiplier. A group passes them to its effects and their children.
	virtual void SetColorScale(float r, float g, float b, float a) { ; }
	virtual void SetSizeScale(float k) { ; }
	virtual void SetTimeScale(float k) { ; }
	virtual void SetRateScale(float k) { ; }
};

#endif	//	ParticleCustom_included
