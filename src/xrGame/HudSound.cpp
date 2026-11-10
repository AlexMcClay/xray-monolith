#include "stdafx.h"

#include "HudSound.h"

#include "pch_script.h"
#include "script_game_object.h"


float psHUDSoundVolume = 1.0f;
float psHUDStepSoundVolume = 1.0f;
int psHUDSoundVolumeColumn = 1; // snd_hud_volume_column: non-exclusive sounds keep their line's volume (see PlaySound)

void InitHudSoundSettings()
{
	psHUDSoundVolume = pSettings->r_float("hud_sound", "hud_sound_vol_k");
	psHUDStepSoundVolume = pSettings->r_float("hud_sound", "hud_step_sound_vol_k");
}

void HUD_SOUND_ITEM::LoadSound(LPCSTR section, LPCSTR line,
                               HUD_SOUND_ITEM& hud_snd, int type)
{
	hud_snd.m_activeSnd = NULL;
	hud_snd.sounds.clear();

	string256 sound_line;
	xr_strcpy(sound_line, line);
	int k = 0;
	while (pSettings->line_exist(section, sound_line))
	{
		hud_snd.sounds.push_back(SSnd());
		SSnd& s = hud_snd.sounds.back();

		LoadSound(section, sound_line, s.snd, type, &s.volume, &s.delay);
		xr_sprintf(sound_line, "%s%d", line, ++k);
	} //while
}

void HUD_SOUND_ITEM::LoadSound(LPCSTR section,
                               LPCSTR line,
                               ref_sound& snd,
                               int type,
                               float* volume,
                               float* delay)
{
	LPCSTR str = pSettings->r_string(section, line);
	string256 buf_str;

	int count = _GetItemCount(str);
	R_ASSERT(count);

	_GetItem(str, 0, buf_str);
	snd.create(buf_str, st_Effect, type);


	if (volume != NULL)
	{
		*volume = 1.f;
		if (count > 1)
		{
			_GetItem(str, 1, buf_str);
			if (xr_strlen(buf_str) > 0)
				*volume = (float)atof(buf_str);
		}
	}

	if (delay != NULL)
	{
		*delay = 0;
		if (count > 2)
		{
			_GetItem(str, 2, buf_str);
			if (xr_strlen(buf_str) > 0)
				*delay = (float)atof(buf_str);
		}
	}
}

void HUD_SOUND_ITEM::DestroySound(HUD_SOUND_ITEM& hud_snd)
{
	xr_vector<SSnd>::iterator it = hud_snd.sounds.begin();
	for (; it != hud_snd.sounds.end(); ++it)
		(*it).snd.destroy();
	hud_snd.sounds.clear();

	hud_snd.m_activeSnd = NULL;
}

void HUD_SOUND_ITEM::PlaySound(HUD_SOUND_ITEM& hud_snd,
                               const Fvector& position,
                               const CObject* parent,
                               bool b_hud_mode,
                               bool looped,
                               u8 index,
							   float volume_mult,
							   float frequency,
							   float delay_mult)
{
	PROF_EVENT("HUD_SOUND_ITEM::PlaySound");
	if (hud_snd.sounds.empty()) return;

	hud_snd.m_activeSnd = NULL;
	StopSound(hud_snd);

	u32 flags = b_hud_mode ? sm_2D : 0;
	if (looped)
		flags |= sm_Looped;

	//Alundaio: Sanity, don't allow PlaySound of index greater then the size, just play last index
	if (index == u8(-1))
	{
		index = (u8)Random.randI(hud_snd.sounds.size());
	}
	else
	{
		if (index >= (u8)hud_snd.sounds.size())
			index = (u8)hud_snd.sounds.size() - 1;
	}
	//-Alundaio

	hud_snd.m_activeSnd = &hud_snd.sounds[index];

	const float delay = hud_snd.m_activeSnd->delay * delay_mult;
	hud_snd.m_play_volume = hud_snd.m_activeSnd->volume * (b_hud_mode ? psHUDSoundVolume : 1.0f) * volume_mult;
	// A non-exclusive sound plays on a voice of its own (play_no_feedback), so the set_volume below never reaches
	// it: give it the whole volume here, the line's column and hud_sound_vol_k included (snd_hud_volume_column 0:
	// only volume_mult, as before 11 Nov 2026)
	float nf_volume = psHUDSoundVolumeColumn ? hud_snd.m_play_volume : volume_mult;

	if (hud_snd.m_b_exclusive || hud_snd.m_b_tracked)
	{
		hud_snd.m_activeSnd->snd.play_at_pos(const_cast<CObject*>(parent),
		                                     flags & sm_2D ? Fvector().set(0, 0, 0) : position,
		                                     flags,
		                                     delay);
	}
	else
	{
		hud_snd.m_activeSnd->snd.play_no_feedback(const_cast<CObject*>(parent),
		                                          flags,
		                                          delay,
		                                          flags & sm_2D
			                                          ? &Fvector().set(0, 0, 0)
			                                          : &Fvector().set(position.x, position.y, position.z),
		                                          &nf_volume,
		                                          frequency != 1.f ? &frequency : 0,
		                                          0);
	}

	hud_snd.m_activeSnd->snd.set_volume(hud_snd.m_play_volume);

	if ((hud_snd.m_b_exclusive || hud_snd.m_b_tracked) && frequency != 1.f)
		hud_snd.m_activeSnd->snd.set_frequency(frequency);
}

void HUD_SOUND_ITEM::StopSound(HUD_SOUND_ITEM& hud_snd, float fade)
{
	xr_vector<SSnd>::iterator it = hud_snd.sounds.begin();
	for (; it != hud_snd.sounds.end(); ++it)
	{
		if (fade > 0.f)
			(*it).snd.stop_fade(fade);
		else
			(*it).snd.stop();
	}
	hud_snd.m_activeSnd = NULL;
}

//----------------------------------------------------------
HUD_SOUND_COLLECTION::~HUD_SOUND_COLLECTION()
{
	xr_vector<HUD_SOUND_ITEM>::iterator it = m_sound_items.begin();
	xr_vector<HUD_SOUND_ITEM>::iterator it_e = m_sound_items.end();

	for (; it != it_e; ++it)
	{
		HUD_SOUND_ITEM::StopSound(*it);
		HUD_SOUND_ITEM::DestroySound(*it);
	}

	m_sound_items.clear();
}

HUD_SOUND_ITEM* HUD_SOUND_COLLECTION::FindSoundItem(LPCSTR alias, bool b_assert)
{
	xr_vector<HUD_SOUND_ITEM>::iterator it = std::find(m_sound_items.begin(), m_sound_items.end(), alias);

	if (it != m_sound_items.end())
		return &*it;
	else
	{
		R_ASSERT3(!b_assert, "sound item not found in collection", alias);
		return NULL;
	}
}

void HUD_SOUND_COLLECTION::PlaySound(LPCSTR alias,
                                     const Fvector& position,
                                     const CObject* parent,
                                     bool hud_mode,
                                     bool looped,
                                     u8 index,
									 float volume_mult,
									 float frequency,
									 float delay_mult)
{
	xr_vector<HUD_SOUND_ITEM>::iterator it = m_sound_items.begin();
	xr_vector<HUD_SOUND_ITEM>::iterator it_e = m_sound_items.end();
	for (; it != it_e; ++it)
	{
		if (it->m_b_exclusive)
			HUD_SOUND_ITEM::StopSound(*it);
	}

	HUD_SOUND_ITEM* snd_item = FindSoundItem(alias, false);
	if (snd_item)
		HUD_SOUND_ITEM::PlaySound(*snd_item, position, parent, hud_mode, looped, index, volume_mult, frequency, delay_mult);
}

void HUD_SOUND_COLLECTION::StopSound(LPCSTR alias, float fade)
{
	HUD_SOUND_ITEM* snd_item = FindSoundItem(alias, true);
	HUD_SOUND_ITEM::StopSound(*snd_item, fade);
}

void HUD_SOUND_COLLECTION::SetPosition(LPCSTR alias, const Fvector& pos)
{
	HUD_SOUND_ITEM* snd_item = FindSoundItem(alias, false);
	if (snd_item && snd_item->playing())
		snd_item->set_position(pos);
}

void HUD_SOUND_COLLECTION::StopAllSounds()
{
	xr_vector<HUD_SOUND_ITEM>::iterator it = m_sound_items.begin();
	xr_vector<HUD_SOUND_ITEM>::iterator it_e = m_sound_items.end();

	for (; it != it_e; ++it)
	{
		HUD_SOUND_ITEM::StopSound(*it);
	}
}

void HUD_SOUND_COLLECTION::LoadSound(LPCSTR section, LPCSTR line, LPCSTR alias, bool exclusive, int type)
{
	R_ASSERT(NULL==FindSoundItem(alias, false));
	m_sound_items.resize(m_sound_items.size() + 1);
	HUD_SOUND_ITEM& snd_item = m_sound_items.back();
	HUD_SOUND_ITEM::LoadSound(section, line, snd_item, type);
	snd_item.m_alias = alias;
	snd_item.m_b_exclusive = exclusive;
	snd_item.m_type = type;
}

//Alundaio:
/*
It's usage is to play a group of sounds HUD_SOUND_ITEMs as if they were a single layered entity. This is a achieved by
wrapping the class around HUD_SOUND_COLLECTION and tagging them with the same alias. This way, when one for example
sndShot is played, it will play all the sound items with the same alias.
*/
//----------------------------------------------------------
HUD_SOUND_COLLECTION_LAYERED::~HUD_SOUND_COLLECTION_LAYERED()
{
	xr_vector<HUD_SOUND_COLLECTION>::iterator it = m_sound_items.begin();
	xr_vector<HUD_SOUND_COLLECTION>::iterator it_e = m_sound_items.end();

	for (; it != it_e; ++it)
	{
		it->~HUD_SOUND_COLLECTION();
	}

	m_sound_items.clear();
}

void HUD_SOUND_COLLECTION_LAYERED::StopAllSounds()
{
	xr_vector<HUD_SOUND_COLLECTION>::iterator it = m_sound_items.begin();
	xr_vector<HUD_SOUND_COLLECTION>::iterator it_e = m_sound_items.end();

	for (; it != it_e; ++it)
	{
		it->StopAllSounds();
	}
}

void HUD_SOUND_COLLECTION_LAYERED::StopSound(LPCSTR alias, float fade)
{
	xr_vector<HUD_SOUND_COLLECTION>::iterator it = m_sound_items.begin();
	xr_vector<HUD_SOUND_COLLECTION>::iterator it_e = m_sound_items.end();

	for (; it != it_e; ++it)
	{
		if (it->m_alias == alias)
			it->StopSound(alias, fade);
	}
}

void HUD_SOUND_COLLECTION_LAYERED::SetTracked(LPCSTR alias, bool tracked)
{
	for (auto& c : m_sound_items)
	{
		if (!(c.m_alias == alias))
			continue;
		if (HUD_SOUND_ITEM* item = c.FindSoundItem(alias, false))
			item->m_b_tracked = tracked;
	}
}

void HUD_SOUND_COLLECTION_LAYERED::SetVolumeMult(LPCSTR alias, float k)
{
	for (auto& c : m_sound_items)
	{
		if (!(c.m_alias == alias))
			continue;
		// (every variant with a live voice, not m_activeSnd: set_position forgets that for a 2D sound every frame,
		// so the player's own first-person loops were never reached)
		for (auto& i : c.m_sound_items)
			for (auto& v : i.sounds)
				if (v.snd._feedback())
					v.snd.set_volume(i.m_play_volume * k);
	}
}

void HUD_SOUND_COLLECTION_LAYERED::SetPosition(LPCSTR alias, const Fvector& pos)
{
	xr_vector<HUD_SOUND_COLLECTION>::iterator it = m_sound_items.begin();
	xr_vector<HUD_SOUND_COLLECTION>::iterator it_e = m_sound_items.end();

	for (; it != it_e; ++it)
	{
		if (it->m_alias == alias)
			it->SetPosition(alias, pos);
	}
}

void HUD_SOUND_COLLECTION_LAYERED::UpdateAllSoundsPositions(const Fvector& P)
{
	for (auto& i : m_sound_items)
	{
		for (auto& s : i.m_sound_items)
		{
			s.set_position(P);
		}
	}
}

shared_str HUD_SOUND_COLLECTION_LAYERED::PlaySound(LPCSTR alias, const Fvector& position, const CObject* parent,
                                             bool hud_mode, bool looped, u8 index, float volume_mult, float frequency,
                                             float delay_mult)
{
	PROF_EVENT("HUD_SOUND_COLLECTION_LAYERED::PlaySound");
	LPCSTR alias_to_play = alias;
	xr_string new_alias; // (outlives the override block: alias_to_play may point into it)
	::luabind::functor<::luabind::object> funct;
	if (ai().script_engine().functor("_G.COnBeforePlayHudSound", funct))
	{
		const CGameObject* parent_game_object = smart_cast<const CGameObject*>(parent);
		if (parent_game_object)
		{
			auto parent_lua_game_object = parent_game_object->lua_game_object();
			::luabind::object output = funct(alias, parent_lua_game_object);
			if (output && output.type() == LUA_TTABLE)
			{
				auto volume_mult_ex_obj = output["volume_mult"];
				float volume_mult_ex = volume_mult_ex_obj.type() != LUA_TNUMBER ? 1 : ::luabind::object_cast<float>(volume_mult_ex_obj);
				volume_mult = volume_mult * volume_mult_ex;

				auto frequency_obj = output["frequency_mult"];
				if (frequency_obj.type() == LUA_TNUMBER)
				{
					float frequency_ex = ::luabind::object_cast<float>(frequency_obj);
					if (_valid(frequency_ex) && frequency_ex > EPS_S)
						frequency = frequency * frequency_ex;
					else
						Msg("!_G.COnBeforePlayHudSound callback, HUD_SOUND_COLLECTION_LAYERED::PlaySound, ignoring invalid frequency_mult for sound item %s", alias);
				}

				auto delay_mult_obj = output["delay_mult"];
				if (delay_mult_obj.type() == LUA_TNUMBER)
				{
					float delay_mult_ex = ::luabind::object_cast<float>(delay_mult_obj);
					if (_valid(delay_mult_ex) && delay_mult_ex >= 0.f)
						delay_mult = delay_mult * delay_mult_ex;
					else
						Msg("!_G.COnBeforePlayHudSound callback, HUD_SOUND_COLLECTION_LAYERED::PlaySound, ignoring invalid delay_mult for sound item %s", alias);
				}

				LPCSTR section = ::luabind::object_cast<LPCSTR>(output["section"]);
				LPCSTR line = ::luabind::object_cast<LPCSTR>(output["line"]);
				if (!section)
				{
					if (line) {
						Msg("!_G.COnBeforePlayHudSound callback, HUD_SOUND_COLLECTION_LAYERED::PlaySound, failed to override sound item %s, no section specified", alias);
					}
				}
				else if (!line)
				{
					if (section) {
						Msg("!_G.COnBeforePlayHudSound callback, HUD_SOUND_COLLECTION_LAYERED::PlaySound, failed to override sound item %s, no line specified", alias);
					}
				}
				else
				{
					new_alias = xr_string(section) + "|" + line;
					if (!FindSoundItem(new_alias.c_str(), false))
					{
						auto old_sound = FindSoundItem(alias, false);
						if (old_sound)
						{
							alias_to_play = new_alias.c_str();
							LoadSound(section, line, alias_to_play, old_sound->m_b_exclusive, old_sound->m_type);
							// (a tracked sound's stand-in is tracked too: a firing loop swapped for its headset twin can
							// still be stopped, checked and crossfaded)
							if (old_sound->m_b_tracked)
								SetTracked(alias_to_play);

							// Crash if override sound was defined but wasn't loaded correctly
							R_ASSERT2(FindSoundItem(alias_to_play, false), make_string("!_G.COnBeforePlayHudSound callback, HUD_SOUND_COLLECTION_LAYERED::PlaySound, failed to override sound item %s with %s, failed to load section %s line %s", alias, alias_to_play, section, line));
						}
						else
						{
							Msg("!_G.COnBeforePlayHudSound callback, HUD_SOUND_COLLECTION_LAYERED::PlaySound, failed to override sound item %s with %s, sound item by original alias %s not found", alias, new_alias.c_str(), alias);
						}
					}
					else
					{
						alias_to_play = new_alias.c_str();
						//Msg("HUD_SOUND_COLLECTION_LAYERED::PlaySound, overriding sound item %s with %s, already exists", alias, alias_to_play);
					}
				}
			}
		}
	}

	xr_vector<HUD_SOUND_COLLECTION>::iterator it = m_sound_items.begin();
	xr_vector<HUD_SOUND_COLLECTION>::iterator it_e = m_sound_items.end();

	const float dist = Device.vCameraPosition.distance_to(position);
	for (; it != it_e; ++it)
	{
		if (!(it->m_alias == alias_to_play))
			continue;
		const float w = (it->m_has_blend && !hud_mode) ? it->blend_weight(dist) : 1.f;
		if (volume_mult * w > EPS_S)
			it->PlaySound(alias_to_play, position, parent, hud_mode, looped, index, volume_mult * w, frequency, delay_mult);
	}
	return shared_str(alias_to_play);
}


float HUD_SOUND_COLLECTION::blend_weight(float dist) const
{
	if (dist < m_blend[0] || dist >= m_blend[3])
		return 0.f;
	if (dist < m_blend[1])
		return (dist - m_blend[0]) / _max(m_blend[1] - m_blend[0], EPS_L);
	if (dist <= m_blend[2])
		return 1.f;
	return (m_blend[3] - dist) / _max(m_blend[3] - m_blend[2], EPS_L);
}

bool HUD_SOUND_COLLECTION_LAYERED::IsPlaying(LPCSTR alias)
{
	for (auto& c : m_sound_items)
	{
		if (!(c.m_alias == alias))
			continue;
		for (auto& i : c.m_sound_items)
			for (auto& s : i.sounds)
				if (s.snd._feedback())
					return true;
	}
	return false;
}

HUD_SOUND_ITEM* HUD_SOUND_COLLECTION_LAYERED::FindSoundItem(LPCSTR alias, bool b_assert)
{
	xr_vector<HUD_SOUND_COLLECTION>::iterator it = m_sound_items.begin();
	xr_vector<HUD_SOUND_COLLECTION>::iterator it_e = m_sound_items.end();

	for (; it != it_e; ++it)
	{
		if (it->m_alias == alias)
			return it->FindSoundItem(alias, b_assert);
	}
	return (0);
}

void HUD_SOUND_COLLECTION_LAYERED::LoadSound(LPCSTR section, LPCSTR line, LPCSTR alias, bool exclusive, int type)
{
	LoadSound(pSettings, section, line, alias, exclusive, type);
}

void HUD_SOUND_COLLECTION_LAYERED::LoadSound(CInifile const* ini, LPCSTR section, LPCSTR line, LPCSTR alias,
                                             bool exclusive, int type)
{
	if (!ini->line_exist(section, line))
		return;

	LPCSTR str = ini->r_string(section, line);
	string256 buf_str;

	int count = _GetItemCount(str);
	R_ASSERT(count);

	_GetItem(str, 0, buf_str);

	if (ini->section_exist(buf_str))
	{
		LoadLayers(ini, buf_str, alias, exclusive, type);
	}
	else //For compatibility with normal HUD_SOUND_COLLECTION sounds
	{
		m_sound_items.resize(m_sound_items.size() + 1);
		HUD_SOUND_COLLECTION& snd_item = m_sound_items.back();
		snd_item.LoadSound(section, line, alias, exclusive, type);
		snd_item.m_alias = alias;
	}
}

void HUD_SOUND_COLLECTION_LAYERED::LoadLayers(CInifile const* ini, LPCSTR layers_section, LPCSTR alias, bool exclusive,
                                              int type)
{
	string256 sound_line;
	xr_strcpy(sound_line, "snd_1_layer");
	int k = 1;
	while (ini->line_exist(layers_section, sound_line))
	{
		m_sound_items.resize(m_sound_items.size() + 1);
		HUD_SOUND_COLLECTION& snd_item = m_sound_items.back();
		snd_item.LoadSound(layers_section, sound_line, alias, exclusive, type);
		snd_item.m_alias = alias;
		string256 blend_line;
		xr_sprintf(blend_line, "%s_blend", sound_line);
		if (ini->line_exist(layers_section, blend_line))
		{
			LPCSTR b = ini->r_string(layers_section, blend_line);
			string64 v;
			for (int i = 0; i < 4 && i < _GetItemCount(b); ++i)
				snd_item.m_blend[i] = (float)atof(_GetItem(b, i, v));
			snd_item.m_has_blend = true;
		}
		xr_sprintf(sound_line, "snd_%d_layer", ++k);
	}
}

void HUD_SOUND_COLLECTION_LAYERED::PlayMixed(LPCSTR alias, float indoor, const Fvector& position, const CObject* parent,
                                             bool hud_mode, float volume_mult, float frequency)
{
	string128 in;
	strconcat(sizeof(in), in, alias, "Indoor");
	if (!FindSoundItem(in, false))
		indoor = 0.f;
	indoor = _min(_max(indoor, 0.f), 1.f);
	const float floor = 0.0005f; // as the shots' crossfade (WeaponMagazined.cpp SHOT_MIX_FLOOR)
	if (indoor < 1.f)
	{
		const float g = indoor > 0.f ? _max(_cos(indoor * PI_DIV_2), floor) : 1.f;
		PlaySound(alias, position, parent, hud_mode, false, u8(-1), volume_mult * g, frequency);
	}
	if (indoor > 0.f)
	{
		const float g = indoor < 1.f ? _max(_sin(indoor * PI_DIV_2), floor) : 1.f;
		PlaySound(in, position, parent, hud_mode, false, u8(-1), volume_mult * g, frequency);
	}
}

float HUD_SOUND_COLLECTION_LAYERED::IndoorMix(const Fvector& pos, const CObject* obj)
{
	::luabind::functor<::luabind::object> funct;
	if (!ai().script_engine().functor("_G.CSound_IndoorMix", funct))
		return 0.f;
	const CGameObject* go = smart_cast<const CGameObject*>(obj);
	::luabind::object r = go ? funct(pos, go->lua_game_object()) : funct(pos);
	if (r.is_valid() && r.type() == LUA_TNUMBER)
		return _min(_max(::luabind::object_cast<float>(r), 0.f), 1.f);
	if (r.is_valid() && r.type() == LUA_TBOOLEAN)
		return ::luabind::object_cast<bool>(r) ? 1.f : 0.f;
	return 0.f;
}
//-Alundaio
