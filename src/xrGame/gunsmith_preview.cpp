////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_preview.cpp
//	Description : Gunsmith previews - weapons drawn with their part models into
//	              textures that 2D UI shows (inventory icons, the workbench view)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_preview.h"
#include "gunsmith_visuals.h"
#include "gunsmith_config.h"
#include "Weapon.h"
#include "Level.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/RenderVisual.h"

int g_gs_preview_debug = 0;

namespace gunsmith
{
	namespace
	{
		LPCSTR ICON_TEXTURE = "$user$gs_icons";
		LPCSTR VIEW_TEXTURE = "$user$gs_view";
		const u32 ICONS_PER_FRAME = 8;
		const float VIEW_FOV = deg2rad(30.f);

		CPreview* g_instance = nullptr;

		u32 tree_crc(const CTree& tree)
		{
			u32 crc = 0;
			for (u32 i = 0; i < tree.size(); ++i)
			{
				const SNode& n = tree.node(u8(i));
				crc = crc32(n.section.c_str(), n.section.size(), crc);
				const u8 link[2] = { n.parent, n.slot };
				crc = crc32(link, sizeof(link), crc);
			}
			return crc;
		}

		// A live gun's cell key: its object and its build.
		void object_key(const CWeapon* weapon, string128& key)
		{
			xr_sprintf(key, "o%u:%08x", u32(weapon->ID()), tree_crc(weapon->gs_tree()));
		}

		// The inventory cell's aspect, fitted into an icon cell.
		void icon_size(LPCSTR section, u32& w, u32& h)
		{
			const float gw = READ_IF_EXISTS(pSettings, r_float, section, "inv_grid_width", 5.f);
			const float gh = READ_IF_EXISTS(pSettings, r_float, section, "inv_grid_height", 2.f);
			const float aspect = gh > 0.f ? gw / gh : 2.5f;
			if (aspect >= float(CPreview::ICON_W) / float(CPreview::ICON_H))
			{
				w = CPreview::ICON_W;
				h = _max(u32(1), u32(float(CPreview::ICON_W) / aspect));
			}
			else
			{
				h = CPreview::ICON_H;
				w = _max(u32(1), u32(float(CPreview::ICON_H) * aspect));
			}
		}

		void bounds(const xr_vector<std::pair<IRenderVisual*, Fmatrix>>& list, Fbox& box)
		{
			box.invalidate();
			for (const auto& item : list)
			{
				Fbox b;
				b.xform(item.first->getVisData().box, item.second);
				box.merge(b);
			}
		}
	}

	// A section's default build on its own copy of the world model.
	struct CPreview::SScene
	{
		IRenderVisual* root = nullptr;
		CTree tree;
		CVisuals visuals;

		~SScene()
		{
			visuals.clear();
			if (root)
				::Render->model_Delete(root);
		}
	};

	CPreview& CPreview::instance()
	{
		if (!g_instance)
			g_instance = xr_new<CPreview>();
		return *g_instance;
	}

	void CPreview::destroy()
	{
		xr_delete(g_instance);
	}

	CPreview::CPreview()
	{
		m_cells.resize((ATLAS_W / ICON_W) * (ATLAS_H / ICON_H));
		m_view_box.invalidate();
		Device.seqRender.Add(this, REG_PRIORITY_HIGH);
	}

	CPreview::~CPreview()
	{
		Device.seqRender.Remove(this);
		for (auto& it : m_scenes)
			xr_delete(it.second);
	}

	bool CPreview::available() const
	{
		return !m_failed && ::Render->get_dx_level() >= 0x000A0000;
	}

	bool CPreview::icon_rect(u16 id, Frect& rect)
	{
		CWeapon* weapon = smart_cast<CWeapon*>(Level().Objects.net_Find(id));
		if (!weapon || !weapon->gs_full_mode())
			return false;
		string128 key;
		object_key(weapon, key);
		return icon_request(key, id, weapon->cNameSect().c_str(), rect);
	}

	bool CPreview::icon_rect(LPCSTR section, Frect& rect)
	{
		if (!section || !pSettings->section_exist(section) || !is_full_mode(section))
			return false;
		string256 key;
		xr_sprintf(key, "s%s", section);
		return icon_request(key, u16(-1), section, rect);
	}

	bool CPreview::icon_request(LPCSTR key, u16 id, LPCSTR section, Frect& rect)
	{
		if (!available())
			return false;

		int index = -1;
		for (u32 i = 0; i < m_cells.size() && index < 0; ++i)
			if (m_cells[i].key == key)
				index = int(i);

		if (index < 0)
		{
			// The same gun with a different build takes over its old cell; else a free cell,
			// else the one unused longest.
			for (u32 i = 0; i < m_cells.size() && index < 0 && id != u16(-1); ++i)
				if (m_cells[i].id == id)
					index = int(i);
			for (u32 i = 0; i < m_cells.size() && index < 0; ++i)
				if (!m_cells[i].key.size())
					index = int(i);
			if (index < 0)
			{
				index = 0;
				for (u32 i = 1; i < m_cells.size(); ++i)
					if (m_cells[i].used < m_cells[index].used)
						index = int(i);
			}
			SCell& c = m_cells[index];
			if (g_gs_preview_debug)
				Msg("- [gunsmith] preview: icon cell %d [%s] (was [%s])", index, key, c.key.size() ? c.key.c_str() : "");
			c.key = key;
			c.id = id;
			c.section = section;
			c.dirty = true;
			icon_size(section, c.w, c.h);
		}

		SCell& c = m_cells[index];
		c.used = Device.dwFrame;
		const u32 cols = ATLAS_W / ICON_W;
		const float x = float((index % cols) * ICON_W);
		const float y = float((index / cols) * ICON_H);
		rect.set(x, y, x + float(c.w), y + float(c.h));
		return true;
	}

	void CPreview::view_object(u16 id)
	{
		m_view_id = id;
		m_view_section = nullptr;
	}

	void CPreview::view_section(LPCSTR section)
	{
		m_view_id = u16(-1);
		m_view_section = section;
	}

	void CPreview::view_clear()
	{
		m_view_id = u16(-1);
		m_view_section = nullptr;
		m_view_points.clear();
	}

	void CPreview::view_camera(float yaw_deg, float pitch_deg, float zoom, float pan_x, float pan_y, float aspect)
	{
		m_aspect = clampr(aspect, 0.25f, 8.f);
		m_yaw = yaw_deg;
		m_pitch = clampr(pitch_deg, -80.f, 80.f);
		m_zoom = clampr(zoom, 0.2f, 10.f);
		m_pan_x = pan_x;
		m_pan_y = pan_y;
	}

	void CPreview::view_tick()
	{
		m_view_tick = Device.dwFrame;
	}

	CPreview::SScene* CPreview::scene(LPCSTR section)
	{
		auto it = m_scenes.find(section);
		if (it != m_scenes.end())
			return it->second;

		SScene* s = nullptr;
		LPCSTR visual = READ_IF_EXISTS(pSettings, r_string, section, "visual", (LPCSTR)nullptr);
		IRenderVisual* root = visual ? ::Render->model_Create(visual) : nullptr;
		IKinematics* K = root ? root->dcast_PKinematics() : nullptr;
		if (K)
		{
			s = xr_new<SScene>();
			s->root = root;
			s->tree.build_default(section, 0, 1.f);
			s->visuals.ensure(CVisuals::eWorld, K, s->tree, section, nullptr, is_full_mode(section));
		}
		else
		{
			if (root)
				::Render->model_Delete(root);
			Msg("! [gunsmith] preview: [%s] has no skeletal visual", section);
		}
		m_scenes[section] = s;
		return s;
	}

	// The models to draw and their transforms in the receiver model's space, receiver first.
	bool CPreview::draw_list(u16 id, LPCSTR section, DRAW_LIST& out)
	{
		out.clear();
		IRenderVisual* root = nullptr;
		CVisuals* visuals = nullptr;
		if (id != u16(-1))
		{
			CWeapon* weapon = smart_cast<CWeapon*>(Level().Objects.net_Find(id));
			if (weapon && weapon->gs_full_mode() && weapon->Visual())
			{
				root = weapon->Visual();
				visuals = &weapon->gs_visuals();
				if (IKinematics* K = root->dcast_PKinematics())
					visuals->ensure(CVisuals::eWorld, K, weapon->gs_tree(), weapon->cNameSect().c_str(), nullptr, true);
			}
		}
		if (!root && section && section[0])
		{
			if (SScene* s = scene(section))
			{
				root = s->root;
				visuals = &s->visuals;
			}
		}
		IKinematics* K = root ? root->dcast_PKinematics() : nullptr;
		if (!K)
			return false;

		K->CalculateBones_Invalidate();
		K->CalculateBones(TRUE);
		out.push_back(std::make_pair(root, Fidentity));
		visuals->draw_list(CVisuals::eWorld, K, out);
		return true;
	}

	void CPreview::draw_icon(u32 index)
	{
		SCell& c = m_cells[index];
		c.dirty = false;
		DRAW_LIST list;
		if (!draw_list(c.id, c.section.c_str(), list))
			return;

		Fbox box;
		bounds(list, box);
		Fvector centre, size;
		box.getcenter(centre);
		box.getsize(size);

		// Orthographic, from the gun's right (+X), up +Y: the muzzle (+Z) points right.
		const float aspect = float(c.w) / float(c.h);
		const float width = _max(size.z, size.y * aspect) * 1.04f;
		const float height = width / aspect;
		Fvector from, dir, up;
		dir.set(-1.f, 0.f, 0.f);
		up.set(0.f, 1.f, 0.f);
		from.mad(centre, dir, -(size.x * 0.5f + 1.f));
		Fmatrix view, proj;
		view.build_camera_dir(from, dir, up);
		proj.build_projection_ortho(width, height, 0.05f, size.x + 2.f);

		xr_vector<IRenderVisual*> visuals;
		xr_vector<Fmatrix> xforms;
		for (const auto& item : list)
		{
			visuals.push_back(item.first);
			xforms.push_back(item.second);
		}
		const u32 cols = ATLAS_W / ICON_W;
		const u32 x = (index % cols) * ICON_W;
		const u32 y = (index / cols) * ICON_H;
		if (!::Render->PreviewRender(ICON_TEXTURE, ATLAS_W, ATLAS_H, x, y, c.w, c.h, view, proj, visuals.data(),
		                             xforms.data(), u32(visuals.size())))
		{
			m_failed = true;
			return;
		}
		if (g_gs_preview_debug)
			Msg("- [gunsmith] preview: drew icon %u [%s], %u models", index, c.key.c_str(), u32(visuals.size()));
	}

	void CPreview::draw_view()
	{
		DRAW_LIST list;
		if (!draw_list(m_view_id, m_view_section.c_str(), list))
			return;

		bounds(list, m_view_box);
		Fmatrix view, proj;
		view_matrices(m_view_box, view, proj);

		xr_vector<IRenderVisual*> visuals;
		xr_vector<Fmatrix> xforms;
		for (const auto& item : list)
		{
			visuals.push_back(item.first);
			xforms.push_back(item.second);
		}
		if (!::Render->PreviewRender(VIEW_TEXTURE, VIEW_W, VIEW_H, 0, 0, VIEW_W, VIEW_H, view, proj, visuals.data(),
									 xforms.data(), u32(visuals.size())))
		{
			m_failed = true;
			return;
		}

		// The slots, for markers; view_slots() projects them with the camera of that moment.
		m_view_points.clear();
		IKinematics* K = list.front().first->dcast_PKinematics();
		if (m_view_id != u16(-1))
		{
			CWeapon* weapon = smart_cast<CWeapon*>(Level().Objects.net_Find(m_view_id));
			if (weapon)
				weapon->gs_visuals().slot_points(CVisuals::eWorld, K, weapon->gs_tree(), weapon->cNameSect().c_str(), nullptr, m_view_points);
		}
		else if (SScene* s = scene(m_view_section.c_str()))
			s->visuals.slot_points(CVisuals::eWorld, K, s->tree, m_view_section.c_str(), nullptr, m_view_points);
	}

	// Orbits the build's centre; yaw 0 / pitch 0 looks at its right side like the icons.
	void CPreview::view_matrices(const Fbox& box, Fmatrix& view, Fmatrix& proj) const
	{
		Fvector centre;
		box.getcenter(centre);
		const float radius = _max(box.getradius(), 0.05f);

		const float yaw = deg2rad(m_yaw), pitch = deg2rad(m_pitch);
		Fvector back, dir, up, right, cam_up;
		back.set(_cos(pitch) * _cos(yaw), _sin(pitch), _cos(pitch) * _sin(yaw));
		dir.invert(back);
		up.set(0.f, 1.f, 0.f);
		right.crossproduct(up, dir).normalize();
		cam_up.crossproduct(dir, right).normalize();
		centre.mad(right, -m_pan_x * radius);
		centre.mad(cam_up, -m_pan_y * radius);

		const float aspect = 1.f / m_aspect; // X-Ray: height / width, of the view as shown
		const float half_w = tanf(VIEW_FOV * 0.5f) / aspect; // tangent of half the horizontal FOV
		const float dist = radius / half_w / m_zoom * 1.05f;
		Fvector from;
		from.mad(centre, back, dist);
		view.build_camera_dir(from, dir, up);
		proj.build_projection(VIEW_FOV, aspect, 0.02f, dist + radius * 2.f + 1.f);
	}

	void CPreview::view_slots(xr_vector<SViewSlot>& out) const
	{
		out.clear();
		if (m_view_points.empty())
			return;
		Fmatrix view, proj, vp;
		view_matrices(m_view_box, view, proj);
		vp.mul(proj, view);
		for (const CVisuals::SSlotPoint& p : m_view_points)
		{
			Fvector4 h;
			vp.transform(h, p.pos);
			SViewSlot s;
			s.node = p.node;
			s.slot = p.slot;
			s.visible = h.w > EPS_L;
			const float iw = s.visible ? 1.f / h.w : 0.f;
			s.u = (h.x * iw + 1.f) * 0.5f;
			s.v = (1.f - h.y * iw) * 0.5f;
			s.visible = s.visible && s.u >= 0.f && s.u <= 1.f && s.v >= 0.f && s.v <= 1.f;
			out.push_back(s);
		}
	}

	void CPreview::OnRender()
	{
		if (!available() || !g_pGameLevel)
			return;
		// A gun whose build changed is redrawn in its cell, whether or not the UI asks again
		// (the inventory doesn't refresh a cell when a part is swapped from its menu).
		for (SCell& c : m_cells)
		{
			if (c.id == u16(-1) || c.dirty)
				continue;
			CWeapon* weapon = smart_cast<CWeapon*>(Level().Objects.net_Find(c.id));
			if (!weapon)
				continue;
			string128 key;
			object_key(weapon, key);
			if (c.key != key)
			{
				c.key = key;
				c.dirty = true;
			}
		}

		u32 drawn = 0;
		for (u32 i = 0; i < m_cells.size() && drawn < ICONS_PER_FRAME && !m_failed; ++i)
		{
			if (!m_cells[i].dirty)
				continue;
			draw_icon(i);
			++drawn;
		}
		// The view is drawn while the workbench keeps ticking it.
		if (!m_failed && Device.dwFrame - m_view_tick <= 1 && (m_view_id != u16(-1) || m_view_section.size()))
			draw_view();
	}
}
