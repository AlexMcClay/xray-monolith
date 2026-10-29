////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_tactical.cpp
//	Description : Gunsmith tactical devices - weapon-mounted flashlights and lasers,
//	              any number per gun, each in its own mode (Gunsmith Docs 16)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_tactical.h"
#include "gunsmith_visuals.h"
#include "Level.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/RenderVisual.h"

namespace gunsmith
{
	namespace
	{
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

		IRenderVisual* load_beam(const shared_str& path, bool hud)
		{
			if (!path.size())
				return nullptr;
			string_path p;
			if (hud)
			{
				// <folder>\hud\<name>: a model's HUD shaders are fixed by the first load of its path
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
		type = READ_IF_EXISTS(pSettings, r_u32, section, "type", 2);
		range = READ_IF_EXISTS(pSettings, r_float, section, key_r2(section, "range", buf), 30.f);
		color = pSettings->line_exist(section, key_r2(section, "color", buf))
			? pSettings->r_fcolor(section, key_r2(section, "color", buf)) : Fcolor().set(1.f, 1.f, 1.f, 1.f);
		cone = deg2rad(READ_IF_EXISTS(pSettings, r_float, section, "spot_angle", 40.f));
		texture = READ_IF_EXISTS(pSettings, r_string, section, "spot_texture", "");
		glow_texture = READ_IF_EXISTS(pSettings, r_string, section, "glow_texture", "");
		glow_radius = READ_IF_EXISTS(pSettings, r_float, section, "glow_radius", 0.f);
		volumetric = !!READ_IF_EXISTS(pSettings, r_bool, section, "volumetric", false);
		vol_quality = READ_IF_EXISTS(pSettings, r_float, section, "volumetric_quality", 1.f);
		vol_intensity = READ_IF_EXISTS(pSettings, r_float, section, "volumetric_intensity", 0.15f);
		vol_distance = READ_IF_EXISTS(pSettings, r_float, section, "volumetric_distance", 0.4f);
		dot_range = READ_IF_EXISTS(pSettings, r_float, section, "dot_range", 0.25f);
		dot_glow_texture = READ_IF_EXISTS(pSettings, r_string, section, "dot_glow_texture", "");
		dot_glow_radius = READ_IF_EXISTS(pSettings, r_float, section, "dot_glow_radius", 0.02f);
		beam_visual = READ_IF_EXISTS(pSettings, r_string, section, "beam_visual", "");
		beam_hud_length = READ_IF_EXISTS(pSettings, r_float, section, "beam_hud_length", 4.f);
	}

	void CTacticalDevices::free_device(SDevice& d)
	{
		device_off(d);
		d.light.destroy();
		d.light_glow.destroy();
		d.dot.destroy();
		d.dot_glow.destroy();
		for (IRenderVisual*& v : d.beam)
			if (v)
				::Render->model_Delete(v);
	}

	void CTacticalDevices::clear()
	{
		for (SDevice& d : m_devices)
			free_device(d);
		m_devices.clear();
		m_sig.clear();
		m_lit = false;
	}

	void CTacticalDevices::build(SDevice& d, bool shadow)
	{
		const SPartVisual& pv = part_visual(d.section.c_str());
		bool has_light = false, has_laser = false;
		for (u8 f : pv.tac_modes)
		{
			has_light |= !!(f & SPartVisual::eTacLight);
			has_laser |= !!(f & SPartVisual::eTacLaser);
		}
		if (has_light)
		{
			SLightDesc ld;
			ld.load(pv.light_section.c_str());
			d.light = ::Render->light_create();
			d.light->set_type(IRender_Light::LT(ld.type));
			d.light->set_range(ld.range);
			d.light->set_color(ld.color);
			d.light->set_cone(ld.cone);
			if (ld.texture.size())
				d.light->set_texture(ld.texture.c_str());
			// Only one device light casts shadows: each shadowed spot costs a shadow-map pass.
			d.light->set_shadow(shadow);
			d.light->set_volumetric(ld.volumetric);
			d.light->set_volumetric_quality(ld.vol_quality);
			d.light->set_volumetric_intensity(ld.vol_intensity);
			d.light->set_volumetric_distance(ld.vol_distance);
			d.light->set_active(false);
			if (ld.glow_texture.size() && ld.glow_radius > 0.f)
			{
				d.light_glow = ::Render->glow_create();
				d.light_glow->set_texture(ld.glow_texture.c_str());
				d.light_glow->set_radius(ld.glow_radius);
				d.light_glow->set_color(ld.color);
				d.light_glow->set_active(false);
			}
		}
		if (has_laser)
		{
			SLightDesc zd;
			zd.load(pv.laser_section.c_str());
			d.dot = ::Render->light_create();
			d.dot->set_type(IRender_Light::POINT);
			d.dot->set_range(zd.dot_range);
			d.dot->set_color(zd.color);
			d.dot->set_shadow(false);
			d.dot->set_active(false);
			if (zd.dot_glow_texture.size())
			{
				d.dot_glow = ::Render->glow_create();
				d.dot_glow->set_texture(zd.dot_glow_texture.c_str());
				d.dot_glow->set_radius(zd.dot_glow_radius);
				d.dot_glow->set_color(zd.color);
				d.dot_glow->set_active(false);
			}
			d.beam[CVisuals::eWorld] = load_beam(zd.beam_visual, false);
			d.beam[CVisuals::eHud] = load_beam(zd.beam_visual, true);
		}
	}

	void CTacticalDevices::sync(const CTree& tree)
	{
		xr_vector<shared_str> sig;
		string256 buf;
		for (u8 i = 1; i < tree.size(); ++i)
		{
			const SNode& n = tree.node(i);
			if (!part_visual(n.section.c_str()).is_tactical())
				continue;
			xr_sprintf(buf, "%u:%s", u32(i), n.section.c_str());
			sig.push_back(buf);
		}
		if (sig == m_sig)
			return;
		clear();
		m_sig = sig;
		bool shadow_given = false;
		for (u8 i = 1; i < tree.size(); ++i)
		{
			const SNode& n = tree.node(i);
			const SPartVisual& pv = part_visual(n.section.c_str());
			if (!pv.is_tactical())
				continue;
			SDevice d;
			d.node = i;
			d.section = n.section;
			bool has_light = false;
			for (u8 f : pv.tac_modes)
				has_light |= !!(f & SPartVisual::eTacLight);
			build(d, has_light && !shadow_given);
			shadow_given |= has_light;
			m_devices.push_back(d);
		}
	}

	void CTacticalDevices::device_off(SDevice& d)
	{
		if (d.light)
			d.light->set_active(false);
		if (d.light_glow)
			d.light_glow->set_active(false);
		if (d.dot)
			d.dot->set_active(false);
		if (d.dot_glow)
			d.dot_glow->set_active(false);
		d.beam_ctx = -1;
	}

	void CTacticalDevices::off()
	{
		for (SDevice& d : m_devices)
			device_off(d);
		m_lit = false;
	}

	void CTacticalDevices::update(const CTree& tree, CVisuals& vis, int ctx, IKinematics* root, const Fmatrix& root_xform,
								  const Fmatrix* hud_proj, CObject* ignore)
	{
		m_lit = false;
		const bool hud = ctx == CVisuals::eHud;
		for (SDevice& d : m_devices)
		{
			const SPartVisual& pv = part_visual(d.section.c_str());
			const u8 flags = d.node < tree.size() ? pv.tac_flags(tree.node(d.node).mode) : 0;
			Fmatrix frame;
			if (!flags || !vis.part_frame(ctx, root, d.node, frame))
			{
				device_off(d);
				continue;
			}
			Fmatrix M;
			M.mul_43(root_xform, frame);
			Fvector dir, right;
			M.transform_dir(dir, Fvector().set(0.f, 0.f, -1.f)); // devices shine along the part's bore
			dir.normalize_safe();
			M.transform_dir(right, Fvector().set(1.f, 0.f, 0.f));
			right.normalize_safe();

			// Flashlight
			if (d.light && (flags & SPartVisual::eTacLight))
			{
				Fvector p;
				M.transform_tiny(p, pv.light_point);
				d.light->set_hud_mode(hud);
				d.light->set_position(p);
				d.light->set_rotation(dir, right);
				d.light->set_active(true);
				if (d.light_glow)
				{
					// glows have no HUD mode: only on the world model
					d.light_glow->set_position(p);
					d.light_glow->set_direction(dir);
					d.light_glow->set_active(!hud);
				}
				m_lit = true;
			}
			else
			{
				if (d.light)
					d.light->set_active(false);
				if (d.light_glow)
					d.light_glow->set_active(false);
			}

			// Laser: a ray into the world, a dot where it hits, and a beam from the device
			if (d.dot && (flags & SPartVisual::eTacLaser))
			{
				Fvector p;
				M.transform_tiny(p, pv.laser_point);
				Fvector wp = p, wd = dir;
				if (hud && hud_proj)
				{
					Device.hud_to_world(wp, *hud_proj);
					Device.hud_to_world_dir(wd, *hud_proj).normalize_safe();
				}
				SLightDesc zd;
				zd.range = 300.f;
				if (pv.laser_section.size() && pSettings->line_exist(pv.laser_section, "range"))
					zd.range = pSettings->r_float(pv.laser_section, "range");
				collide::rq_result R;
				float dist = zd.range;
				const bool hit = Level().ObjectSpace.RayPick(wp, wd, zd.range, collide::rqtBoth, R, ignore) && R.range > 0.f;
				if (hit)
				{
					dist = R.range;
					Fvector at;
					at.mad(wp, wd, _max(dist - 0.02f, 0.f));
					d.dot->set_hud_mode(false);
					d.dot->set_position(at);
					d.dot->set_active(true);
					if (d.dot_glow)
					{
						d.dot_glow->set_position(at);
						d.dot_glow->set_direction(Fvector().invert(wd));
						d.dot_glow->set_active(true);
					}
				}
				else
				{
					d.dot->set_active(false);
					if (d.dot_glow)
						d.dot_glow->set_active(false);
				}

				// The beam model is 1 m long along -Z: stretch it to the laser's length. In first
				// person it is drawn with the HUD and kept short (beam_hud_length): it fades out there.
				IRenderVisual* beam = d.beam[ctx];
				if (beam)
				{
					float len = dist;
					if (hud)
					{
						float hud_len = 4.f;
						if (pv.laser_section.size() && pSettings->line_exist(pv.laser_section, "beam_hud_length"))
							hud_len = pSettings->r_float(pv.laser_section, "beam_hud_length");
						len = _min(len, hud_len);
					}
					Fmatrix at = M;
					at.c = p;
					const float k = _max(M.k.magnitude(), 0.0001f); // the part's own scale along the bore
					Fmatrix S;
					S.scale(1.f, 1.f, len / k);
					d.beam_xform.mul_43(at, S);
					d.beam_ctx = ctx;
				}
			}
			else
			{
				if (d.dot)
					d.dot->set_active(false);
				if (d.dot_glow)
					d.dot_glow->set_active(false);
				d.beam_ctx = -1;
			}
		}
	}

	void CTacticalDevices::render_beams(int ctx)
	{
		for (SDevice& d : m_devices)
		{
			if (d.beam_ctx != ctx || !d.beam[ctx])
				continue;
			::Render->set_Transform(&d.beam_xform);
			::Render->add_Visual(d.beam[ctx]);
		}
	}
}
