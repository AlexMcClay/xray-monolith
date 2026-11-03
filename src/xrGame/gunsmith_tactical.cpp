////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_tactical.cpp
//	Description : Gunsmith tactical devices - weapon-mounted flashlights and lasers,
//	              any number per gun, each in its own mode
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_tactical.h"
#include "gunsmith_visuals.h"
#include "gunsmith_config.h"
#include "Level.h"
#include "../xrCDB/xr_area.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/RenderVisual.h"

namespace gunsmith
{
	namespace
	{
		const float MAX_CONE = deg2rad(119.f); // the renderer's limit for a spot light is 120 degrees

		// Key, or its _r2 variant on the R2+ renderers (as CTorch reads them).
		LPCSTR key_r2(LPCSTR section, LPCSTR key, string128& buf)
		{
			if (::Render->get_generation() == IRender_interface::GENERATION_R2)
			{
				xr_sprintf(buf, "%s_r2", key);
				if (pSettings->line_exist(section, buf))
					return buf;
			}
			return key;
		}

		// A model (no file: null); hud: its first-person copy <folder>\hud\<name> (a model's HUD shaders are
		// fixed by the first load of its path).
		IRenderVisual* load_visual(const shared_str& path, bool hud)
		{
			if (!path.size())
				return nullptr;
			string_path p;
			if (hud)
			{
				xr_strcpy(p, path.c_str());
				LPSTR slash = strrchr(p, '\\');
				string_path name;
				xr_strcpy(name, slash ? slash + 1 : p);
				if (slash)
					slash[1] = 0;
				else
					p[0] = 0;
				xr_strcat(p, "hud\\");
				xr_strcat(p, name);
			}
			else
				xr_strcpy(p, path.c_str());
			const bool was_hud = ::Render->hud_loading;
			::Render->hud_loading = hud;
			IRenderVisual* v = ::Render->model_Create(p);
			::Render->hud_loading = was_hud;
			IKinematics* K = v ? v->dcast_PKinematics() : nullptr;
			if (K)
			{
				K->CalculateBones_Invalidate();
				K->CalculateBones(TRUE);
			}
			return v;
		}

		void free_visual(IRenderVisual*& v)
		{
			if (v)
				::Render->model_Delete(v);
			v = nullptr;
		}

		// A quad model's transform: centre `at`, its x-y plane facing `normal` (z), half-size `size`, and
		// `k` in the length of z (the shader's brightness: the flat quad doesn't use z).
		void facing_xform(Fmatrix& M, const Fvector& at, const Fvector& normal, float size, float k)
		{
			Fvector n = normal, up = Fvector().set(0.f, 1.f, 0.f), i, j;
			n.normalize_safe();
			if (_abs(n.dotproduct(up)) > 0.99f)
				up.set(1.f, 0.f, 0.f);
			i.crossproduct(up, n).normalize_safe();
			j.crossproduct(n, i);
			M.identity();
			M.i.mul(i, size);
			M.j.mul(j, size);
			M.k.mul(n, _max(k, 0.f));
			M.c = at;
		}

		// The normal of the static triangle a ray hit (null object), facing back along the ray; else the way
		// back along the ray.
		Fvector hit_normal(const collide::rq_result& R, const Fvector& dir)
		{
			Fvector n = Fvector().invert(dir);
			if (!R.O && R.element >= 0)
			{
				CDB::TRI* T = Level().ObjectSpace.GetStaticTris() + R.element;
				Fvector* V = Level().ObjectSpace.GetStaticVerts();
				Fvector e1, e2, tn;
				e1.sub(V[T->verts[1]], V[T->verts[0]]);
				e2.sub(V[T->verts[2]], V[T->verts[0]]);
				tn.crossproduct(e1, e2);
				if (tn.square_magnitude() > EPS_S)
				{
					tn.normalize();
					if (tn.dotproduct(dir) > 0.f)
						tn.invert();
					n = tn;
				}
			}
			return n;
		}

		// The lens glow: brighter and larger as the device points at the camera (Tarkov's flare fades in from
		// lens_angle_min, the cosine to the device's axis).
		void place_lens(Fmatrix& M, const SLightDesc& def, const Fvector& at, const Fvector& dir)
		{
			Fvector to_cam;
			to_cam.sub(Device.vCameraPosition, at);
			const float dist = to_cam.magnitude();
			if (dist < EPS_L)
				to_cam.invert(dir);
			else
				to_cam.div(dist);
			const float a = clampr((dir.dotproduct(to_cam) - def.lens_angle_min) / _max(1.f - def.lens_angle_min, EPS_L), 0.f, 1.f);
			Fvector c;
			c.mad(at, to_cam, 0.002f);
			facing_xform(M, c, to_cam, def.lens_size * (1.f + 1.5f * a), def.lens_intensity * (0.35f + 0.65f * a));
		}
	}

	void SLightDesc::load(LPCSTR section)
	{
		if (!section || !pSettings->section_exist(section))
		{
			Msg("! [gunsmith] tactical device light definition [%s] not found", section ? section : "");
			color.set(1.f, 1.f, 1.f, 1.f);
			return;
		}
		string128 buf;
		ir = !!READ_IF_EXISTS(pSettings, r_bool, section, "ir", false);
		type = READ_IF_EXISTS(pSettings, r_u32, section, "type", 2);
		range = READ_IF_EXISTS(pSettings, r_float, section, key_r2(section, "range", buf), 30.f);
		color = pSettings->line_exist(section, key_r2(section, "color", buf))
			? pSettings->r_fcolor(section, key_r2(section, "color", buf)) : Fcolor().set(1.f, 1.f, 1.f, 1.f);
		cone = _min(deg2rad(READ_IF_EXISTS(pSettings, r_float, section, "spot_angle", 40.f)), MAX_CONE);
		texture = READ_IF_EXISTS(pSettings, r_string, section, "spot_texture", "");
		volumetric = !!READ_IF_EXISTS(pSettings, r_bool, section, "volumetric", false);
		vol_quality = READ_IF_EXISTS(pSettings, r_float, section, "volumetric_quality", 1.f);
		vol_intensity = READ_IF_EXISTS(pSettings, r_float, section, "volumetric_intensity", 0.15f);
		vol_distance = READ_IF_EXISTS(pSettings, r_float, section, "volumetric_distance", 0.4f);

		laser_range = READ_IF_EXISTS(pSettings, r_float, section, "range", 100.f);
		beam_visual = READ_IF_EXISTS(pSettings, r_string, section, "beam_visual", "");
		dot_visual = READ_IF_EXISTS(pSettings, r_string, section, "dot_visual", "");
		beam_width = READ_IF_EXISTS(pSettings, r_float, section, "beam_width", 0.004f);
		beam_intensity = READ_IF_EXISTS(pSettings, r_float, section, "beam_intensity", 1.f);
		if (pSettings->line_exist(section, "dot_size"))
		{
			const Fvector2 v = pSettings->r_fvector2(section, "dot_size");
			dot_size_close = v.x;
			dot_size_far = v.y;
		}
		dot_view_scale = READ_IF_EXISTS(pSettings, r_float, section, "dot_view_scale", 1.5f);
		dot_intensity = READ_IF_EXISTS(pSettings, r_float, section, "dot_intensity", 1.f);
		dot_light_range = READ_IF_EXISTS(pSettings, r_float, section, "dot_light_range", 0.2f);
		dot_light_intensity = READ_IF_EXISTS(pSettings, r_float, section, "dot_light_intensity", 3.1f);
		if (pSettings->line_exist(section, "dot_light_cone"))
		{
			const Fvector2 v = pSettings->r_fvector2(section, "dot_light_cone");
			dot_cone_close = _min(deg2rad(v.x), MAX_CONE);
			dot_cone_far = _min(deg2rad(v.y), MAX_CONE);
		}

		lens_visual = READ_IF_EXISTS(pSettings, r_string, section, "lens_visual", "");
		lens_size = READ_IF_EXISTS(pSettings, r_float, section, "lens_size", 0.f);
		lens_intensity = READ_IF_EXISTS(pSettings, r_float, section, "lens_intensity", 1.f);
		lens_angle_min = READ_IF_EXISTS(pSettings, r_float, section, "lens_angle_min", 0.6f);
	}

	const SLightDesc& light_desc(const shared_str& section)
	{
		static xr_map<shared_str, SLightDesc> cache;
		auto it = cache.find(section);
		if (it != cache.end())
			return it->second;
		SLightDesc d;
		d.load(section.c_str());
		return cache.insert(mk_pair(section, d)).first->second;
	}

	void CTacticalDevices::free_device(SDevice& d)
	{
		device_off(d);
		d.light.destroy();
		d.dot.destroy();
		free_visual(d.beam);
		free_visual(d.dot_vis);
		for (auto& l : d.lens)
			for (IRenderVisual*& v : l)
				free_visual(v);
	}

	void CTacticalDevices::clear()
	{
		for (SDevice& d : m_devices)
			free_device(d);
		m_devices.clear();
		m_sig.clear();
		m_rev_valid = false;
		m_lit = false;
	}

	// The light, the laser and their models for `mode`.
	void CTacticalDevices::setup_mode(SDevice& d, u8 mode)
	{
		const SPartVisual& pv = part_visual(d.section);
		const SPartVisual::STacMode& md = pv.tac_mode(mode);
		const u8 flags = pv.tac_flags(mode);
		const SLightDesc* light = (flags & SPartVisual::eTacAnyLight) && md.light.size() ? &light_desc(md.light) : nullptr;
		const SLightDesc* laser = (flags & SPartVisual::eTacAnyLaser) && md.laser.size() ? &light_desc(md.laser) : nullptr;
		device_off(d);
		d.mode = mode;

		if (light != d.light_def)
		{
			d.light_def = light;
			free_visual(d.lens[eLensLight][CVisuals::eWorld]);
			free_visual(d.lens[eLensLight][CVisuals::eHud]);
			if (light)
			{
				if (!d.light)
					d.light = ::Render->light_create();
				d.light->set_type(IRender_Light::LT(light->type));
				d.light->set_range(0.f); // set_range ignores changes under 10 %
				d.light->set_range(light->range);
				d.light->set_color(light->color);
				d.light->set_cone(light->cone);
				d.light->set_texture(light->texture.size() ? light->texture.c_str() : nullptr);
				d.light->set_shadow(false);
				d.shadow = false;
				d.light->set_volumetric(light->volumetric);
				d.light->set_volumetric_quality(light->vol_quality);
				d.light->set_volumetric_intensity(light->vol_intensity);
				d.light->set_volumetric_distance(light->vol_distance);
				d.light->set_active(false);
				d.light_score = _max(light->color.r, _max(light->color.g, light->color.b)) * light->range * light->cone;
				d.lens[eLensLight][CVisuals::eWorld] = load_visual(light->lens_visual, false);
				d.lens[eLensLight][CVisuals::eHud] = load_visual(light->lens_visual, true);
			}
			else
				d.light.destroy();
		}

		if (laser != d.laser_def)
		{
			d.laser_def = laser;
			free_visual(d.beam);
			free_visual(d.dot_vis);
			free_visual(d.lens[eLensLaser][CVisuals::eWorld]);
			free_visual(d.lens[eLensLaser][CVisuals::eHud]);
			if (laser)
			{
				if (!d.dot)
					d.dot = ::Render->light_create();
				// Tarkov's laserBeamLight: a small spot just off the surface, pointing at the dot
				d.dot->set_type(IRender_Light::SPOT);
				d.dot->set_range(0.f);
				d.dot->set_range(laser->dot_light_range);
				d.dot->set_shadow(false);
				d.dot->set_active(false);
				d.beam = load_visual(laser->beam_visual, false);
				d.dot_vis = load_visual(laser->dot_visual, false);
				d.lens[eLensLaser][CVisuals::eWorld] = load_visual(laser->lens_visual, false);
				d.lens[eLensLaser][CVisuals::eHud] = load_visual(laser->lens_visual, true);
			}
			else
				d.dot.destroy();
		}
	}

	void CTacticalDevices::sync(const CTree& tree)
	{
		// Same layout as last time: nothing to compare.
		if (m_rev_valid && m_rev == tree.layout_revision())
			return;
		m_rev = tree.layout_revision();
		m_rev_valid = true;
		xr_vector<shared_str> sig;
		string256 buf;
		for (u8 i = 1; i < tree.size(); ++i)
		{
			const SNode& n = tree.node(i);
			if (!part_visual(n.section).is_tactical())
				continue;
			xr_sprintf(buf, "%u:%s", u32(i), n.section.c_str());
			sig.push_back(buf);
		}
		if (sig == m_sig)
			return;
		clear();
		m_sig = sig;
		m_rev = tree.layout_revision();
		m_rev_valid = true;
		for (u8 i = 1; i < tree.size(); ++i)
		{
			const SNode& n = tree.node(i);
			if (!part_visual(n.section).is_tactical())
				continue;
			SDevice d;
			d.node = i;
			d.section = n.section;
			m_devices.push_back(d);
		}
	}

	void CTacticalDevices::device_off(SDevice& d)
	{
		if (d.light)
			d.light->set_active(false);
		if (d.dot)
			d.dot->set_active(false);
		d.beam_on = d.dot_on = false;
		for (int& c : d.lens_ctx)
			c = -1;
	}

	void CTacticalDevices::off()
	{
		for (SDevice& d : m_devices)
			device_off(d);
		m_lit = false;
	}

	void CTacticalDevices::update(const CTree& tree, CVisuals& vis, int ctx, IKinematics* root, const Fmatrix& root_xform,
								  const Fmatrix* hud_proj, CObject* ignore, bool player, u32 shadows, bool lasers)
	{
		m_lit = false;
		const bool hud = ctx == CVisuals::eHud;
		const SSettings& gs = settings();
		// the lights shining this frame, to hand out the shadows
		SDevice* shining[16];
		u32 n_shining = 0;
		for (SDevice& d : m_devices)
		{
			const SPartVisual& pv = part_visual(d.section);
			const u8 mode = d.node < tree.size() ? u8(_min(u32(tree.node(d.node).mode), u32(pv.tac_modes.size() - 1))) : 0;
			Fmatrix frame;
			if (d.node >= tree.size() || !pv.is_tactical() || !vis.part_frame(ctx, root, d.node, frame))
			{
				device_off(d);
				continue;
			}
			if (d.mode != mode)
				setup_mode(d, mode);
			const SPartVisual::STacMode& md = pv.tac_mode(mode);
			Fmatrix M;
			M.mul_43(root_xform, frame);
			Fvector dir, right;
			M.transform_dir(dir, Fvector().set(0.f, 0.f, -1.f)); // devices shine along the part's bore
			dir.normalize_safe();
			M.transform_dir(right, Fvector().set(1.f, 0.f, 0.f));
			right.normalize_safe();
			for (int& c : d.lens_ctx)
				c = -1;

			// Flashlight
			const SLightDesc* L = d.light_def;
			if (L && d.light && (!L->ir || gs.ir_visible))
			{
				Fvector p;
				M.transform_tiny(p, md.light_point);
				d.light->set_hud_mode(hud);
				d.light->set_is_playerlight(player);
				// the shadow map's near plane: the holder's body and gun right behind the lens stay out of it
				d.light->set_virtual_size(gs.device_shadow_near);
				d.light->set_range(L->range * gs.device_light_range_k);
				Fcolor c = L->color;
				c.mul_rgb(gs.device_light_bright_k);
				d.light->set_color(c);
				d.light->set_position(p);
				d.light->set_rotation(dir, right);
				d.light->set_active(true);
				if (n_shining < sizeof(shining) / sizeof(shining[0]))
					shining[n_shining++] = &d;
				m_lit = true;
				if (d.lens[eLensLight][ctx] && L->lens_size > 0.f)
				{
					Fvector lp;
					M.transform_tiny(lp, md.light_lens);
					place_lens(d.lens_xform[eLensLight], *L, lp, dir);
					d.lens_ctx[eLensLight] = ctx;
				}
			}
			else if (d.light)
				d.light->set_active(false);

			// Laser: a ray into the world, a dot where it hits, a beam from the device to the dot
			const SLightDesc* Z = lasers ? d.laser_def : nullptr;
			if (Z && (!Z->ir || gs.ir_visible))
			{
				Fvector p;
				M.transform_tiny(p, md.laser_point);
				Fvector wp = p, wd = dir;
				if (hud && hud_proj)
				{
					Device.hud_to_world(wp, *hud_proj);
					Device.hud_to_world_dir(wd, *hud_proj).normalize_safe();
				}
				const float range = _max(Z->laser_range, 1.f);
				collide::rq_result R;
				const bool hit = Level().ObjectSpace.RayPick(wp, wd, range, collide::rqtBoth, R, ignore) && R.range > 0.f;
				const float dist = hit ? R.range : range;
				const float t = clampr(dist / range, 0.f, 1.f);
				const float k = 1.f - t;

				d.dot_on = false;
				if (d.dot)
					d.dot->set_active(false);
				if (hit)
				{
					Fvector at, n = hit_normal(R, wd);
					at.mad(wp, wd, dist);
					// the dot (Tarkov's LaserPoint): grows with the hit's distance and the viewer's, smaller when zoomed
					if (d.dot_vis)
					{
						const float view = _min(at.distance_to(Device.vCameraPosition), range) / range;
						const float fov = clampr((Device.fFOV + 50.f) / 125.f, 0.f, 1.f);
						const float size = (Z->dot_size_close + (Z->dot_size_far - Z->dot_size_close) * t) *
							(1.f + (Z->dot_view_scale - 1.f) * view) * fov * gs.laser_dot_size_k;
						Fvector c;
						c.mad(at, n, 0.005f);
						facing_xform(d.dot_xform, c, n, size, Z->dot_intensity * k * gs.laser_bright_k);
						d.dot_on = true;
					}
					// its light: a spot 0.1 m off the surface, wider and dimmer with distance
					if (d.dot && k > 0.f)
					{
						Fvector lp, ld;
						ld.sub(n, wd).normalize_safe();
						lp.mad(at, ld, 0.1f);
						ld.sub(at, lp).normalize_safe();
						Fvector lr;
						lr.crossproduct(Fvector().set(0.f, 1.f, 0.f), ld);
						if (lr.square_magnitude() < EPS_S)
							lr.set(1.f, 0.f, 0.f);
						lr.normalize();
						Fcolor c = Z->color;
						c.mul_rgb(Z->dot_light_intensity * k * gs.laser_bright_k);
						d.dot->set_hud_mode(false);
						d.dot->set_color(c);
						d.dot->set_cone(Z->dot_cone_close + (Z->dot_cone_far - Z->dot_cone_close) * t);
						d.dot->set_position(lp);
						d.dot->set_rotation(ld, lr);
						d.dot->set_active(true);
					}
				}

				// The beam (Tarkov's LaserBeam): a ribbon from the device to the hit, turned to face the camera. Model z
				// 0..-1 runs along it, x across; the length of y carries its brightness (gs_tac_beam.vs).
				d.beam_on = false;
				if (d.beam)
				{
					Fvector mid, to_cam, side, up;
					mid.mad(wp, wd, dist * 0.5f);
					to_cam.sub(Device.vCameraPosition, mid);
					side.crossproduct(to_cam, wd);
					if (side.square_magnitude() < EPS_S)
						side.set(right);
					side.normalize();
					up.crossproduct(wd, side).normalize_safe();
					d.beam_xform.identity();
					d.beam_xform.i.mul(side, 0.5f * Z->beam_width * gs.laser_beam_width_k);
					d.beam_xform.j.mul(up, Z->beam_intensity * gs.laser_bright_k);
					d.beam_xform.k.mul(wd, -dist);
					d.beam_xform.c = wp;
					d.beam_on = true;
				}

				if (d.lens[eLensLaser][ctx] && Z->lens_size > 0.f)
				{
					Fvector lp;
					M.transform_tiny(lp, md.laser_lens);
					place_lens(d.lens_xform[eLensLaser], *Z, lp, dir);
					d.lens_ctx[eLensLaser] = ctx;
				}
			}
			else
			{
				if (d.dot)
					d.dot->set_active(false);
				d.beam_on = d.dot_on = false;
			}
		}

		// The strongest lights get the shadows (`shadows` of them).
		std::sort(shining, shining + n_shining, [](const SDevice* a, const SDevice* b) { return a->light_score > b->light_score; });
		for (u32 i = 0; i < n_shining; ++i)
		{
			const bool shadow = i < shadows;
			if (shining[i]->shadow != shadow)
			{
				shining[i]->light->set_shadow(shadow);
				shining[i]->shadow = shadow;
			}
		}
	}

	void CTacticalDevices::render(int ctx)
	{
		for (SDevice& d : m_devices)
			for (int l = 0; l < eLensCount; ++l)
				if (d.lens_ctx[l] == ctx && d.lens[l][ctx])
				{
					::Render->set_Transform(&d.lens_xform[l]);
					::Render->add_Visual(d.lens[l][ctx]);
				}
	}

	void CTacticalDevices::render_world()
	{
		for (SDevice& d : m_devices)
		{
			if (d.beam_on && d.beam)
			{
				::Render->set_Transform(&d.beam_xform);
				::Render->add_Visual(d.beam);
			}
			if (d.dot_on && d.dot_vis)
			{
				::Render->set_Transform(&d.dot_xform);
				::Render->add_Visual(d.dot_vis);
			}
		}
	}
}
