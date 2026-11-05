#include "stdafx.h"
#pragma hdrstop

#include "SoundRender_Core.h"
#include "SoundRender_Emitter.h"
#include "SoundRender_Source.h"

void CSoundRender_Emitter::start(ref_sound* _owner, BOOL _loop, float delay)
{
	PROF_EVENT("CSoundRender_Emitter::start");
	starting_delay = delay;

	VERIFY(_owner);
	owner_data = _owner->_p;
	VERIFY(owner_data);
	//	source					= (CSoundRender_Source*)owner_data->handle;
	p_source.position.set(0, 0, 0);
	p_source.min_distance = source()->m_fMinDist; // DS3D_DEFAULTMINDISTANCE;
    p_source.max_distance = source()->m_fMaxDist * psSoundMaxDistanceMultiplier; // 300.f;
	p_source.base_volume = source()->m_fBaseVolume; // 1.f
	p_source.volume = 1.f; // 1.f
	set_frequency(1.f);
    p_source.max_ai_distance = source()->m_fMaxAIDist * psSoundMaxAIDistanceMultiplier; // 300.f;

	if (fis_zero(delay, EPS_L))
	{
		m_current_state = _loop ? stStartingLooped : stStarting;
	}
	else
	{
		m_current_state = _loop ? stStartingLoopedDelayed : stStartingDelayed;
		fTimeToPropagade = SoundRender->Timer.GetElapsed_sec();
	}
	bStopping = FALSE;
	fade_rate = 10.f;
	bFadeHold = FALSE;
	fade_zero_updates = 0;
	bRewind = FALSE;
}

void CSoundRender_Emitter::i_stop()
{
	bRewind = FALSE;
	if (target) SoundRender->i_stop(this);
	if (owner_data)
	{
		Event_ReleaseOwner();
		VERIFY(this==owner_data->feedback);
		owner_data->feedback = NULL;
		owner_data = NULL;
	}
	m_current_state = stStopped;
}

void CSoundRender_Emitter::stop(BOOL bDeffered)
{
	if (bDeffered) bStopping = TRUE;
	else i_stop();
}

// A deferred stop with its own fade time: the volume falls to zero over `seconds` (at least one update), the voice
// is kept while it does, gets the zero gain once (OpenAL ramps a gain change within its mix block, so the sound
// ends on silence, not mid-wave), and is released the update after (update(), update_culling())
void CSoundRender_Emitter::stop_fade(float seconds)
{
	if (m_current_state == stStopped)
		return;
	bStopping = TRUE;
	fade_rate = seconds > EPS_L ? 1.f / seconds : 1000.f;
	bFadeHold = TRUE;
	fade_zero_updates = 0;
}

void CSoundRender_Emitter::rewind()
{
	bStopping = FALSE;
	bFadeHold = FALSE;
	fade_rate = 10.f;

	float fTime = SoundRender->Timer.GetElapsed_sec();
	float fDiff = fTime - fTimeStarted;
	fTimeStarted += fDiff;
	fTimeToStop += fDiff;
	fTimeToPropagade = fTime;

	set_cursor(0);
	bRewind = TRUE;
}

void CSoundRender_Emitter::pause(BOOL bVal, int id)
{
	if (bVal)
	{
		if (0 == iPaused) iPaused = id;
	}
	else
	{
		if (id == iPaused)iPaused = 0;
	}
}

void CSoundRender_Emitter::cancel()
{
	// Msg		("- %10s : %3d[%1.4f] : %s","cancel",dbg_ID,priority(),source->fname);
	switch (m_current_state)
	{
	case stPlaying:
		// switch to: SIMULATE
		m_current_state = stSimulating; // switch state
		SoundRender->i_stop(this);
		break;
	case stPlayingLooped:
		// switch to: SIMULATE
		m_current_state = stSimulatingLooped; // switch state
		SoundRender->i_stop(this);
		break;
	default:
		FATAL("Non playing ref_sound forced out of render queue");
		break;
	}
}
