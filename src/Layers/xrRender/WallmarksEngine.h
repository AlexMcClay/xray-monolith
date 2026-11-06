// WallmarksEngine.h: interface for the CWallmarksEngine class.
//
//////////////////////////////////////////////////////////////////////
#pragma once

namespace WallmarksEngine
{
	struct wm_slot;
}

class CSkeletonWallmark;
class CKinematics;

// Decal budget and lifetime: optional [wallmarks] keys in system.ltx, read when a level loads.
// Every default keeps vanilla behaviour (no caps, fade over the whole life, vanilla lifetimes).
struct SWallmarkSettings
{
	u32 static_max; // most static (level) marks alive at once; 0 = no cap. Infinite marks (ttl -1) don't count
	u32 skeleton_max; // most marks on skinned / kinematic objects at once (all objects together); 0 = no cap
	u32 skeleton_max_per_object; // most marks on one object; 0 = no cap
	float fade_time; // seconds a mark pushed out by a cap takes to fade away (0 = removed at once)
	float fade_fraction; // last part of the life over which a mark fades (1 = vanilla: fades over its whole life)
	float static_ttl; // lifetime of static marks placed without their own ttl (0 = vanilla: r__wallmark_ttl x 15)
	float skeleton_ttl; // lifetime of skeleton marks placed without their own ttl (0 = vanilla: r__wallmark_ttl)

	void load();
};
extern SWallmarkSettings g_wallmark_settings;

// How far a mark has faded, 0 (full) to 1 (gone): the life fade (last fade_fraction of ttl; ttl -1 = never)
// and, once a cap pushed it out (evict_start >= 0), the eviction fade over fade_time, whichever is further.
// With the default settings this is age / ttl, exactly the vanilla value shaders get in the vertex alpha.
float wallmark_fade(float time_start, float ttl, float evict_start);

class CWallmarksEngine
{
public:
	typedef WallmarksEngine::wm_slot wm_slot;

public:
	struct static_wallmark
	{
		Fsphere bounds;
		xr_vector<FVF::LIT> verts;
		float m_fTimeStart;

		float m_fTimeEnd;
		float m_fEvictStart; // time a cap pushed this mark out (it then fades over fade_time), -1 = not pushed out

		IC float TimeStart() { return m_fTimeStart; }
		IC float TimeEnd() { return m_fTimeEnd; }
		IC float Fade() { return wallmark_fade(m_fTimeStart, m_fTimeEnd, m_fEvictStart); }
	};

	DEFINE_VECTOR(static_wallmark*, StaticWMVec, StaticWMVecIt);
	DEFINE_VECTOR(wm_slot*, WMSlotVec, WMSlotVecIt);
private:
	StaticWMVec static_pool;
	WMSlotVec marks;
	ref_geom hGeom;

	Fvector sml_normal;
	CFrustum sml_clipper;
	sPoly sml_poly_dest;
	sPoly sml_poly_src;

	xrXRC xrc;
	CDB::Collector sml_collector;
	xr_vector<u32> sml_adjacency;

	xrCriticalSection lock;

	// every live skeleton mark in the order they were made, kept only while [wallmarks] skeleton_max is set
	xr_vector<intrusive_ptr<CSkeletonWallmark>> skeleton_budget;
private:
	wm_slot* FindSlot(ref_shader shader);
	wm_slot* AppendSlot(ref_shader shader);

	// budget: push out the oldest marks over the caps (they fade, then die)
	void static_budget();
	void skeleton_budget_add(intrusive_ptr<CSkeletonWallmark> wm);
private:
	void BuildMatrix(Fmatrix& dest, float invsz, const Fvector& from);
	void RecurseTri(u32 T, Fmatrix& mView, static_wallmark& W);
	void AddWallmark_internal(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point, ref_shader hTexture, float sz, float ttl = 0.f, bool random_rotation = true);
	void AddWallmark_internal(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point, ref_shader hTexture, float sz, float ttl, float rotation);

	static_wallmark* static_wm_allocate();
	void static_wm_render(static_wallmark* W, FVF::LIT* & V);
	void static_wm_destroy(static_wallmark* W);

	void skeleton_wm_render(intrusive_ptr<CSkeletonWallmark>, FVF::LIT* & V);
public:
	CWallmarksEngine();
	~CWallmarksEngine();
	// edit wallmarks
	void AddStaticWallmark(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point, ref_shader hTexture, float sz, float ttl = 0.f, bool ignore_opt = false, bool random_rotation = true);

	// demonized: add user defined rotation to wallmark
	void AddStaticWallmark(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point, ref_shader hTexture, float sz, float ttl, bool ignore_opt, float rotation);

	void AddSkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm);
	void AddSkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh, const Fvector& start,
	                         const Fvector& dir, float size, float ttl = 0.f, bool ignore_opt = false);

	// render
	void Render();

	void clear();
};
