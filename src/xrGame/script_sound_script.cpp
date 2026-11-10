////////////////////////////////////////////////////////////////////////////
//	Module 		: script_sound_script.cpp
//	Created 	: 06.02.2004
//  Modified 	: 06.02.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script sound class script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "script_sound.h"
#include "script_game_object.h"
#include "HudSound.h"

using namespace luabind;

// Gunsmith (Tarkov SFX, P9): a layered sound set for scripts, as explosions and others' shots have
// (HUD_SOUND_COLLECTION_LAYERED): sound_layers(section [, type]) loads the section's snd_1_layer, snd_2_layer, ...
// ("file, volume, delay"; each may vary as snd_1_layer1, ...) and their snd_<n>_layer_blend distance windows ("from,
// full, full_to, out" metres from the camera), and the section <section>_indoor as its indoor twin when it exists.
//   play_at_pos(obj, pos [, volume [, indoor [, frequency]]]): every layer in its window, the outdoor and indoor sets
//     crossfaded by indoor (0..1, equal power); obj may be nil (it owns the sound: AI hearing, script callbacks)
//   play(obj [, volume]): 2D (no distance windows; the outdoor set)
//   stop(), playing(), has_indoor(): stop and playing only see an exclusive set's sounds
//     (sound_layers(section, type, true): each layer one voice, a new play restarts it; the default lets plays
//     overlap, each on a voice of its own that can't be stopped, as blasts and others' shots play)
class CScriptSoundLayers
{
	HUD_SOUND_COLLECTION_LAYERED m_sounds;
	bool m_indoor = false;

	static const CObject* owner(CScriptGameObject* obj) { return obj ? &obj->object() : nullptr; }

public:
	CScriptSoundLayers(LPCSTR section) { load(section, sg_SourceType, false); }
	CScriptSoundLayers(LPCSTR section, int type) { load(section, type, false); }
	CScriptSoundLayers(LPCSTR section, int type, bool exclusive) { load(section, type, exclusive); }

	void load(LPCSTR section, int type, bool exclusive)
	{
		R_ASSERT3(pSettings->section_exist(section), "sound_layers: no such section", section);
		m_sounds.LoadLayers(pSettings, section, "sndLayers", exclusive, type);
		string256 in;
		strconcat(sizeof(in), in, section, "_indoor");
		if (pSettings->section_exist(in))
		{
			m_sounds.LoadLayers(pSettings, in, "sndLayersIndoor", exclusive, type);
			m_indoor = true;
		}
	}

	void play_at_pos5(CScriptGameObject* obj, const Fvector& pos, float volume, float indoor, float freq)
	{
		m_sounds.PlayMixed("sndLayers", m_indoor ? indoor : 0.f, pos, owner(obj), false, volume, freq);
	}
	void play_at_pos4(CScriptGameObject* obj, const Fvector& pos, float volume, float indoor)
	{
		play_at_pos5(obj, pos, volume, indoor, 1.f);
	}
	void play_at_pos3(CScriptGameObject* obj, const Fvector& pos, float volume) { play_at_pos5(obj, pos, volume, 0.f, 1.f); }
	void play_at_pos2(CScriptGameObject* obj, const Fvector& pos) { play_at_pos5(obj, pos, 1.f, 0.f, 1.f); }
	void play2(CScriptGameObject* obj, float volume)
	{
		m_sounds.PlaySound("sndLayers", Fvector().set(0, 0, 0), owner(obj), true, false, u8(-1), volume);
	}
	void play1(CScriptGameObject* obj) { play2(obj, 1.f); }
	void stop()
	{
		m_sounds.StopSound("sndLayers");
		if (m_indoor)
			m_sounds.StopSound("sndLayersIndoor");
	}
	bool playing() { return m_sounds.IsPlaying("sndLayers") || (m_indoor && m_sounds.IsPlaying("sndLayersIndoor")); }
	bool has_indoor() const { return m_indoor; }
};

#pragma optimize("s",on)
void CScriptSound::script_register(lua_State* L)
{
	module(L)
	[
		class_<CSound_params>("sound_params")
		.def_readwrite("position", &CSound_params::position)
		.def_readwrite("volume", &CSound_params::volume)
		.def_readwrite("frequency", &CSound_params::freq)
		.def_readwrite("min_distance", &CSound_params::min_distance)
		.def_readwrite("max_distance", &CSound_params::max_distance),

		class_<CScriptSound>("sound_object")
		.enum_("sound_play_type")
		[
			value("looped", sm_Looped),
			value("s2d", sm_2D),
			value("s3d", 0)
		]
		.property("frequency", &CScriptSound::GetFrequency, &CScriptSound::SetFrequency)
		.property("min_distance", &CScriptSound::GetMinDistance, &CScriptSound::SetMinDistance)
		.property("max_distance", &CScriptSound::GetMaxDistance, &CScriptSound::SetMaxDistance)
		.property("volume", &CScriptSound::GetVolume, &CScriptSound::SetVolume)
		.property("position", &CScriptSound::GetPosition, &CScriptSound::SetPosition)
		.def(constructor<LPCSTR>())
		.def(constructor<LPCSTR, ESoundTypes>())
		.def("get_position", &CScriptSound::GetPosition)
		.def("set_position", &CScriptSound::SetPosition)
		.def("play", (void (CScriptSound::*)(CScriptGameObject*))(&CScriptSound::Play))
		.def("play", (void (CScriptSound::*)(CScriptGameObject*, float))(&CScriptSound::Play))
		.def("play", (void (CScriptSound::*)(CScriptGameObject*, float, int))(&CScriptSound::Play))
		.def("play_at_pos", (void (CScriptSound::*)(CScriptGameObject*, const Fvector&))(&CScriptSound::PlayAtPos))
		.def("play_at_pos",
		     (void (CScriptSound::*)(CScriptGameObject*, const Fvector&, float))(&CScriptSound::PlayAtPos))
		.def("play_at_pos",
		     (void (CScriptSound::*)(CScriptGameObject*, const Fvector&, float, int))(&CScriptSound::PlayAtPos))
		.def("play_no_feedback", &CScriptSound::PlayNoFeedback)
		.def("stop", &CScriptSound::Stop)
		.def("stop_deffered", &CScriptSound::StopDeffered)
		.def("playing", &CScriptSound::IsPlaying)
		.def("length", &CScriptSound::Length)
		.def("attach_tail", &CScriptSound::AttachTail),

		class_<CScriptSoundLayers>("sound_layers")
		.def(constructor<LPCSTR>())
		.def(constructor<LPCSTR, int>())
		.def(constructor<LPCSTR, int, bool>())
		.def("play_at_pos", &CScriptSoundLayers::play_at_pos2)
		.def("play_at_pos", &CScriptSoundLayers::play_at_pos3)
		.def("play_at_pos", &CScriptSoundLayers::play_at_pos4)
		.def("play_at_pos", &CScriptSoundLayers::play_at_pos5)
		.def("play", &CScriptSoundLayers::play1)
		.def("play", &CScriptSoundLayers::play2)
		.def("stop", &CScriptSoundLayers::stop)
		.def("playing", &CScriptSoundLayers::playing)
		.def("has_indoor", &CScriptSoundLayers::has_indoor)
	];
}
