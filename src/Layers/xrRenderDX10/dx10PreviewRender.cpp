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
#include "dx10TextureUtils.h"

namespace
{
	// The forward shader every mesh is drawn with: shaders\r3\models_gs_preview.s (Gunsmith Framework).
	LPCSTR PREVIEW_SHADER = "models\\gs_preview";
	// The supersampled draw's resolve pass, without and with the halo: shaders\r3\gs_preview_post(_halo).s.
	LPCSTR POST_SHADER = "gs_preview_post";
	LPCSTR POST_HALO_SHADER = "gs_preview_post_halo";
	LPCSTR RESOLVED_RT = "$user$gs_preview_resolved";
	const u32 SUPERSAMPLE_MAX = 4096; // a supersampled draw's largest side
	const u32 MSAA_PIXEL_BUDGET = 8u << 20; // pixels x samples of the MSAA target (32 MB of colour): fewer samples when larger

	// A rect copied to a CPU-readable texture, waiting for the GPU (PreviewReadBegin / PreviewReadEnd).
	struct SReadback
	{
		ID3DTexture2D* staging = nullptr;
		u32 w = 0, h = 0;
	};

	struct SPreview
	{
		xr_map<shared_str, ref_rt> targets;
		ref_rt scratch, scratch_zb; // each draw goes here first, then is copied into its rect
		// a supersampled draw: at twice the size with MSAA, resolved, then brought down into its rect
		ref_rt ms, ms_zb, resolved;
		ref_shader post, post_halo;
		ref_geom quad; // the resolve pass's quad (FVF::TL, clip-space positions)
		u32 samples = 0; // MSAA samples the card takes (1: none), found on first use
		u32 ms_samples = 0; // the samples `ms` was made with
		xr_map<shared_str, ref_shader> shaders; // per texture and skinning
		bool checked = false, has_shader = false;
		xr_map<u32, SReadback> readbacks;
		u32 next_ticket = 1;

		~SPreview()
		{
			for (auto& it : readbacks)
				_RELEASE(it.second.staging);
		}
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
			// the texture's bump map (its .thm / texture descriptor) as the second texture: normal and gloss
			const shared_str bump = dxRenderDeviceRender::Instance().Resources->m_textures_description.GetBumpName(texture);
			string512 textures;
			if (bump.size())
				xr_sprintf(textures, "%s,%s", texture, bump.c_str());
			else
				xr_strcpy(textures, texture);
			sh.create(PREVIEW_SHADER, textures);
			::Render->m_skinning = prev_skinning;
			::Render->hud_loading = prev_hud;
		}
		return sh;
	}

	// `world` is set again for every mesh: a rigid skinned mesh (RM_SINGLE) multiplies its bone into
	// the world matrix and leaves it there, which would move the model's later meshes.
	void draw_visual(SPreview& P, dxRender_Visual* V, const Fmatrix& world, const Fvector4& params, const Fvector4& grade)
	{
		if (!V)
			return;
		if (FHierrarhyVisual* H = dynamic_cast<FHierrarhyVisual*>(V))
		{
			for (IRenderVisual* child : H->children)
				draw_visual(P, static_cast<dxRender_Visual*>(child), world, params, grade);
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
		RCache.set_c("gs_preview_params", params);
		RCache.set_c("gs_preview_grade", grade);
		RCache.set_CullMode(CULL_CCW);
		RCache.set_xform_world(world);
		V->Render(1.f);
	}

	void ensure_rt(ref_rt& rt, LPCSTR name, u32 w, u32 h, D3DFORMAT format)
	{
		if (rt && rt->dwWidth == w && rt->dwHeight == h)
			return;
		rt = ref_rt();
		rt.create(name, w, h, format);
	}

	bool check_shader(SPreview& P)
	{
		if (!P.checked)
		{
			P.checked = true;
			P.has_shader = !!dxRenderDeviceRender::Instance().Resources->_lua_HasShader(PREVIEW_SHADER);
			if (!P.has_shader)
				Msg("! [gunsmith] preview shader [%s] not found (shaders\\r3\\models_gs_preview.s); previews are off", PREVIEW_SHADER);
		}
		return P.has_shader;
	}

	// The named target at tw x th, made (and cleared to transparent) when missing or of another size.
	ref_rt& target_rt(SPreview& P, LPCSTR target, u32 tw, u32 th)
	{
		ref_rt& rt = P.targets[target];
		if (!rt || rt->dwWidth != tw || rt->dwHeight != th)
		{
			ensure_rt(rt, target, tw, th, D3DFMT_A8R8G8B8);
			const float clear[4] = { 0.f, 0.f, 0.f, 0.f };
			HW.pContext->ClearRenderTargetView(rt->pRT, clear);
		}
		return rt;
	}

#ifdef USE_DX11
	typedef D3D11_BOX SBox;
#else
	typedef D3D10_BOX SBox;
#endif

	// The most MSAA samples (8, 4 or 2) both the colour and the depth format take, else 1.
	u32 msaa_samples()
	{
		const DXGI_FORMAT color = dx10TextureUtils::ConvertTextureFormat(D3DFMT_A8R8G8B8);
		for (u32 n : { 8u, 4u, 2u })
		{
			UINT qc = 0, qd = 0;
			if (SUCCEEDED(HW.pDevice->CheckMultisampleQualityLevels(color, n, &qc)) && qc &&
				SUCCEEDED(HW.pDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, n, &qd)) && qd)
				return n;
		}
		return 1;
	}

	void set_viewport(u32 x, u32 y, u32 w, u32 h)
	{
		D3D_VIEWPORT vp;
		vp.TopLeftX = float(x);
		vp.TopLeftY = float(y);
		vp.Width = float(w);
		vp.Height = float(h);
		vp.MinDepth = 0.f;
		vp.MaxDepth = 1.f;
		HW.pContext->RSSetViewports(1, &vp);
	}

	void draw_meshes(SPreview& P, const Fmatrix& view, const Fmatrix& proj, IRenderVisual* const* visuals,
					 const Fmatrix* xforms, u32 count, const Fvector4& params, const Fvector4& grade)
	{
		// The scene's camera is set before the frame renders (CRenderDevice::UpdateCamera); keep it.
		const Fmatrix saved_w = RCache.xforms.m_w, saved_v = RCache.xforms.m_v, saved_p = RCache.xforms.m_p;
		RCache.set_xform_view(view);
		RCache.set_xform_project(proj);
		for (u32 i = 0; i < count; ++i)
			draw_visual(P, static_cast<dxRender_Visual*>(visuals[i]), xforms[i], params, grade);
		RCache.set_xform_world(saved_w);
		RCache.set_xform_view(saved_v);
		RCache.set_xform_project(saved_p);
	}

	void rect_box(SBox& box, u32 x, u32 y, u32 w, u32 h)
	{
		box.left = x;
		box.top = y;
		box.front = 0;
		box.right = x + w;
		box.bottom = y + h;
		box.back = 1;
	}
}

bool CRender::PreviewRender(LPCSTR target, u32 tw, u32 th, u32 x, u32 y, u32 w, u32 h, const Fmatrix& view,
							const Fmatrix& proj, IRenderVisual* const* visuals, const Fmatrix* xforms, u32 count,
							u32 flags, const Fvector4* params, const Fvector4* grade)
{
	SPreview& P = preview();
	if (!check_shader(P) || !target || !w || !h || x + w > tw || y + h > th)
		return false;

	ref_rt& rt = target_rt(P, target, tw, th);
	const Fvector4 shading = params ? *params : Fvector4().set(1.f, 1.f, 1.f, 0.f);
	const Fvector4 grading = grade ? *grade : Fvector4().set(1.f, 1.f, 0.f, 0.f);
	const float clear[4] = { 0.f, 0.f, 0.f, 0.f };

	if ((flags & ePreviewSupersample) && 2 * w <= SUPERSAMPLE_MAX && 2 * h <= SUPERSAMPLE_MAX)
	{
		// Twice the size with MSAA into `ms`, resolved into `resolved`, then gs_preview_post brings each 2x2
		// block down into the rect (and draws the halo).
		if (!P.samples)
			P.samples = msaa_samples();
		const u32 sw = 2 * w, sh = 2 * h;
		const u32 mw = P.ms ? _max(P.ms->dwWidth, sw) : sw;
		const u32 mh = P.ms ? _max(P.ms->dwHeight, sh) : sh;
		u32 samples = P.samples;
		while (samples > 1 && mw * mh * samples > MSAA_PIXEL_BUDGET)
			samples /= 2;
		if (!P.ms || P.ms->dwWidth != mw || P.ms->dwHeight != mh || P.ms_samples != samples)
		{
			P.ms = ref_rt();
			P.ms_zb = ref_rt();
			P.resolved = ref_rt();
			P.ms.create("$user$gs_preview_ms", mw, mh, D3DFMT_A8R8G8B8, samples);
			P.ms_zb.create("$user$gs_preview_ms_zb", mw, mh, D3DFMT_D24S8, samples);
			P.resolved.create(RESOLVED_RT, mw, mh, D3DFMT_A8R8G8B8);
			P.ms_samples = samples;
		}
		if (!P.quad)
			P.quad.create(FVF::F_TL, RCache.Vertex.Buffer(), RCache.QuadIB);
		ref_shader& post = (flags & ePreviewHalo) ? P.post_halo : P.post;
		if (!post)
			post.create((flags & ePreviewHalo) ? POST_HALO_SHADER : POST_SHADER);
		if (P.ms && P.ms->pRT && P.ms_zb && P.ms_zb->pZRT && P.resolved && P.resolved->pRT && post && P.quad)
		{
			RCache.set_RT(P.ms->pRT);
			RCache.set_RT(nullptr, 1);
			RCache.set_RT(nullptr, 2);
			RCache.set_ZB(P.ms_zb->pZRT);
			HW.pContext->ClearRenderTargetView(P.ms->pRT, clear);
			HW.pContext->ClearDepthStencilView(P.ms_zb->pZRT, D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, 1.f, 0);
			set_viewport(0, 0, sw, sh);
			draw_meshes(P, view, proj, visuals, xforms, count, shading, grading);

			D3D_TEXTURE2D_DESC desc;
			P.resolved->pSurface->GetDesc(&desc);
			if (P.ms_samples > 1)
				HW.pContext->ResolveSubresource(P.resolved->pSurface, 0, P.ms->pSurface, 0, desc.Format);
			else
				HW.pContext->CopyResource(P.resolved->pSurface, P.ms->pSurface);

			// The rect, every pixel written: a quad in clip space over the viewport, its texture coordinates
			// over the 2w x 2h corner of `resolved`.
			RCache.set_RT(rt->pRT);
			RCache.set_ZB(nullptr);
			set_viewport(x, y, w, h);
			const float u1 = float(sw) / float(mw), v1 = float(sh) / float(mh);
			u32 offset;
			FVF::TL* pv = (FVF::TL*)RCache.Vertex.Lock(4, P.quad->vb_stride, offset);
			pv->set(-1.f, 1.f, 0.f, 1.f, 0, 0.f, 0.f);
			pv++;
			pv->set(-1.f, -1.f, 0.f, 1.f, 0, 0.f, v1);
			pv++;
			pv->set(1.f, 1.f, 0.f, 1.f, 0, u1, 0.f);
			pv++;
			pv->set(1.f, -1.f, 0.f, 1.f, 0, u1, v1);
			pv++;
			RCache.Vertex.Unlock(4, P.quad->vb_stride);
			RCache.set_Shader(post);
			RCache.set_c("gs_preview_params", shading);
			RCache.set_CullMode(CULL_NONE);
			RCache.set_Geometry(P.quad);
			RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);

			RCache.set_RT(HW.pBaseRT);
			RCache.set_ZB(HW.pBaseZB);
			set_viewport(0, 0, Device.dwWidth, Device.dwHeight);
			return true;
		}
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

	draw_meshes(P, view, proj, visuals, xforms, count, shading, grading);

	SBox box;
	rect_box(box, 0, 0, w, h);
	HW.pContext->CopySubresourceRegion(rt->pSurface, 0, x, y, 0, P.scratch->pSurface, 0, &box);

	RCache.set_RT(HW.pBaseRT);
	RCache.set_ZB(HW.pBaseZB);
	vp.Width = float(Device.dwWidth);
	vp.Height = float(Device.dwHeight);
	HW.pContext->RSSetViewports(1, &vp);
	return true;
}

u32 CRender::PreviewReadBegin(LPCSTR target, u32 x, u32 y, u32 w, u32 h)
{
	SPreview& P = preview();
	auto it = target ? P.targets.find(target) : P.targets.end();
	if (it == P.targets.end() || !it->second || !w || !h || x + w > it->second->dwWidth || y + h > it->second->dwHeight)
		return 0;

	D3D_TEXTURE2D_DESC desc;
	it->second->pSurface->GetDesc(&desc);
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D_CPU_ACCESS_READ;
	desc.MiscFlags = 0;
	SReadback rb;
	if (FAILED(HW.pDevice->CreateTexture2D(&desc, nullptr, &rb.staging)) || !rb.staging)
		return 0;
	rb.w = w;
	rb.h = h;
	SBox box;
	rect_box(box, x, y, w, h);
	HW.pContext->CopySubresourceRegion(rb.staging, 0, 0, 0, 0, it->second->pSurface, 0, &box);
	const u32 ticket = P.next_ticket++;
	if (!P.next_ticket)
		P.next_ticket = 1;
	P.readbacks[ticket] = rb;
	return ticket;
}

int CRender::PreviewReadEnd(u32 ticket, void* out)
{
	SPreview& P = preview();
	auto it = P.readbacks.find(ticket);
	if (it == P.readbacks.end())
		return -1;
	SReadback& rb = it->second;
	if (!out) // not wanted any more
	{
		_RELEASE(rb.staging);
		P.readbacks.erase(it);
		return -1;
	}
	D3D_MAPPED_TEXTURE2D mapped;
#ifdef USE_DX11
	const HRESULT hr = HW.pContext->Map(rb.staging, 0, D3D_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
#else
	const HRESULT hr = rb.staging->Map(0, D3D_MAP_READ, D3D10_MAP_FLAG_DO_NOT_WAIT, &mapped);
#endif
	if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
		return 0;
	int result = -1;
	if (SUCCEEDED(hr) && out)
	{
		const u8* src = static_cast<const u8*>(mapped.pData);
		u8* dst = static_cast<u8*>(out);
		for (u32 row = 0; row < rb.h; ++row)
			CopyMemory(dst + row * rb.w * 4, src + row * mapped.RowPitch, rb.w * 4);
		result = 1;
	}
	if (SUCCEEDED(hr))
	{
#ifdef USE_DX11
		HW.pContext->Unmap(rb.staging, 0);
#else
		rb.staging->Unmap(0);
#endif
	}
	_RELEASE(rb.staging);
	P.readbacks.erase(it);
	return result;
}

bool CRender::PreviewWriteRect(LPCSTR target, u32 tw, u32 th, u32 x, u32 y, u32 w, u32 h, const void* pixels)
{
	SPreview& P = preview();
	if (!check_shader(P) || !target || !pixels || !w || !h || x + w > tw || y + h > th)
		return false;
	ref_rt& rt = target_rt(P, target, tw, th);
	SBox box;
	rect_box(box, x, y, w, h);
	HW.pContext->UpdateSubresource(rt->pSurface, 0, &box, pixels, w * 4, 0);
	return true;
}

void CRender::PreviewRelease()
{
	xr_delete(g_preview);
}
