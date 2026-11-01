////////////////////////////////////////////////////////////////////////////
//	Module 		: scope_timers.h
//	Description : Named scope timers: how long the game spends in chosen pieces of code, written to the log
////////////////////////////////////////////////////////////////////////////
//
// Console "scope_timers 1" starts collecting; every scope_timers_period seconds (default 5) the log gets one line
// per scope that ran, the costliest first: milliseconds per frame, calls per frame, the average and the longest
// call in microseconds, and its share of the frame. "scope_timers 0" stops.
//
// C++: SCOPE_TIMER("name"); at the top of a block times the rest of that block. Times include nested scopes.
// Lua: scope_timers.enabled(), scope_timers.add(name, microseconds) (time with profile_timer()).
// Off, a scope costs one int test and nothing is stored.
#pragma once

extern int g_scope_timers;        // scope_timers 0/1
extern int g_scope_timers_period; // scope_timers_period 1..60 (seconds between reports)

namespace scope_timers
{
	// A scope's id, made once per name (the macro keeps it in a static).
	u32 id(LPCSTR name);
	void add(u32 id, u64 ticks);
	void add_script(LPCSTR name, float microseconds);
	bool enabled();
	// Once per frame (CLevel::OnFrame): counts the frame and writes the report when the period is over.
	void frame();

	class CScope
	{
		u32 m_id;
		u64 m_start;

	public:
		explicit CScope(u32 id) : m_id(id), m_start(g_scope_timers ? CPU::QPC() : 0) {}
		~CScope()
		{
			if (m_start)
				add(m_id, CPU::QPC() - m_start);
		}
	};
}

#define SCOPE_TIMER_CAT2(a, b) a##b
#define SCOPE_TIMER_CAT(a, b) SCOPE_TIMER_CAT2(a, b)
#define SCOPE_TIMER(name) \
	static const u32 SCOPE_TIMER_CAT(scope_timer_id_, __LINE__) = scope_timers::id(name); \
	scope_timers::CScope SCOPE_TIMER_CAT(scope_timer_, __LINE__)(SCOPE_TIMER_CAT(scope_timer_id_, __LINE__))
