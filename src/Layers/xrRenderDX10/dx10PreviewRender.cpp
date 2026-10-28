////////////////////////////////////////////////////////////////////////////
//	Module 		: dx10PreviewRender.cpp
//	Description : Gunsmith previews - models drawn straight into a $user$ texture
//	              that 2D UI can show (inventory icons, the workbench view)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "../xrRender/FBasicVisual.h"
#include "../xrRender/FHierrarhyVisual.h"
#include "../xrRender/ResourceManager.h"
#include "../xrRender/dxRenderDeviceRender.h"

namespace
{
	// The forward shader every mesh is drawn with: shaders\r3\models_gs_preview.s (Gunsmith Framework).
	LPCSTR PREVIEW_SHADER = "models\\gs_preview";

	struct SPreview
	{
		xr_map<shared_str, ref_rt> targets;
		ref_rt scratch, scratch_zb; // each draw goes here first, then is copied into its rect
		xr_map<shared_str, ref_shader> shaders; // per texture and skinning
		bool checked = false, has_shader = false;
	};

	SPreview* g_preview = nullptr;

	SPreview& preview()
	{
		if (!g_preview)
			g_preview = xr_new<SPreview>();
		return *g_preview;
	}

	ref_shader& mesh_shader(SPreview& P, LPCSTR texture, s32 skinning)
	{
		string512 key;
		xr_sprintf(key, "%s|%d", texture, skinning);
		ref_shader& sh = P.shaders[key];
		if (!sh)
		{
			const s32 prev_skinning = ::Render->m_skinning;
			const bool prev_hud = ::Render->hud_loading;
			::Render->m_skinning = skinning;
			::Render->hud_loading = false;
			sh.create(PREVIEW_SHADER, texture);
			::Render->m_skinning = prev_skinning;
			::Render->hud_loading = prev_hud;
		}
		return sh;
	}

	void draw_visual(SPreview& P, dxRender_Visual* V)
	{
		if (!V)
			return;
		if (FHierrarhyVisual* H = dynamic_cast<FHierrarhyVisual*>(V))
		{
			for (IRenderVisual* child : H->children)
				draw_visual(P, static_cast<dxRender_Visual*>(child));
			return;
		}
		LPCSTR shader = V->dbg_shader.size() ? V->dbg_shader.c_str() : V->dbg_shader_def.c_str();
		LPCSTR texture = V->dbg_texture.size() ? V->dbg_texture.c_str() : V->dbg_texture_def.c_str();
		if (!texture || !texture[0])
			return;
		// Collimator dots and scope lenses are screen effects, not surfaces.
		if (shader && (strstr(shader, "lfo_") || strstr(shader, "scope_")))
			return;
		RCache.set_Shader(mesh_shader(P, texture, V->skinning));
		RCache.set_CullMode(CULL_CCW);
		V->Render(1.f);
	}

	void ensure_rt(ref_rt& rt, LPCSTR name, u32 w, u32 h, D3DFORMAT format)
	{
		if (rt && rt->dwWidth == w && rt->dwHeight == h)
			return;
		rt = ref_rt();
		rt.create(name, w, h, format);
	}
}

bool CRender::PreviewRender(LPCSTR target, u32 tw, u32 th, u32 x, u32 y, u32 w, u32 h, const Fmatrix& view,
							const Fmatrix& proj, IRenderVisual* const* visuals, const Fmatrix* xforms, u32 count)
{
	SPreview& P = preview();
	if (!P.checked)
	{
		P.checked = true;
		P.has_shader = !!dxRenderDeviceRender::Instance().Resources->_lua_HasShader(PREVIEW_SHADER);
		if (!P.has_shader)
			Msg("! [gunsmith] preview shader [%s] not found (shaders\\r3\\models_gs_preview.s); previews are off", PREVIEW_SHADER);
	}
	if (!P.has_shader || !target || !w || !h || x + w > tw || y + h > th)
		return false;

	ref_rt& rt = P.targets[target];
	if (!rt || rt->dwWidth != tw || rt->dwHeight != th)
	{
		ensure_rt(rt, target, tw, th, D3DFMT_A8R8G8B8);
		const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
		HW.pContext->ClearRenderTargetView(rt->pRT, clear);
	}

	// The scratch target grows to the largest rect asked for.
	const u32 sw = P.scratch ? _max(P.scratch->dwWidth, w) : w;
	const u32 sh = P.scratch ? _max(P.scratch->dwHeight, h) : h;
	ensure_rt(P.scratch, "$user$gs_preview_scratch", sw, sh, D3DFMT_A8R8G8B8);
	ensure_rt(P.scratch_zb, "$user$gs_preview_scratch_zb", sw, sh, D3DFMT_D24S8);

	RCache.set_RT(P.scratch->pRT);
	RCache.set_RT(nullptr, 1);
	RCache.set_RT(nullptr, 2);
	RCache.set_ZB(P.scratch_zb->pZRT);
	const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
	HW.pContext->ClearRenderTargetView(P.scratch->pRT, clear);
	HW.pContext->ClearDepthStencilView(P.scratch_zb->pZRT, D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, 1.f, 0);

	D3D_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width = w;
	vp.Height = h;
	vp.MinDepth = 0.f;
	vp.MaxDepth = 1.f;
	HW.pContext->RSSetViewports(1, &vp);

	RCache.set_xform_view(view);
	RCache.set_xform_project(proj);
	for (u32 i = 0; i < count; ++i)
	{
		RCache.set_xform_world(xforms[i]);
		draw_visual(P, static_cast<dxRender_Visual*>(visuals[i]));
	}

#ifdef USE_DX11
	D3D11_BOX box;
#else
	D3D10_BOX box;
#endif
	box.left = 0;
	box.top = 0;
	box.front = 0;
	box.right = w;
	box.bottom = h;
	box.back = 1;
	HW.pContext->CopySubresourceRegion(rt->pSurface, 0, x, y, 0, P.scratch->pSurface, 0, &box);

	RCache.set_RT(HW.pBaseRT);
	RCache.set_ZB(HW.pBaseZB);
	vp.Width = float(Device.dwWidth);
	vp.Height = float(Device.dwHeight);
	HW.pContext->RSSetViewports(1, &vp);
	return true;
}

void CRender::PreviewRelease()
{
	xr_delete(g_preview);
}
