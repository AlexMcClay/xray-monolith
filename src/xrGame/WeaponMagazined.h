#pragma once

#include "weapon.h"
#include "hudsound.h"
#include "ai_sounds.h"

class ENGINE_API CMotionDef;
class CAnonHudItem;

//размер очереди считается бесконечность
//заканчиваем стрельбу, только, если кончились патроны
#define WEAPON_ININITE_QUEUE -1

class CWeaponMagazined : public CWeapon
{
private:
	typedef CWeapon inherited;
protected:
	//звук текущего выстрела
	shared_str m_sSndShotCurrent;

	// A firing loop (snd_shoot_loop, snd_shoot_actor_loop and the silencer's), as Tarkov plays automatic fire: a
	// recording of automatic fire played looped while a queue of shots lasts, cut on its next beat (its beats at
	// snd_shoot_loop_rpm) once the shots stop, and its tail (snd_*_tail) from there, instead of a sound per shot.
	struct SShotLoop
	{
		shared_str base;        // the loop's alias (sndShotActorLoop, ...)
		shared_str loop, tail;  // the alias playing (a script may swap the sound), its tail's
		float start = 0.f;      // Device.fTimeGlobal it started at
		float beat = 0.f;       // the recording's shot period
		float end_at = 0.f;     // when it is cut, once known
		bool hud = false;
	} m_shot_loop;
	float m_shot_loop_rpm = 0.f, m_shot_loop_rpm_actor = 0.f;
	float m_shot_pitch_random = 0.f;    // a single shot's pitch varies by up to this (snd_shoot_pitch_random)
	bool PlayShotLoop(bool actor);
	void UpdateShotLoop(bool force_end = false);
	float ShotPitch() const;

	// Tarkov's indoor shots (each shot key's <key>_indoor, alias <alias>Indoor): played when the shooter is under a
	// roof (ShotIndoors), chosen on a queue's first shot
	bool m_shot_has_indoor = false;
	bool m_shot_indoor = false;
	bool m_indoor_last = false;     // ShotIndoors' last answer, its time and place
	float m_indoor_time = -1.f;
	Fvector m_indoor_pos = {0.f, 0.f, 0.f};
	bool ShotIndoors();
	LPCSTR ShotAlias(LPCSTR alias, string128& buf);

	// Tarkov's sonic crack (snd_bullet_crack): another's supersonic bullet passing close to the camera, played at the
	// shot with the shot (so after the same distance delay), its volume by how close the bullet passes
	struct SBulletCrack
	{
		float speed = 340.29f;      // snd_bullet_crack_speed: the slowest bullet that cracks (m/s)
		float min_dist = 1.5f;      // snd_bullet_crack_min_dist: no crack from a shooter closer than this
		float cone = 0.125f;        // snd_bullet_crack_cone: passing within this share of the shooter's distance
		xr_vector<Fvector2> curve;  // snd_bullet_crack_curve: (miss / (cone x distance), volume) pairs, linear between
		// its reach (snd_bullet_crack_rolloff, _silencer: the gun's fire bank's Rolloff; 0 no limit): Tarkov plays it
		// to maxDistance = lerp(2000, rolloff, distance / rolloff) and fades it by snd_bullet_crack_rolloff_curve
		// (distance / maxDistance, volume)
		float rolloff = 0.f, rolloff_silencer = 0.f;
		xr_vector<Fvector2> rolloff_curve;
	} m_crack;
	bool m_has_crack = false;
	void PlayBulletCrack(const Fvector& pos, const Fvector& dir, float speed);

	//дополнительная информация о глушителе
	LPCSTR m_sSilencerFlameParticles;
	LPCSTR m_sSilencerSmokeParticles;

	ESoundTypes m_eSoundShow;
	ESoundTypes m_eSoundHide;
	ESoundTypes m_eSoundShot;
	ESoundTypes m_eSoundEmptyClick;
	ESoundTypes m_eSoundReload;
	ESoundTypes m_eSoundReloadEmpty;
	ESoundTypes m_eSoundReloadMisfire;

	bool m_sounds_enabled;
	bool m_nextFireMode;
	bool m_needReload;
    bool m_pendingShot;
	// General
	//кадр момента пересчета UpdateSounds
	u32 dwUpdateSounds_Frame = 0;
protected:
	virtual void OnMagazineEmpty();

	virtual void switch2_Idle();
	virtual void switch2_Fire();
	virtual void switch2_Reload();
	virtual void switch2_Hiding();
	virtual void switch2_Hidden();
	virtual void switch2_Showing();

	virtual void switch2_StartAim();
	virtual void switch2_EndAim();

	virtual void on_a_hud_attach();
	virtual void on_b_hud_detach();

	virtual void OnShot();
	virtual void PlaySoundShot();

	virtual void OnEmptyClick();

	virtual void OnAnimationEnd(u32 state);
	virtual void OnStateSwitch(u32 S, u32 oldState);

	virtual void UpdateSounds();
	virtual void UpdateSoundsPositionsImpl();
	void __stdcall UpdateSoundsPositions();

	bool TryReload();

private:
	LPCSTR empty_click_layer;
	float empty_click_speed;
	float empty_click_power;

protected:
	virtual void ReloadMagazine();
	void ApplySilencerKoeffs();
	void ApplyScopeKoeffs();
	void ResetSilencerKoeffs();
	void ResetScopeKoeffs();

	bool cycleDownCheck();

	virtual void state_Fire(float dt);
public:
	CWeaponMagazined(ESoundTypes eSoundType = SOUND_TYPE_WEAPON_SUBMACHINEGUN);
	virtual ~CWeaponMagazined();

	virtual void Load(LPCSTR section);
	void LoadSilencerKoeffs();
	void LoadScopeKoeffs();

	virtual CWeaponMagazined* cast_weapon_magazined()
	{
		return this;
	}

	virtual void SetDefaults();
	virtual void FireStart();
	virtual void FireEnd();
	virtual void Reload();

	virtual void UpdateCL();
	virtual void net_Destroy();
	virtual void net_Export(NET_Packet& P);
	virtual void net_Import(NET_Packet& P);

	virtual void OnMotionMark(u32 state, const motion_marks& M);
	virtual int     CheckAmmoBeforeReload(u8& v_ammoType);

	virtual void OnH_A_Chield();

	virtual bool Attach(PIItem pIItem, bool b_send_event);
	virtual bool Detach(const char* item_section_name, bool b_spawn_item);
	bool DetachScope(const char* item_section_name, bool b_spawn_item);
	virtual bool CanAttach(PIItem pIItem);
	virtual bool CanDetach(const char* item_section_name);

	virtual void InitAddons();
	virtual void gs_on_suppression_changed();
	virtual void gs_apply_mag_size(int size, bool live);

	virtual bool Action(u16 cmd, u32 flags);
	bool IsAmmoAvailable();
	virtual void UnloadMagazine(bool spawn_ammo = true);

	virtual bool GetBriefInfo(II_BriefInfo& info);

public:
	virtual bool SwitchMode();

	virtual bool SingleShotMode()
	{
		return 1 == m_iQueueSize;
	}

	virtual void SetQueueSize(int size);
	IC int GetQueueSize() const
	{
		return m_iQueueSize;
	};

	virtual bool StopedAfterQueueFired()
	{
		return m_bStopedAfterQueueFired;
	}

	virtual void StopedAfterQueueFired(bool value)
	{
		m_bStopedAfterQueueFired = value;
	}

	virtual float GetFireDispersion(float cartridge_k, bool for_crosshair = false);

protected:
	//максимальный размер очереди, которой можно стрельнуть
	int m_iQueueSize;
	//количество реально выстреляных патронов
	int m_iShotNum;
	//после какого патрона, при непрерывной стрельбе, начинается отдача (сделано из-за Абакана)
	int m_iBaseDispersionedBulletsCount;
	//скорость вылета патронов, на которые не влияет отдача (сделано из-за Абакана)
	float m_fBaseDispersionedBulletsSpeed;
	//скорость вылета остальных патронов
	float m_fOldBulletSpeed;
	Fvector m_vStartPos, m_vStartDir;
	//флаг того, что мы остановились после того как выстреляли
	//ровно столько патронов, сколько было задано в m_iQueueSize
	bool m_bStopedAfterQueueFired;
	//флаг того, что хотя бы один выстрел мы должны сделать
	//(даже если очень быстро нажали на курок и вызвалось FireEnd)
	bool m_bFireSingleShot;
	//режимы стрельбы
	bool m_bHasDifferentFireModes;
	xr_vector<s8> m_aFireModes;
	int m_iCurFireMode;
	int m_iPrefferedFireMode;

	//переменная блокирует использование
	//только разных типов патронов
	bool m_bLockType;

public:
	virtual void OnZoomIn();
	virtual void OnZoomOut();

	bool HasFireModes()
	{
		return m_bHasDifferentFireModes;
	};

	virtual int GetCurrentFireMode()
	{
		//AVO: fixed crash due to original GSC assumption that CWeaponMagazined will always have firemodes specified in configs.
		//return m_aFireModes[m_iCurFireMode];
		if (HasFireModes())
			return m_aFireModes[m_iCurFireMode];
		else
			return 1;
	};

	virtual void SetFireMode(int mode)
	{
		if (mode >= m_aFireModes.size()) mode = 0;
		m_iCurFireMode = mode;
		SetQueueSize(GetCurrentFireMode());
	};

	virtual void save(NET_Packet& output_packet);
	virtual void load(IReader& input_packet);

protected:
	virtual bool install_upgrade_impl(LPCSTR section, bool test);

protected:
	virtual bool AllowFireWhileWorking()
	{
		return false;
	}

	//виртуальные функции для проигрывания анимации HUD
	virtual void PlayAnimShow();
	virtual void PlayAnimHide();
	virtual void PlayAnimReload();
	virtual void PlayAnimIdle();
	virtual void PlayAnimIdleMoving();
	virtual bool PlayAnimCrouchIdleMoving();
	virtual void PlayAnimIdleSprint();
	virtual void PlayAnimShoot();
	virtual void PlayReloadSound();
	virtual void PlayAnimAim();
	virtual void PlayAnimFireModeSwitch();
	virtual bool TryPlayAnimBore();

	virtual void UpdateFireMode();

	virtual int ShotsFired()
	{
		return m_iShotNum;
	}

	virtual float GetWeaponDeterioration();

	virtual void FireBullet(const Fvector& pos,
	                        const Fvector& dir,
	                        float fire_disp,
	                        const CCartridge& cartridge,
	                        u16 parent_id,
	                        u16 weapon_id,
	                        bool send_hit);
	//AVO: for custom added sounds check if sound exists
	bool WeaponSoundExist(LPCSTR section, LPCSTR sound_name);
};
