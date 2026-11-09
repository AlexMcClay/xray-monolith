// Level_Bullet_Manager.h:  для обеспечения полета пули по траектории
//							все пули и осколки передаются сюда
//////////////////////////////////////////////////////////////////////

#pragma once


#include "weaponammo.h"
#include "tracer.h"

//коэфициенты и параметры патрона
struct SBullet_Hit
{
	float power; // power          * cartridge
	float impulse; // impulse        * cartridge
	u8 impact = 2; // what the bullet did (ObjectHit): 0 ricochet, 1 stopped in it, 2 went through
};

//структура, описывающая пулю и ее свойства в полете
struct SBullet
{
	u32 init_frame_num; //номер кадра на котором была запущена пуля
	union
	{
		struct
		{
			u16 ricochet_was : 1; //пуля срикошетила
			u16 explosive : 1; //special explosive mode for particles
			u16 allow_tracer : 1;
			u16 allow_ricochet : 1; //разрешить рикошет
			u16 allow_sendhit : 1; //statistics
			//.			u16			skipped_frame	: 1	;			//пропуск первой отрисовки
			u16 aim_bullet : 1; //прицеленная пуля( вылетевшая первой после длительного молчания оружия (1-3 сек.))
			u16 magnetic_beam : 1;
			//магнитный луч (нет отклонения после пробивания, не падает скорость после пробивания)
		};

		u16 _storage;
	} flags;

	u16 bullet_material_idx;

	Fvector bullet_pos; //текущая позиция
	Fvector dir;
	float speed; //текущая скорость

	u16 parent_id; //ID персонажа который иницировал действие
	u16 weapon_id; //ID оружия из которого была выпущены пуля
	LPCSTR catridgeSection;
	static u32 bulletCount;
	u32 bulletId;

	float fly_dist; //дистанция которую пуля пролетела
	Fvector tracer_start_position;

	Fvector start_position;
	Fvector start_velocity;
	u32 born_time;
	float life_time;
	u32 change_rajectory_count;

	//коэфициенты и параметры патрона
	SBullet_Hit hit_param;
	//-------------------------------------------------------------------
	float air_resistance;
	//-------------------------------------------------------------------
	float max_speed; // maxspeed*cartridge
	float max_dist; // maxdist*cartridge
	float armor_piercing; // ap
	float wallmark_size;
	//-------------------------------------------------------------------
	u8 m_u8ColorID;

	//тип наносимого хита
	ALife::EHitType hit_type;
	//---------------------------------
	u32 m_dwID;
	ref_sound m_whine_snd;
	ref_sound m_mtl_snd;
	//---------------------------------
	u16 targetID;
	//---------------------------------
	bool density_mode;
	float density;
	Fvector begin_density;
	LPCSTR section;
	bool operator ==(u32 ID) { return ID == m_dwID; }
public:
	SBullet();
	~SBullet();

	bool CanBeRenderedNow() const { return (Device.dwFrame > init_frame_num); }

	void Init(const Fvector& position,
	          const Fvector& direction,
	          float start_speed,
	          float power,
	          //.										float	power_critical,
	          float impulse,
	          u16 sender_id,
	          u16 sendersweapon_id,
	          ALife::EHitType e_hit_type,
	          float maximum_distance,
	          const CCartridge& cartridge,
	          float const air_resistance_factor,
	          bool SendHit,
	          int iShotNum = 0);
};

class CLevel;

class CBulletManager
{
private:
	static float const parent_ignore_distance;

private:
	collide::rq_results rq_storage;
	collide::rq_results m_rq_results;

private:
	DEFINE_VECTOR(ref_sound, SoundVec, SoundVecIt);
	DEFINE_VECTOR(SBullet, BulletVec, BulletVecIt);
	friend CLevel;

	enum EventType
	{
		EVENT_HIT = u8(0),
		EVENT_REMOVE,

		EVENT_DUMMY = u8(-1),
	};

	struct _event
	{
		EventType Type;
		BOOL dynamic;
		BOOL Repeated; // последовательное повторное попадание в динамический объект
		SBullet_Hit hit_result;
		SBullet bullet;
		Fvector normal;
		Fvector point;
		collide::rq_result R;
		u16 tgt_material;
	};

	static void CalculateNewVelocity(Fvector& dest_new_vel, Fvector const& old_velocity, float ar, float life_time);
protected:
	SoundVec m_WhineSounds;
	RStringVec m_ExplodeParticles;

	//список пуль находящихся в данный момент на уровне
	//.	xrCriticalSection		m_Lock				;

	BulletVec m_Bullets; // working set, locked
	BulletVec m_BulletsRendered; // copy for rendering
	xr_vector<_event> m_Events;

	// Bullets passing a stalker's bounding sphere without the stalker being the shooter, queued by the trace (it runs
	// on the parallel thread) and handed to scripts at the next CommitEvents (main thread):
	// _G.CAI_Stalker__OnBulletNear(npc, shooter_id, point), once per stalker per frame. For near-miss reactions.
	struct near_event
	{
		u16 npc_id;
		u16 shooter_id;
		Fvector point;
	};
	xr_vector<near_event> m_NearEvents;

#ifdef DEBUG
	u32						m_thread_id;

	typedef xr_vector<Fvector>	BulletPoints;
	BulletPoints			m_bullet_points;
#endif // #ifdef DEBUG

	//отрисовка трассеров от пуль
	CTracer tracers;

	//минимальная скорость, на которой пуля еще считается
	static float m_fMinBulletSpeed;

	float m_fHPMaxDist;

	//константа G
	float m_fGravityConst;
	//сопротивление воздуха, процент, который отнимается от скорости
	//полета пули
	float m_fAirResistanceK;
	// Gunsmith ballistics ([bullet_manager] keys; without them the game plays as vanilla):
	// per_ammo_air_resistance: single player flies each bullet with its own air resistance (the round's
	// k_air_resistance x the gun's air_resistance_factor), as multiplayer does, instead of air_resistance_k for all
	bool m_bPerAmmoAirResistance;
	// damage_degradation (0..1): hit power and impulse scale by r + (1 - r) x this, r = speed / launch speed
	// (Tarkov's GlobalDamageDegradationCoefficient, 0.5); 0 keeps vanilla's r
	float m_fDamageDegradation;
	float air_resistance_of(SBullet const& bullet) const;
	//cколько процентов энергии потеряет пуля при столкновении с материалом (при падении под прямым углом)
	float m_fCollisionEnergyMin;
	//сколькол процентов энергии устанется у пули при любом столкновении
	float m_fCollisionEnergyMax;
	// the bullet_on_impact script set handled = true: this event's particles, decal and sound are skipped
	// (FireShotmark), the hit itself is not (Gunsmith Docs vfx.md, E7)
	bool m_skip_impact_fx = false;
	// bullet_on_impact skip_particles = true: a script plays this hit's particles itself (decal and sound stay)
	bool m_skip_impact_particles = false;
	// bullet_on_impact skip_sound = true: a script plays this hit's sound itself (particles and decal stay;
	// Tarkov SFX plays Tarkov's close or distant clip by distance, Gunsmith Docs sfx.md)
	bool m_skip_impact_sound = false;

	//параметры отрисовки трассеров
	float m_fTracerWidth;
	float m_fTracerLengthMax;
	float m_fTracerLengthMin;
	// optional [bullet_manager] keys (0 = off, vanilla): the width grows over the bullet's first
	// tracer_width_grow_time seconds (1 - (1 - t)^2); the length is at most speed x tracer_length_speed_k
	// (seconds of flight); the tracer fades out from tracer_fade_start and is gone at tracer_life seconds
	float m_fTracerWidthGrow = 0.f;
	float m_fTracerLengthSpeedK = 0.f;
	float m_fTracerLife = 0.f;
	float m_fTracerFadeStart = 0.f;
protected:
	void PlayWhineSound(SBullet* bullet, CObject* object, const Fvector& pos);
	void PlayExplodePS(const Fmatrix& xf);
	//функция обработки хитов объектов
	static BOOL test_callback(const collide::ray_defs& rd, CObject* object, LPVOID params);
	static BOOL firetrace_callback(collide::rq_result& result, LPVOID params);

	// Deffer event
	void RegisterEvent(EventType Type, BOOL _dynamic, SBullet* bullet, const Fvector& end_point, collide::rq_result& R,
	                   u16 target_material);

	//попадание по динамическому объекту
	void DynamicObjectHit(_event& E);

	//попадание по статическому объекту
	void StaticObjectHit(_event& E);

	//попадание по любому объекту, на выходе - импульс и сила переданные пулей объекту
	bool ObjectHit(SBullet_Hit* hit_res, SBullet* bullet, const Fvector& end_point,
	               collide::rq_result& R, u16 target_material, Fvector& hit_normal);
	//отметка на пораженном объекте
	void FireShotmark(SBullet* bullet, const Fvector& vDir,
	                  const Fvector& vEnd, collide::rq_result& R, u16 target_material,
	                  const Fvector& vNormal, bool ShowMark = true);
	//просчет полета пули за некоторый промежуток времени
	//принимается что на этом участке пуля движется прямолинейно
	//и равномерно, а после просчета также изменяется текущая
	//скорость и положение с учетом гравитации и ветра
	//возвращаем true если пуля продолжает полет
	bool trajectory_check_error(
		Fvector& previous_position,
		collide::rq_results& rq_storage,
		SBullet& bullet,
		float& low,
		float& high,
		Fvector const& gravity,
		float const air_resistance
	);
	void add_bullet_point(
		Fvector const& start_position,
		Fvector& previous_position,
		Fvector const& start_velocity,
		Fvector const& gravity,
		float const ait_resistance,
		float const current_time
	);
	bool process_bullet(
		collide::rq_results& rq_storage,
		SBullet& bullet,
		u32 delta_time
	);
	void __stdcall UpdateWorkload();
public:
	CBulletManager();
	virtual ~CBulletManager();

	void Load();
	void Clear();
	void AddBullet(const Fvector& position, const Fvector& direction, float starting_speed,
	               float power, /*float power_critical,*/ float impulse,
	               u16 sender_id, u16 sendersweapon_id,
	               ALife::EHitType e_hit_type, float maximum_distance,
	               const CCartridge& cartridge,
	               float const air_resistance_factor,
	               bool SendHit, bool AimBullet = false, int iShotNum = 0);

	void CommitEvents(); // @ the start of frame
	void CommitRenderSet(); // @ the end of frame
	void Render();

#ifdef CBULLETMANAGER_EX
    bool ValidateHitDynamicVisualMesh(SBullet& B, collide::rq_result& R, Fvector& collide_position, float range);
#endif
};

struct bullet_test_callback_data
{
	Fvector collide_position;
	SBullet* pBullet;
	float collide_time;
#if 1//def DEBUG
	float high_time;
#endif // #ifdef DEBUG
#ifdef CBULLETMANAGER_EX
    float max_distance;
#endif
};
