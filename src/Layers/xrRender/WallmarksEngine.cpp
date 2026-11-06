// WallmarksEngine.cpp: implementation of the CWallmarksEngine class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "WallmarksEngine.h"

#include "../../xrEngine/xr_object.h"
#include "../../xrEngine/x_ray.h"
#include "../../xrEngine/GameFont.h"
#include "SkeletonCustom.h"

float wallmark_range_static = 100.f;
float wallmark_range_skeleton = 50.f;

// Decal budget and lifetime ([wallmarks] in system.ltx; see WallmarksEngine.h). Defaults = vanilla.
SWallmarkSettings g_wallmark_settings = {0, 0, 0, 1.f, 1.f, 0.f, 0.f};

void SWallmarkSettings::load()
{
	static_max = 0;
	skeleton_max = 0;
	skeleton_max_per_object = 0;
	fade_time = 1.f;
	fade_fraction = 1.f;
	static_ttl = 0.f;
	skeleton_ttl = 0.f;

	LPCSTR S = "wallmarks";
	if (!pSettings->section_exist(S))
		return;
	if (pSettings->line_exist(S, "static_max")) static_max = pSettings->r_u32(S, "static_max");
	if (pSettings->line_exist(S, "skeleton_max")) skeleton_max = pSettings->r_u32(S, "skeleton_max");
	if (pSettings->line_exist(S, "skeleton_max_per_object")) skeleton_max_per_object = pSettings->r_u32(S, "skeleton_max_per_object");
	if (pSettings->line_exist(S, "fade_time")) fade_time = pSettings->r_float(S, "fade_time");
	if (pSettings->line_exist(S, "fade_fraction")) fade_fraction = pSettings->r_float(S, "fade_fraction");
	if (pSettings->line_exist(S, "static_ttl")) static_ttl = pSettings->r_float(S, "static_ttl");
	if (pSettings->line_exist(S, "skeleton_ttl")) skeleton_ttl = pSettings->r_float(S, "skeleton_ttl");
	clamp(fade_time, 0.f, 3600.f);
	clamp(fade_fraction, 0.f, 1.f);
	Msg("* [wallmarks] static_max %u, skeleton_max %u (per object %u), fade_time %.2f, fade_fraction %.2f, static_ttl %.1f, skeleton_ttl %.1f",
		static_max, skeleton_max, skeleton_max_per_object, fade_time, fade_fraction, static_ttl, skeleton_ttl);
}

float wallmark_fade(float time_start, float ttl, float evict_start)
{
	float now = RDEVICE.fTimeGlobal;
	float f = 0.f;
	if (ttl != -1.f)
	{
		// life fade: none until the last fade_fraction of the life, then linear to 1
		// (fade_fraction 1 gives age / ttl, the vanilla value)
		float frac = g_wallmark_settings.fade_fraction;
		float age = now - time_start;
		if (frac > 0.f)
			f = (age - ttl * (1.f - frac)) / (ttl * frac);
		else
			f = age >= ttl ? 1.f : 0.f;
	}
	if (evict_start >= 0.f)
	{
		// pushed out by a cap: fade over fade_time from then on
		float t = g_wallmark_settings.fade_time;
		float e = t > 0.f ? (now - evict_start) / t : 1.f;
		f = _max(f, e);
	}
	clamp(f, 0.f, 1.f);
	return f;
}

namespace WallmarksEngine
{
	struct wm_slot
	{
		typedef CWallmarksEngine::StaticWMVec StaticWMVec;
		ref_shader shader;
		StaticWMVec static_items;
		xr_vector<intrusive_ptr<CSkeletonWallmark>> skeleton_items;

		wm_slot(ref_shader sh)
		{
			shader = sh;
			static_items.reserve(256);
			skeleton_items.reserve(256);
		}
	};
}

// #include "xr_effsun.h"

const float W_DIST_FADE = 15.f;
const float W_DIST_FADE_SQR = W_DIST_FADE * W_DIST_FADE;
const float I_DIST_FADE_SQR = 1.f / W_DIST_FADE_SQR;
const int MAX_TRIS = 1024 * 16;

IC bool operator ==(const CWallmarksEngine::wm_slot* slot, const ref_shader& shader) { return slot->shader == shader; }

CWallmarksEngine::wm_slot* CWallmarksEngine::FindSlot(ref_shader shader)
{
	WMSlotVecIt it = std::find(marks.begin(), marks.end(), shader);
	return (it != marks.end()) ? *it : 0;
}

CWallmarksEngine::wm_slot* CWallmarksEngine::AppendSlot(ref_shader shader)
{
	marks.push_back(xr_new<wm_slot>(shader));
	return marks.back();
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CWallmarksEngine::CWallmarksEngine()
#ifdef PROFILE_CRITICAL_SECTIONS
	:lock(MUTEX_PROFILE_ID(CWallmarksEngine))
#endif // PROFILE_CRITICAL_SECTIONS
{
	static_pool.reserve(256);
	marks.reserve(256);
	hGeom.create(FVF::F_LIT, RCache.Vertex.Buffer(), NULL);
	g_wallmark_settings.load();
}

CWallmarksEngine::~CWallmarksEngine()
{
	clear();
	hGeom.destroy();
}

void CWallmarksEngine::clear()
{
	{
		for (WMSlotVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
		{
			for (StaticWMVecIt m_it = (*p_it)->static_items.begin(); m_it != (*p_it)->static_items.end(); m_it++)
				static_wm_destroy(*m_it);
			xr_delete(*p_it);
		}
		marks.clear();
	}
	{
		for (u32 it = 0; it < static_pool.size(); it++)
			xr_delete(static_pool[it]);
		static_pool.clear();
	}
	skeleton_budget.clear();
}

// allocate
CWallmarksEngine::static_wallmark* CWallmarksEngine::static_wm_allocate()
{
	static_wallmark* W = 0;
	if (static_pool.empty()) W = xr_new<static_wallmark>();
	else
	{
		W = static_pool.back();
		static_pool.pop_back();
	}

	W->m_fTimeStart = RDEVICE.fTimeGlobal;
	W->m_fTimeEnd = g_wallmark_settings.static_ttl > 0.f ? g_wallmark_settings.static_ttl : ps_r__WallmarkTTL * 15.f;
	W->m_fEvictStart = -1.f;
	W->verts.clear();
	return W;
}

// destroy
void CWallmarksEngine::static_wm_destroy(CWallmarksEngine::static_wallmark* W)
{
	static_pool.push_back(W);
}

// render
void CWallmarksEngine::static_wm_render(CWallmarksEngine::static_wallmark* W, FVF::LIT* & V)
{
	float a = W->Fade();
	int aC = iFloor(a * 255.f);
	clamp(aC, 0, 255);
	u32 C = color_rgba(128, 128, 128, aC);
	FVF::LIT* S = &*W->verts.begin();
	FVF::LIT* E = &*W->verts.end();
	for (; S != E; S++, V++)
	{
		V->p.set(S->p);
		V->color = C;
		V->t.set(S->t);
	}
}

//--------------------------------------------------------------------------------
void CWallmarksEngine::RecurseTri(u32 t, Fmatrix& mView, CWallmarksEngine::static_wallmark& W)
{
	CDB::TRI* T = sml_collector.getT() + t;
	if (T->dummy) return;
	T->dummy = 0xffffffff;

	// Some vars
	u32* v_ids = T->verts;
	Fvector* v_data = sml_collector.getV();
	sml_poly_src.clear();
	sml_poly_src.push_back(v_data[v_ids[0]]);
	sml_poly_src.push_back(v_data[v_ids[1]]);
	sml_poly_src.push_back(v_data[v_ids[2]]);
	sml_poly_dest.clear();

	sPoly* P = sml_clipper.ClipPoly(sml_poly_src, sml_poly_dest);

	//. todo
	// uv_gen = mView * []
	// UV = pos*uv_gen

	if (P)
	{
		// Create vertices and triangulate poly (tri-fan style triangulation)
		FVF::LIT V0, V1, V2;
		Fvector UV;

		mView.transform_tiny(UV, (*P)[0]);
		V0.set((*P)[0], 0, (1 + UV.x) * .5f, (1 - UV.y) * .5f);
		mView.transform_tiny(UV, (*P)[1]);
		V1.set((*P)[1], 0, (1 + UV.x) * .5f, (1 - UV.y) * .5f);

		for (u32 i = 2; i < P->size(); i++)
		{
			mView.transform_tiny(UV, (*P)[i]);
			V2.set((*P)[i], 0, (1 + UV.x) * .5f, (1 - UV.y) * .5f);
			W.verts.push_back(V0);
			W.verts.push_back(V1);
			W.verts.push_back(V2);
			V1 = V2;
		}

		// recurse
		for (u32 i = 0; i < 3; i++)
		{
			u32 adj = sml_adjacency[3 * t + i];
			if (0xffffffff == adj) continue;
			CDB::TRI* SML = sml_collector.getT() + adj;
			v_ids = SML->verts;

			Fvector test_normal;
			test_normal.mknormal(v_data[v_ids[0]], v_data[v_ids[1]], v_data[v_ids[2]]);
			float cosa = test_normal.dotproduct(sml_normal);
			if (cosa < 0.034899f) continue; // cos(88)
			RecurseTri(adj, mView, W);
		}
	}
}

void CWallmarksEngine::BuildMatrix(Fmatrix& mView, float invsz, const Fvector& from)
{
	// build projection
	Fmatrix mScale;
	Fvector at, up, right, y;
	at.sub(from, sml_normal);
	y.set(0, 1, 0);
	if (_abs(sml_normal.y) > .99f) y.set(1, 0, 0);
	right.crossproduct(y, sml_normal);
	up.crossproduct(sml_normal, right);
	mView.build_camera(from, at, up);
	mScale.scale(invsz, invsz, invsz);
	mView.mulA_43(mScale);
}

// demonized: add user defined rotation to wallmark
void CWallmarksEngine::AddWallmark_internal(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
                                            ref_shader hShader, float sz, float ttl, float rotation)
{
	// query for polygons in bounding box
	// calculate adjacency
	{
		Fbox bb_query;
		Fvector bbc, bbd;
		bb_query.set(contact_point, contact_point);
		bb_query.grow(sz * 2.5f);
		bb_query.get_CD(bbc, bbd);
		xrc.box_options(CDB::OPT_FULL_TEST);
		xrc.box_query(g_pGameLevel->ObjectSpace.GetStaticModel(), bbc, bbd);
		u32 triCount = xrc.r_count();
		if (0 == triCount)
			return;

		CDB::TRI* tris = g_pGameLevel->ObjectSpace.GetStaticTris();
		sml_collector.clear();
		sml_collector.add_face_packed_D(pVerts[pTri->verts[0]], pVerts[pTri->verts[1]], pVerts[pTri->verts[2]], 0);
		for (u32 t = 0; t < triCount; t++)
		{
			CDB::TRI* T = tris + xrc.r_begin()[t].id;
			if (T == pTri) continue;
			sml_collector.add_face_packed_D(pVerts[T->verts[0]], pVerts[T->verts[1]], pVerts[T->verts[2]], 0);
		}
		sml_collector.calc_adjacency(sml_adjacency);
	}

	// calc face normal
	Fvector N;
	N.mknormal(pVerts[pTri->verts[0]], pVerts[pTri->verts[1]], pVerts[pTri->verts[2]]);
	sml_normal.set(N);

	// build 3D ortho-frustum
	Fmatrix mView, mRot;
	BuildMatrix(mView, 1 / sz, contact_point);
	mRot.rotateZ(deg2rad(rotation));
	mView.mulA_43(mRot);
	sml_clipper.CreateFromMatrix(mView,FRUSTUM_P_LRTB);

	// create wallmark
	static_wallmark* W = static_wm_allocate();
	if (ttl) W->m_fTimeEnd = ttl;
	RecurseTri(0, mView, *W);

	// calc sphere
	if (W->verts.size() < 3)
	{
		static_wm_destroy(W);
		return;
	}
	else
	{
		Fbox bb;
		bb.invalidate();

		FVF::LIT* I = &*W->verts.begin();
		FVF::LIT* E = &*W->verts.end();
		for (; I != E; I++) bb.modify(I->p);
		bb.getsphere(W->bounds.P, W->bounds.R);
	}

	//	if (W->bounds.R < 1.f)	
	{
		// search if similar wallmark exists
		wm_slot* slot = FindSlot(hShader);
		if (slot)
		{
			StaticWMVecIt it = slot->static_items.begin();
			StaticWMVecIt end = slot->static_items.end();
			for (; it != end; it++)
			{
				static_wallmark* wm = *it;
				if (wm->bounds.P.similar(W->bounds.P, 0.02f))
				{
					// replace
					static_wm_destroy(wm);
					*it = W;
					return;
				}
			}
		}
		else
		{
			slot = AppendSlot(hShader);
		}

		// no similar - register _new_
		slot->static_items.push_back(W);
		if (g_wallmark_settings.static_max)
			static_budget();
	}
	//else
	//{
	//	static_wm_destroy(W);
	//}
}

void CWallmarksEngine::AddWallmark_internal(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
	ref_shader hShader, float sz, float ttl, bool random_rotation)
{
	AddWallmark_internal(pTri, pVerts, contact_point, hShader, sz, ttl, random_rotation ? ::Random.randF(-20.f, 20.f) : 0.f);
}

void CWallmarksEngine::AddStaticWallmark(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
                                         ref_shader hShader, float sz, float ttl, bool ignore_opt, bool random_rotation)
{
	AddStaticWallmark(pTri, pVerts, contact_point, hShader, sz, ttl, ignore_opt, random_rotation ? ::Random.randF(-20.f, 20.f) : 0.f);
}

void CWallmarksEngine::AddStaticWallmark(CDB::TRI* pTri, const Fvector* pVerts, const Fvector& contact_point,
	ref_shader hShader, float sz, float ttl, bool ignore_opt, float rotation)
{
	// optimization cheat: don't allow wallmarks more than 100 m from viewer/actor
	if (!ignore_opt && contact_point.distance_to_sqr(Device.vCameraPosition) > _sqr(wallmark_range_static))
		return;

	// Physics may add wallmarks in parallel with rendering
	lock.Enter();
	AddWallmark_internal(pTri, pVerts, contact_point, hShader, sz, ttl, rotation);
	lock.Leave();
}

void CWallmarksEngine::AddSkeletonWallmark(const Fmatrix* xf, CKinematics* obj, ref_shader& sh, const Fvector& start,
                                           const Fvector& dir, float size, float ttl, bool ignore_opt)
{
	if (::RImplementation.phase != CRender::PHASE_NORMAL) return;
	// optimization cheat: don't allow wallmarks more than 50 m from viewer/actor
	if (!ignore_opt && xf->c.distance_to_sqr(Device.vCameraPosition) > _sqr(wallmark_range_skeleton)) return;

	VERIFY(obj&&xf&&(size>EPS_L));
	// a visual that isn't kinematic (no bones) can't take a skeleton mark: skip it instead of crashing
	if (!obj || !xf) return;
	lock.Enter();
	intrusive_ptr<CSkeletonWallmark> wm = obj->AddWallmark(xf, start, dir, sh, size, ttl);
	if (wm && g_wallmark_settings.skeleton_max)
		skeleton_budget_add(wm);
	lock.Leave();
}

// Static cap: while more than static_max finite marks are not yet fading, the oldest starts to fade out
// (it dies after fade_time). One pass over all marks per new mark; cheap next to rendering them.
void CWallmarksEngine::static_budget()
{
	float now = RDEVICE.fTimeGlobal;
	for (;;)
	{
		u32 live = 0;
		static_wallmark* oldest = NULL;
		for (WMSlotVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
		{
			StaticWMVec& items = (*slot_it)->static_items;
			for (StaticWMVecIt it = items.begin(); it != items.end(); it++)
			{
				static_wallmark* W = *it;
				if (W->m_fEvictStart >= 0.f || W->TimeEnd() == -1.f) continue;
				live++;
				if (!oldest || W->TimeStart() < oldest->TimeStart()) oldest = W;
			}
		}
		if (live <= g_wallmark_settings.static_max || !oldest)
			return;
		oldest->m_fEvictStart = now;
	}
}

// Skeleton cap over all objects: marks are listed in the order they were made; dead ones (their object dropped
// them, or they faded out) leave the list; while more than skeleton_max are not yet fading, the oldest fades.
void CWallmarksEngine::skeleton_budget_add(intrusive_ptr<CSkeletonWallmark> wm)
{
	// prune: only this list still holds it (object gone or mark removed), or fully faded
	u32 n = 0;
	for (u32 i = 0; i < skeleton_budget.size(); i++)
	{
		CSkeletonWallmark* W = &*skeleton_budget[i];
		if (W->intrusive_ref_count() <= 1) continue;
		if (wallmark_fade(W->TimeStart(), W->TimeEnd(), W->EvictStart()) >= 1.f) continue;
		if (n != i) skeleton_budget[n] = skeleton_budget[i];
		n++;
	}
	skeleton_budget.resize(n);
	skeleton_budget.push_back(wm);

	u32 live = 0;
	for (u32 i = 0; i < skeleton_budget.size(); i++)
		if (!skeleton_budget[i]->Evicted() && skeleton_budget[i]->TimeEnd() != -1.f)
			live++;
	float now = RDEVICE.fTimeGlobal;
	for (u32 i = 0; i < skeleton_budget.size() && live > g_wallmark_settings.skeleton_max; i++)
	{
		CSkeletonWallmark* W = &*skeleton_budget[i];
		if (W->Evicted() || W->TimeEnd() == -1.f) continue;
		W->Evict(now);
		live--;
	}
}

void CWallmarksEngine::AddSkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm)
{
	if (::RImplementation.phase != CRender::PHASE_NORMAL) return;

	if (!::RImplementation.val_bHUD)
	{
		lock.Enter();
		// search if similar wallmark exists
		wm_slot* slot = FindSlot(wm->Shader());
		if (0 == slot) slot = AppendSlot(wm->Shader());
		// no similar - register _new_
		slot->skeleton_items.push_back(wm);
#ifdef	DEBUG
		wm->used_in_render	= Device.dwFrame;
#endif
		lock.Leave();
	}
}

extern float r_ssaDISCARD;
ICF void BeginStream(ref_geom hGeom, u32& w_offset, FVF::LIT*& w_verts, FVF::LIT*& w_start)
{
	w_offset = 0;
	w_verts = (FVF::LIT*)RCache.Vertex.Lock(MAX_TRIS * 3, hGeom->vb_stride, w_offset);
	w_start = w_verts;
}

ICF void FlushStream(ref_geom hGeom, ref_shader shader, u32& w_offset, FVF::LIT*& w_verts, FVF::LIT*& w_start,
                     BOOL bSuppressCull)
{
	u32 w_count = u32(w_verts - w_start);
	RCache.Vertex.Unlock(w_count, hGeom->vb_stride);
	if (w_count)
	{
		RCache.set_Shader(shader);
		RCache.set_Geometry(hGeom);
		if (bSuppressCull) RCache.set_CullMode(CULL_NONE);
		RCache.Render(D3DPT_TRIANGLELIST, w_offset, w_count / 3);
		if (bSuppressCull) RCache.set_CullMode(CULL_CCW);
		Device.Statistic->RenderDUMP_WMT_Count += w_count / 3;
	}
}

void CWallmarksEngine::Render()
{
	//	if (marks.empty())			return;
	// Projection and xform
	float _43 = Device.mProject._43;
	Device.mProject._43 -= ps_r__WallmarkSHIFT;
	RCache.set_xform_world(Fidentity);
	RCache.set_xform_project(Device.mProject);

	Fmatrix mSavedView = Device.mView;
	Fvector mViewPos;
	mViewPos.mad(Device.vCameraPosition, Device.vCameraDirection, ps_r__WallmarkSHIFT_V);
	Device.mView.build_camera_dir(mViewPos, Device.vCameraDirection, Device.vCameraTop);
	RCache.set_xform_view(Device.mView);

	Device.Statistic->RenderDUMP_WM.Begin();
	Device.Statistic->RenderDUMP_WMS_Count = 0;
	Device.Statistic->RenderDUMP_WMD_Count = 0;
	Device.Statistic->RenderDUMP_WMT_Count = 0;

	float ssaCLIP = r_ssaDISCARD / 4;

	lock.Enter(); // Physics may add wallmarks in parallel with rendering

	for (WMSlotVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
	{
		u32 w_offset;
		FVF::LIT *w_verts, *w_start;
		BeginStream(hGeom, w_offset, w_verts, w_start);
		wm_slot* slot = *slot_it;
		// static wallmarks
		for (StaticWMVecIt w_it = slot->static_items.begin(); w_it != slot->static_items.end();)
		{
			static_wallmark* W = *w_it;
			if (RImplementation.ViewBase.testSphere_dirty(W->bounds.P, W->bounds.R))
			{
				Device.Statistic->RenderDUMP_WMS_Count++;
				float dst = Device.vCameraPosition.distance_to_sqr(W->bounds.P);
				float ssa = W->bounds.R * W->bounds.R / dst;
				if (ssa >= ssaCLIP)
				{
					u32 w_count = u32(w_verts - w_start);
					if ((w_count + W->verts.size()) >= (MAX_TRIS * 3))
					{
						FlushStream(hGeom, slot->shader, w_offset, w_verts, w_start,FALSE);
						BeginStream(hGeom, w_offset, w_verts, w_start);
					}
					static_wm_render(W, w_verts);
				}
			}

			// don't need to check wallmarks with infinite lifetime (unless a cap pushed them out)
			if (W->TimeEnd() == -1.f && W->m_fEvictStart < 0.f)
			{
				w_it++;
				continue;
			}

			float w = W->Fade();
			if (w < 1.f)
			{
				w_it++;
			}
			else
			{
				static_wm_destroy(W);
				*w_it = slot->static_items.back();
				slot->static_items.pop_back();
			}
		}
		// Flush stream
		FlushStream(hGeom, slot->shader, w_offset, w_verts, w_start,FALSE); //. remove line if !(suppress cull needed)
		BeginStream(hGeom, w_offset, w_verts, w_start);

		// dynamic wallmarks
		for (xr_vector<intrusive_ptr<CSkeletonWallmark>>::iterator w_it = slot->skeleton_items.begin(); w_it != slot
		                                                                                                        ->
		                                                                                                        skeleton_items
		                                                                                                        .end();
		     w_it++)
		{
			intrusive_ptr<CSkeletonWallmark> W = *w_it;
			if (!W)
			{
				continue ;
			}

#ifdef DEBUG
			if(W->used_in_render != Device.dwFrame)			
			{
				Log("W->used_in_render",W->used_in_render);
				Log("Device.dwFrame",Device.dwFrame);
				VERIFY(W->used_in_render == Device.dwFrame);
			}
#endif

			Device.Statistic->RenderDUMP_WMD_Count++;
			u32 w_count = u32(w_verts - w_start);
			if ((w_count + W->VCount()) >= (MAX_TRIS * 3))
			{
				FlushStream(hGeom, slot->shader, w_offset, w_verts, w_start, TRUE);
				BeginStream(hGeom, w_offset, w_verts, w_start);
			}

			FVF::LIT* w_save = w_verts;
			try
			{
				W->Parent()->RenderWallmark(W, w_verts);
			}
			catch (...)
			{
				Msg("! Failed to render dynamic wallmark");
				w_verts = w_save;
			}

#ifdef	DEBUG
			 W->used_in_render	= u32(-1);
#endif
		}
		slot->skeleton_items.clear();
		// Flush stream
		FlushStream(hGeom, slot->shader, w_offset, w_verts, w_start,TRUE);
	}

	lock.Leave(); // Physics may add wallmarks in parallel with rendering

	// Level-wmarks
	RImplementation.r_dsgraph_render_wmarks();
	Device.Statistic->RenderDUMP_WM.End();

	// Projection
	Device.mView = mSavedView;
	Device.mProject._43 = _43;
	RCache.set_xform_view(Device.mView);
	RCache.set_xform_project(Device.mProject);
}
