////////////////////////////////////////////////////////////////////////////
//	Module 		: scope_timers.cpp
//	Description : Named scope timers (see scope_timers.h)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "scope_timers.h"

int g_scope_timers = 0;
int g_scope_timers_period = 5;

namespace scope_timers
{
	namespace
	{
		struct SScope
		{
			shared_str name;
			u64 total = 0;
			u64 longest = 0;
			u32 calls = 0;
		};

		xrCriticalSection s_lock; // scopes may run on render or worker threads
		xr_vector<SScope> s_scopes;
		bool s_running = false;
		u64 s_start = 0;   // QPC at the start of the period
		u32 s_frames = 0;

		u32 find_or_add(LPCSTR name)
		{
			shared_str n(name);
			for (u32 i = 0; i < s_scopes.size(); ++i)
				if (s_scopes[i].name == n)
					return i;
			s_scopes.push_back(SScope());
			s_scopes.back().name = n;
			return u32(s_scopes.size() - 1);
		}

		void reset()
		{
			for (SScope& s : s_scopes)
				s.total = s.longest = 0, s.calls = 0;
			s_frames = 0;
			s_start = CPU::QPC();
		}

		double to_us(u64 ticks) { return double(ticks) * 1000000.0 / double(CPU::qpc_freq); }

		void report()
		{
			const double period_us = to_us(CPU::QPC() - s_start);
			const double frames = double(_max(s_frames, 1u));
			const double frame_us = period_us / frames;
			Msg("[scope_timers] %.1f s, %u frames, %.2f ms per frame (%.0f fps)", period_us / 1000000.0, s_frames,
			    frame_us / 1000.0, frame_us > 0.0 ? 1000000.0 / frame_us : 0.0);
			xr_vector<const SScope*> order;
			for (const SScope& s : s_scopes)
				if (s.calls)
					order.push_back(&s);
			std::sort(order.begin(), order.end(), [](const SScope* a, const SScope* b) { return a->total > b->total; });
			for (const SScope* s : order)
			{
				const double total_us = to_us(s->total);
				Msg("[scope_timers]   %-32s %8.3f ms/frame %7.2f calls/frame  avg %8.1f us  max %8.1f us  %5.2f%%",
				    s->name.c_str(), total_us / frames / 1000.0, double(s->calls) / frames, total_us / double(s->calls),
				    to_us(s->longest), frame_us > 0.0 ? 100.0 * total_us / frames / frame_us : 0.0);
			}
			if (order.empty())
				Msg("[scope_timers]   (no scope ran)");
		}
	}

	u32 id(LPCSTR name)
	{
		xrCriticalSectionGuard guard(s_lock);
		return find_or_add(name);
	}

	void add(u32 id, u64 ticks)
	{
		if (!g_scope_timers)
			return;
		xrCriticalSectionGuard guard(s_lock);
		if (id >= s_scopes.size())
			return;
		SScope& s = s_scopes[id];
		s.total += ticks;
		s.longest = _max(s.longest, ticks);
		++s.calls;
	}

	void add_script(LPCSTR name, float microseconds)
	{
		if (!g_scope_timers || !name || !*name || microseconds < 0.f)
			return;
		const u64 ticks = u64(double(microseconds) * double(CPU::qpc_freq) / 1000000.0);
		xrCriticalSectionGuard guard(s_lock);
		SScope& s = s_scopes[find_or_add(name)];
		s.total += ticks;
		s.longest = _max(s.longest, ticks);
		++s.calls;
	}

	bool enabled() { return g_scope_timers != 0; }

	void frame()
	{
		if (!g_scope_timers)
		{
			s_running = false;
			return;
		}
		xrCriticalSectionGuard guard(s_lock);
		if (!s_running)
		{
			s_running = true;
			reset(); // a fresh period from the frame it was turned on
			return;
		}
		++s_frames;
		if (to_us(CPU::QPC() - s_start) >= double(_max(g_scope_timers_period, 1)) * 1000000.0)
		{
			report();
			reset();
		}
	}
}
