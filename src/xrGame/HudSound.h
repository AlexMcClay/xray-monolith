#pragma once

struct HUD_SOUND_ITEM
{
	HUD_SOUND_ITEM() : m_activeSnd(NULL), m_b_exclusive(false), m_b_tracked(false)
	{
	}

	static void LoadSound(LPCSTR section, LPCSTR line,
	                      ref_sound& hud_snd,
	                      int type = sg_SourceType,
	                      float* volume = NULL,
	                      float* delay = NULL);

	static void LoadSound(LPCSTR section,
	                      LPCSTR line,
	                      HUD_SOUND_ITEM& hud_snd,
	                      int type = sg_SourceType);

	static void DestroySound(HUD_SOUND_ITEM& hud_snd);

	static void PlaySound(HUD_SOUND_ITEM& snd,
	                      const Fvector& position,
	                      const CObject* parent,
	                      bool hud_mode,
	                      bool looped = false,
	                      u8 index = u8(-1),
						  float volume_mult = 1.f,
						  float frequency = 1.f,
						  float delay_mult = 1.f);

	// fade > 0: a fade-out over that many seconds first (ref_sound::stop_fade)
	static void StopSound(HUD_SOUND_ITEM& snd, float fade = 0.f);

	ICF BOOL playing()
	{
		if (m_activeSnd) return m_activeSnd->snd._feedback() ? TRUE : FALSE;
		else return FALSE;
	}

	ICF void set_position(const Fvector& pos)
	{
		if (m_activeSnd)
		{
			if (m_activeSnd->snd._feedback() && !m_activeSnd->snd._feedback()->is_2D())
				m_activeSnd->snd.set_position(pos);
			else m_activeSnd = NULL;
		}
	}

	struct SSnd
	{
		ref_sound snd;
		float delay; //задержка перед проигрыванием
		float volume; //громкость
	};

	shared_str m_alias;
	SSnd* m_activeSnd;
	bool m_b_exclusive;
	// tracked: played with feedback as an exclusive sound (it can be stopped and checked), but other sounds of the
	// collection starting don't stop it (a firing loop)
	bool m_b_tracked;
	int m_type;
	xr_vector<SSnd> sounds;

	bool operator ==(LPCSTR alias) const
	{
		return 0 == stricmp(m_alias.c_str(), alias);
	}
};

class HUD_SOUND_COLLECTION
{
public:
	~HUD_SOUND_COLLECTION();
	shared_str m_alias; //Alundaio: For use when it's part of a layered Collection
	xr_vector<HUD_SOUND_ITEM> m_sound_items; //Alundaio: made public
	// a layer's distance window (snd_<n>_layer_blend = fade in from, full from, full to, faded out at; metres from
	// the camera, read when a 3D sound starts): distant and far layers that take over from the close one
	float m_blend[4] = {0.f, 0.f, flt_max, flt_max};
	bool m_has_blend = false;
	float blend_weight(float dist) const;

	HUD_SOUND_ITEM* FindSoundItem(LPCSTR alias, bool b_assert); //AVO: made public to check if sound is loaded
	void PlaySound(LPCSTR alias, const Fvector& position, const CObject* parent, bool hud_mode, bool looped = false,
	               u8 index = u8(-1), float volume_mult = 1.f, float frequency = 1.f, float delay_mult = 1.f);

	void StopSound(LPCSTR alias, float fade = 0.f);

	void LoadSound(LPCSTR section, LPCSTR line, LPCSTR alias, bool exclusive = false, int type = sg_SourceType);

	void SetPosition(LPCSTR alias, const Fvector& pos);
	void StopAllSounds();
};

//Alundaio:
class HUD_SOUND_COLLECTION_LAYERED
{
	xr_vector<HUD_SOUND_COLLECTION> m_sound_items;
public:
	~HUD_SOUND_COLLECTION_LAYERED();
	HUD_SOUND_ITEM* FindSoundItem(LPCSTR alias, bool b_assert);
	// returns the alias that played (a script's on_before_play_hud_sound may swap in another sound)
	shared_str PlaySound(LPCSTR alias, const Fvector& position, const CObject* parent, bool hud_mode, bool looped = false,
	               u8 index = u8(-1), float volume_mult = 1.f, float frequency = 1.f, float delay_mult = 1.f);
	void StopSound(LPCSTR alias, float fade = 0.f);
	void StopAllSounds();
	void LoadSound(LPCSTR section, LPCSTR line, LPCSTR alias, bool exclusive = false, int type = sg_SourceType);
	void SetTracked(LPCSTR alias, bool tracked = true); // every layer's item of the alias (HUD_SOUND_ITEM::m_b_tracked)
	void LoadSound(CInifile const* ini, LPCSTR section, LPCSTR line, LPCSTR alias, bool exclusive = false,
	               int type = sg_SourceType);
	void SetPosition(LPCSTR alias, const Fvector& pos);
	void UpdateAllSoundsPositions(const Fvector& P);
	bool IsPlaying(LPCSTR alias); // any layer of it still sounding (2D ones too, unlike HUD_SOUND_ITEM::playing)
};
//-Alundaio
