////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_preview.cpp
//	Description : Gunsmith previews - weapons drawn with their part models into
//	              textures that 2D UI shows (inventory icons, the workbench view)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_preview.h"
#include "gunsmith_visuals.h"
#include "gunsmith_config.h"
#include "gunsmith_icon_cache.h"
#include "Weapon.h"
#include "inventory_item_object.h"
#include "Level.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/RenderVisual.h"

namespace gunsmith
{
	namespace
	{
		LPCSTR ICON_PAGES[CPreview::MAX_PAGES] = {
			"$user$gs_icons", "$user$gs_icons1", "$user$gs_icons2", "$user$gs_icons3",
			"$user$gs_icons4", "$user$gs_icons5", "$user$gs_icons6", "$user$gs_icons7",
		};
		LPCSTR VIEW_TEXTURE = "$user$gs_view";
		LPCSTR BAKE_TARGET = "$user$gs_bake";
		const float VIEW_FOV = deg2rad(30.f);
		// A cell asked for this frame or the last is in use: it is never given to another build.
		const u32 CELL_HOLD_FRAMES = 1;

		CPreview* g_instance = nullptr;

		// What an object draws: a full-mode gun with its parts, or a part item with the parts it
		// carries (`part`: exported parts face -Z where guns face +Z).
		struct SSource
		{
			IRenderVisual* root = nullptr;
			CVisuals* visuals = nullptr;
			const CTree* tree = nullptr;
			shared_str owner;
			bool part = false;
		};

		bool object_source(u16 id, SSource& src)
		{
			CObject* O = Level().Objects.net_Find(id);
			if (CWeapon* weapon = smart_cast<CWeapon*>(O))
			{
				if (!weapon->gs_full_mode() || !weapon->Visual())
					return false;
				src.root = weapon->Visual();
				src.visuals = &weapon->gs_visuals();
				src.tree = &weapon->gs_tree();
				src.owner = weapon->cNameSect();
				src.part = false;
				return true;
			}
			CInventoryItemObject* item = smart_cast<CInventoryItemObject*>(O);
			if (!item || !item->gs_carries_parts() || !item->Visual())
				return false;
			src.root = item->Visual();
			src.visuals = &item->gs_part_visuals();
			src.tree = &item->gs_subtree();
			src.owner = item->cNameSect();
			src.part = true;
			return true;
		}

		// A build's cell key: a hash of its signature; part items apart (drawn from their other side).
		shared_str build_key(const xr_string& sig, bool part)
		{
			u64 h = 14695981039346656037ull;
			for (char ch : sig)
			{
				h ^= u8(ch);
				h *= 1099511628211ull;
			}
			string32 key;
			xr_sprintf(key, "b%016llx%s", h, part ? "p" : "");
			return shared_str(key);
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

		// A tree's build from node `index` down as text: the same for the same parts in the same slots, whatever
		// order the nodes were added in. A part's mode is in it when not 0 (a sight's mode can show other meshes,
		// e.g. a magnifier flipped aside); a weapon's own node 0 mode isn't (the selected sight, devices on).
		void signature(const CTree& tree, u8 index, xr_string& out)
		{
			out += tree.node(index).section.c_str();
			const u8 mode = tree.node(index).mode;
			if (mode && (index > 0 || tree.size() == 0 || !is_full_mode(tree.node(0).section.c_str())))
			{
				string16 m;
				xr_sprintf(m, "#%u", u32(mode));
				out += m;
			}
			xr_vector<std::pair<u8, u8>> children; // slot, node
			for (u8 i = index + 1; i < tree.size(); ++i)
				if (tree.node(i).parent == index)
					children.push_back(std::make_pair(tree.node(i).slot, i));
			if (children.empty())
				return;
			std::sort(children.begin(), children.end());
			out += '(';
			for (const auto& child : children)
			{
				string16 slot;
				xr_sprintf(slot, "%u:", u32(child.first));
				out += slot;
				signature(tree, child.second, out);
				out += ';';
			}
			out += ')';
		}

		// An icon's pose ([gunsmith_settings] icon_pose, icon_yaw, icon_scale; a section's gs_icon_* override).
		struct SPose
		{
			bool tarkov;
			float yaw, pitch, roll, scale;
			Fvector2 offset; // of the cell: the build moved right and up
		};

		SPose icon_pose(LPCSTR section)
		{
			const SSettings& s = settings();
			SPose p;
			p.tarkov = s.icon_tarkov_pose;
			p.yaw = READ_IF_EXISTS(pSettings, r_float, section, "gs_icon_yaw", s.icon_yaw);
			p.pitch = READ_IF_EXISTS(pSettings, r_float, section, "gs_icon_pitch", 0.f);
			p.roll = READ_IF_EXISTS(pSettings, r_float, section, "gs_icon_roll", 0.f);
			p.scale = clampr(READ_IF_EXISTS(pSettings, r_float, section, "gs_icon_scale", s.icon_scale), 0.2f, 1.5f);
			p.offset = READ_IF_EXISTS(pSettings, r_fvector2, section, "gs_icon_offset", Fvector2().set(0.f, 0.f));
			return p;
		}

		// Everything besides the build that changes an icon's picture, for its icon cache name.
		xr_string icon_look(LPCSTR section)
		{
			const SSettings& s = settings();
			const SPose p = icon_pose(section);
			string256 look;
			xr_sprintf(look, "|pose %d %.2f %.2f %.2f %.3f %.3f %.3f|look %d %.3f %.3f %.3f %.3f %.3f", int(p.tarkov), p.yaw,
				p.pitch, p.roll, p.scale, p.offset.x, p.offset.y, int(s.icon_halo), s.icon_exposure, s.icon_ambient,
				s.icon_specular, s.icon_contrast, s.icon_saturation);
			return xr_string(look);
		}

		// The preview shaders' gs_preview_params: exposure, ambient, specular, and the halo's radius in source pixels
		// for a picture `h` pixels high (4.5% of it: 9 for a 200 px icon cell; 0: the shader's default).
		Fvector4 shading_params(u32 h = 0)
		{
			const SSettings& s = settings();
			return Fvector4().set(s.icon_exposure, s.icon_ambient, s.icon_specular, float(h) * 0.045f);
		}

		// The preview shader's gs_preview_grade: contrast and saturation after lighting.
		Fvector4 grade_params()
		{
			const SSettings& s = settings();
			return Fvector4().set(s.icon_contrast, s.icon_saturation, 0.f, 0.f);
		}

		// A TGA (32-bit, top-down) of w x h pixels in the targets' byte order (RGBA).
		bool write_tga(LPCSTR path, u32 w, u32 h, const u8* rgba)
		{
			FILE* f = fopen(path, "wb");
			if (!f)
				return false;
			u8 head[18] = {};
			head[2] = 2; // uncompressed true colour
			head[12] = u8(w & 0xff);
			head[13] = u8(w >> 8);
			head[14] = u8(h & 0xff);
			head[15] = u8(h >> 8);
			head[16] = 32;
			head[17] = 0x28; // 8 alpha bits, top-left origin
			bool ok = fwrite(head, sizeof(head), 1, f) == 1;
			xr_vector<u8> row(w * 4);
			for (u32 y = 0; y < h && ok; ++y)
			{
				const u8* src = rgba + y * w * 4;
				for (u32 x = 0; x < w; ++x)
				{
					row[x * 4 + 0] = src[x * 4 + 2];
					row[x * 4 + 1] = src[x * 4 + 1];
					row[x * 4 + 2] = src[x * 4 + 0];
					row[x * 4 + 3] = src[x * 4 + 3];
				}
				ok = fwrite(row.data(), 1, row.size(), f) == row.size();
			}
			return fclose(f) == 0 && ok;
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
		m_cells.resize(MAX_PAGES * PAGE_CELLS);
		m_view_box.invalidate();
		Device.seqRender.Add(this, REG_PRIORITY_HIGH);
	}

	CPreview::~CPreview()
	{
		Device.seqRender.Remove(this);
		for (const SSave& save : m_saves)
			::Render->PreviewReadEnd(save.ticket, nullptr); // dropped
		for (auto& it : m_scenes)
			xr_delete(it.second);
	}

	bool CPreview::available() const
	{
		return !m_failed && ::Render->get_dx_level() >= 0x000A0000;
	}

	LPCSTR CPreview::page_texture(u32 page)
	{
		return ICON_PAGES[page < MAX_PAGES ? page : 0];
	}

	void CPreview::icon(u16 id, bool stock_static, SIcon& out)
	{
		out = SIcon();
		SSource src;
		if (!available() || !object_source(id, src))
			return;
		const SBuild& b = object_build(id, *src.tree, src.owner, src.part);
		if (b.sig.empty())
			return;
		if (stock_static && b.stock)
		{
			out.state = eIconStock;
			return;
		}
		icon_request(b.key, b.sig, *src.tree, id, src.owner.c_str(), src.part, out);
	}

	void CPreview::icon(LPCSTR section, SIcon& out)
	{
		out = SIcon();
		if (!available() || !section || !pSettings->section_exist(section) || !is_full_mode(section))
			return;
		const SDefault& d = default_build(shared_str(section));
		if (d.sig.empty())
			return;
		icon_request(d.key, d.sig, d.tree, u16(-1), section, false, out);
	}

	void CPreview::icon_request(const shared_str& key, const xr_string& sig, const CTree& tree, u16 id, LPCSTR section,
	                            bool part, SIcon& out)
	{
		int index = -1;
		auto it = m_index.find(key);
		if (it != m_index.end())
			index = int(it->second);

		if (index < 0)
		{
			index = free_cell();
			if (index < 0)
			{
				if (g_gs_debug & eGsDebugPreview)
					Msg("- [gunsmith] preview: every icon cell is in use (%u pages), [%s] keeps its static icon", m_pages, section);
				return;
			}
			SCell& c = m_cells[index];
			if (g_gs_debug & eGsDebugPreview)
				Msg("- [gunsmith] preview: icon cell %d [%s] for [%s]", index, key.c_str(), section);
			c = SCell();
			c.key = key;
			c.section = section;
			c.part = part;
			c.dirty = true;
			icon_size(section, c.w, c.h);
			if (icon_cache::enabled())
				c.file = icon_cache::file_name(sig + icon_look(section), tree, c.w, c.h, part);
			m_index[key] = u32(index);
		}

		SCell& c = m_cells[index];
		c.used = Device.dwFrame;
		if (id != u16(-1))
			c.id = id;
		u32 page, x, y;
		cell_origin(u32(index), page, x, y);
		out.state = c.drawn ? eIconReady : eIconPending;
		out.cell = u32(index);
		out.texture = page_texture(page);
		out.rect.set(float(x), float(y), float(x + c.w), float(y + c.h));
	}

	void CPreview::cell_origin(u32 index, u32& page, u32& x, u32& y) const
	{
		page = index / PAGE_CELLS;
		const u32 local = index % PAGE_CELLS;
		x = (local % PAGE_COLS) * ICON_W;
		y = (local / PAGE_COLS) * ICON_H;
	}

	// A free cell on the pages in use, else the cell unused longest that nobody asked for this frame or the last,
	// else the first cell of a new page; -1 when every cell of icon_pages pages is in use.
	int CPreview::free_cell()
	{
		const u32 count = m_pages * PAGE_CELLS;
		for (u32 i = 0; i < count; ++i)
			if (!m_cells[i].key.size())
				return int(i);
		int best = -1;
		for (u32 i = 0; i < count; ++i)
			if (m_cells[i].used + CELL_HOLD_FRAMES < Device.dwFrame && (best < 0 || m_cells[i].used < m_cells[best].used))
				best = int(i);
		if (best >= 0)
		{
			release(u32(best));
			return best;
		}
		const u32 pages = _min(settings().icon_pages, u32(MAX_PAGES));
		if (m_pages < pages)
		{
			++m_pages;
			if (g_gs_debug & eGsDebugPreview)
				Msg("- [gunsmith] preview: icon page %u added", m_pages);
			return int(count);
		}
		return -1;
	}

	void CPreview::release(u32 index)
	{
		SCell& c = m_cells[index];
		if (c.key.size())
			m_index.erase(c.key);
		c = SCell();
	}

	const CPreview::SBuild& CPreview::object_build(u16 id, const CTree& tree, const shared_str& section, bool part)
	{
		SBuild& b = m_builds[id];
		if (!b.valid || b.revision != tree.state_revision() || b.section != section || b.part != part)
		{
			b.valid = true;
			b.revision = tree.state_revision();
			b.section = section;
			b.part = part;
			b.sig.clear();
			if (!tree.empty())
				signature(tree, 0, b.sig);
			b.key = build_key(b.sig, part);
			b.stock = !part && !b.sig.empty() && b.sig == default_build(section).sig;
		}
		return b;
	}

	const CPreview::SDefault& CPreview::default_build(const shared_str& section)
	{
		auto it = m_defaults.find(section);
		if (it != m_defaults.end())
			return it->second;
		SDefault& d = m_defaults[section];
		d.tree.build_default(section.c_str(), 0);
		if (!d.tree.empty())
			signature(d.tree, 0, d.sig);
		d.key = build_key(d.sig, false);
		return d;
	}

	// What a cell's picture can be drawn from now: the object that last asked for it while it still has the build,
	// else the section's default build when that is the build. False: nothing to draw it from yet.
	bool CPreview::draw_source(const SCell& c, u16& id)
	{
		if (c.id != u16(-1))
		{
			SSource src;
			if (object_source(c.id, src) && src.owner == c.section && object_build(c.id, *src.tree, src.owner, src.part).key == c.key)
			{
				id = c.id;
				return true;
			}
		}
		if (!c.part && default_build(c.section).key == c.key)
		{
			id = u16(-1);
			return true;
		}
		return false;
	}

	// A cell's picture from the icon cache, without any model.
	bool CPreview::load_icon(u32 index)
	{
		SCell& c = m_cells[index];
		if (!c.file.size() || !icon_cache::load(c.file, c.w, c.h, m_pixels))
			return false;
		u32 page, x, y;
		cell_origin(index, page, x, y);
		if (!::Render->PreviewWriteRect(page_texture(page), ATLAS_W, ATLAS_H, x, y, c.w, c.h, m_pixels.data()))
			return false;
		c.dirty = false;
		c.drawn = true;
		if (g_gs_debug & eGsDebugPreview)
			Msg("- [gunsmith] preview: icon %u [%s] from the cache", index, c.key.c_str());
		return true;
	}

	// Drawn icons the GPU has handed back go into the icon cache.
	void CPreview::save_icons()
	{
		for (u32 i = 0; i < m_saves.size();)
		{
			const SSave& save = m_saves[i];
			m_pixels.resize(save.w * save.h * 4);
			const int done = ::Render->PreviewReadEnd(save.ticket, m_pixels.data());
			if (done == 0)
			{
				++i;
				continue;
			}
			if (done > 0)
				icon_cache::save(save.file, save.w, save.h, m_pixels.data());
			m_saves.erase(m_saves.begin() + i);
		}
	}

	u32 CPreview::preload()
	{
		if (!g_pGameLevel)
			return 0;
		CTimer timer;
		timer.Start();
		u32 count = 0;
		for (u32 i = 0, n = Level().Objects.o_count(); i < n; ++i)
		{
			CObject* O = Level().Objects.o_get_by_iterator(i);
			SSource src;
			if (!O || O->getDestroy() || !object_source(O->ID(), src))
				continue;
			if (IKinematics* K = src.root->dcast_PKinematics())
			{
				src.visuals->ensure(CVisuals::eWorld, K, *src.tree, src.owner.c_str(), nullptr, true);
				++count;
			}
		}
		Msg("- [gunsmith] preview: part models of %u guns and parts loaded in %.0f ms", count, timer.GetElapsed_sec() * 1000.f);
		return count;
	}

	void CPreview::view_object(u16 id)
	{
		m_view_id = id;
	}

	void CPreview::redraw_icons()
	{
		for (u32 i = 0; i < u32(m_cells.size()); ++i)
			release(i);
		m_index.clear();
	}

	void CPreview::view_clear()
	{
		m_view_id = u16(-1);
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

	void CPreview::view_resolution(u32 w, u32 h)
	{
		m_view_w = clampr(w, u32(64), u32(VIEW_MAX));
		m_view_h = clampr(h, u32(64), u32(VIEW_MAX));
	}

	bool CPreview::view_bounds(Frect& uv) const
	{
		if (m_view_points.empty())
			return false;
		const Fmatrix& vp = m_view_vp;
		uv.set(flt_max, flt_max, -flt_max, -flt_max);
		for (u32 i = 0; i < 8; ++i)
		{
			Fvector p;
			m_view_box.getpoint(i, p);
			Fvector4 h;
			vp.transform(h, p); // x, y, z come back divided by w already
			if (h.w <= EPS_L)
				continue;
			const float u = (h.x + 1.f) * 0.5f, v = (1.f - h.y) * 0.5f;
			uv.x1 = _min(uv.x1, u);
			uv.y1 = _min(uv.y1, v);
			uv.x2 = _max(uv.x2, u);
			uv.y2 = _max(uv.y2, v);
		}
		return uv.x1 <= uv.x2;
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
			// a gun with its default build; anything else (a part) is its model alone
			if (is_full_mode(section))
			{
				s->tree.build_default(section, 0);
				s->visuals.ensure(CVisuals::eWorld, K, s->tree, section, nullptr, true);
				// As on a live full-mode gun (CWeapon::UpdateAddonsVisibility, addons disabled): the
				// receiver model's old addon meshes stay hidden, e.g. the AK-12's GP-25.
				for (LPCSTR bone : { "wpn_scope", "wpn_silencer", "wpn_launcher" })
				{
					const u16 id = K->LL_BoneID(bone);
					if (id != BI_NONE)
						K->LL_SetBoneVisible(id, FALSE, TRUE);
				}
			}
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
		SSource src;
		if (id != u16(-1) && object_source(id, src))
		{
			root = src.root;
			visuals = src.visuals;
			if (IKinematics* K = root->dcast_PKinematics())
				visuals->ensure(CVisuals::eWorld, K, *src.tree, src.owner.c_str(), nullptr, true);
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

	void CPreview::draw_icon(u32 index, u16 id)
	{
		SCell& c = m_cells[index];
		c.dirty = false;
		DRAW_LIST list;
		if (!draw_list(id, c.section.c_str(), list))
			return;
		u32 page, x, y;
		cell_origin(index, page, x, y);
		if (!render_build(list, c.section.c_str(), c.part, c.w, c.h, page_texture(page), ATLAS_W, ATLAS_H, x, y))
		{
			m_failed = true;
			return;
		}
		c.drawn = true;
		if (c.file.size())
		{
			SSave save;
			save.ticket = ::Render->PreviewReadBegin(page_texture(page), x, y, c.w, c.h);
			save.file = c.file;
			save.w = c.w;
			save.h = c.h;
			if (save.ticket)
				m_saves.push_back(save);
		}
		if (g_gs_debug & eGsDebugPreview)
			Msg("- [gunsmith] preview: drew icon %u [%s], %u models", index, c.key.c_str(), u32(list.size()));
	}

	bool CPreview::render_build(const DRAW_LIST& list, LPCSTR section, bool part, u32 w, u32 h, LPCSTR target, u32 tw,
	                            u32 th, u32 x, u32 y)
	{
		Fbox box;
		bounds(list, box);
		Fvector centre;
		box.getcenter(centre);
		const float radius = _max(box.getradius(), 0.05f);

		// Orthographic. Tarkov's pose (its icon camera looks along +Z at every gun turned -115 degrees): the gun's
		// left side from -X, the muzzle (+Z) to the left, turned `yaw` degrees with the muzzle towards the camera.
		// The side pose: the right side from +X, the muzzle to the right, as vanilla icons. A part faces -Z, so it
		// is seen from the other side to point the same way. Then pitch (from above) and roll.
		const SPose pose = icon_pose(section);
		const bool from_left = pose.tarkov != part;
		Fvector dir, up, right;
		dir.set(from_left ? 1.f : -1.f, 0.f, 0.f);
		Fmatrix turn;
		turn.rotateY(deg2rad(pose.tarkov ? pose.yaw : -pose.yaw));
		turn.transform_dir(dir);
		up.set(0.f, 1.f, 0.f);
		if (!fis_zero(pose.pitch))
		{
			const float p = deg2rad(pose.pitch);
			Fvector d = dir;
			dir.mul(d, _cos(p)).mad(up, -_sin(p));
			up.mul(_cos(p)).mad(d, _sin(p));
		}
		dir.normalize();
		right.crossproduct(up, dir).normalize();
		if (!fis_zero(pose.roll))
		{
			const float r = deg2rad(pose.roll);
			Fvector u = up;
			up.mul(u, _cos(r)).mad(right, _sin(r));
		}
		up.crossproduct(dir, right).normalize();
		right.crossproduct(up, dir).normalize();

		// The build's box as the camera sees it, from every model's own box (tighter than the world box once turned).
		Fvector from;
		from.mad(centre, dir, -(radius + 1.f));
		Fmatrix view, proj;
		view.build_camera_dir(from, dir, up);
		Fbox seen;
		seen.invalidate();
		for (const auto& item : list)
		{
			const Fbox& b = item.first->getVisData().box;
			for (u32 i = 0; i < 8; ++i)
			{
				Fvector p;
				b.getpoint(i, p);
				item.second.transform_tiny(p);
				view.transform_tiny(p);
				seen.modify(p);
			}
		}

		// Tarkov's fit (CenterByBounds): the box's wider side fills `scale` of the cell, centred, then the offset.
		const float aspect = float(w) / float(h);
		const float width = _max(seen.x2 - seen.x1, (seen.y2 - seen.y1) * aspect) / pose.scale;
		const float height = width / aspect;
		const float cx = (seen.x1 + seen.x2) * 0.5f - pose.offset.x * width;
		const float cy = (seen.y1 + seen.y2) * 0.5f - pose.offset.y * height;
		from.mad(right, cx).mad(up, cy);
		view.build_camera_dir(from, dir, up);
		proj.build_projection_ortho(width, height, _max(seen.z1 - 0.5f, 0.01f), seen.z2 + 0.5f);

		xr_vector<IRenderVisual*> visuals;
		xr_vector<Fmatrix> xforms;
		for (const auto& item : list)
		{
			visuals.push_back(item.first);
			xforms.push_back(item.second);
		}
		const Fvector4 shading = shading_params(h), grade = grade_params();
		const u32 flags = IRender_interface::ePreviewSupersample | (settings().icon_halo ? IRender_interface::ePreviewHalo : 0);
		return ::Render->PreviewRender(target, tw, th, x, y, w, h, view, proj, visuals.data(), xforms.data(),
		                               u32(visuals.size()), flags, &shading, &grade);
	}

	void CPreview::bake_start(LPCSTR filter)
	{
		if (!available())
		{
			Msg("! [gunsmith] icon bake: previews aren't available (DX10/11 and the Framework's preview shaders)");
			return;
		}
		if (m_bake.active)
		{
			Msg("! [gunsmith] icon bake: already running (%u of %u)", m_bake.next, u32(m_bake.sections.size()));
			return;
		}
		m_bake = SBake();
		for (const CInifile::Sect& sect : pSettings->sections())
		{
			LPCSTR name = sect.Name.c_str();
			if (filter && filter[0] && !strstr(name, filter))
				continue;
			LPCSTR tex = nullptr;
			if (!pSettings->line_exist(name, "icons_texture") || !(tex = pSettings->r_string(name, "icons_texture")))
				continue;
			string_path t;
			xr_strcpy(t, tex);
			for (char* p = t; *p; ++p)
				*p = *p == '/' ? '\\' : char(tolower(*p));
			if (strncmp(t, "ui\\gunsmith\\", 12) != 0 || strstr(t, "ui_gunsmith_tarkov"))
				continue;
			const bool gun = is_full_mode(name);
			if (!gun && !(pSettings->line_exist(name, "visual") && pSettings->line_exist(name, "tags")))
				continue;
			m_bake.sections.push_back(sect.Name);
		}
		FS.update_path(m_bake.dir, "$app_data_root$", "gunsmith_bake\\");
		VerifyPath(m_bake.dir);
		m_bake.active = !m_bake.sections.empty();
		Msg("- [gunsmith] icon bake: %u icons into %s%s", u32(m_bake.sections.size()), m_bake.dir,
			m_bake.active ? "" : " (nothing to bake)");
	}

	void CPreview::bake_step()
	{
		// read back: the files
		for (SBakeSlot& slot : m_bake.slots)
		{
			if (!slot.ticket)
				continue;
			m_pixels.resize(slot.w * slot.h * 4);
			const int r = ::Render->PreviewReadEnd(slot.ticket, m_pixels.data());
			if (r == 0)
				continue;
			string_path path;
			xr_sprintf(path, "%s%s.tga", m_bake.dir, slot.section.c_str());
			if (r > 0 && write_tga(path, slot.w, slot.h, m_pixels.data()))
			{
				m_bake.manifest += slot.line.c_str();
				m_bake.manifest += "\n";
				++m_bake.done;
			}
			else
			{
				Msg("! [gunsmith] icon bake: [%s] could not be read back or written", slot.section.c_str());
				++m_bake.failed;
			}
			if (slot.new_scene)
			{
				auto it = m_scenes.find(slot.section);
				if (it != m_scenes.end())
				{
					xr_delete(it->second);
					m_scenes.erase(it);
				}
			}
			slot = SBakeSlot();
		}

		// new pictures into the free slots, within the frame's budget
		const float budget = settings().icon_budget_ms;
		CTimer timer;
		timer.Start();
		for (u32 i = 0; i < BAKE_SLOTS && m_bake.next < m_bake.sections.size(); ++i)
		{
			SBakeSlot& slot = m_bake.slots[i];
			if (slot.ticket)
				continue;
			if (timer.GetElapsed_sec() * 1000.f >= budget)
				break;
			const shared_str section = m_bake.sections[m_bake.next++];
			LPCSTR name = section.c_str();
			const u32 gw = READ_IF_EXISTS(pSettings, r_u32, name, "inv_grid_width", 1);
			const u32 gh = READ_IF_EXISTS(pSettings, r_u32, name, "inv_grid_height", 1);
			const u32 w = gw * BAKE_CELL, h = gh * BAKE_CELL;
			if (!w || !h || w > BAKE_SLOT || h > BAKE_SLOT)
			{
				Msg("! [gunsmith] icon bake: [%s] is %ux%u cells, more than a slot", name, gw, gh);
				++m_bake.failed;
				continue;
			}
			const bool gun = is_full_mode(name);
			const bool had_scene = m_scenes.find(section) != m_scenes.end();
			DRAW_LIST list;
			const u32 sx = (i % (BAKE_W / BAKE_SLOT)) * BAKE_SLOT, sy = (i / (BAKE_W / BAKE_SLOT)) * BAKE_SLOT;
			if (!draw_list(u16(-1), name, list) || !render_build(list, name, !gun, w, h, BAKE_TARGET, BAKE_W, BAKE_H, sx, sy))
			{
				Msg("! [gunsmith] icon bake: [%s] could not be drawn", name);
				++m_bake.failed;
				continue;
			}
			slot.ticket = ::Render->PreviewReadBegin(BAKE_TARGET, sx, sy, w, h);
			if (!slot.ticket)
			{
				++m_bake.failed;
				continue;
			}
			// the line: section, kind, icons_texture, x, y, w, h (cells), the model files it was drawn from
			xr_string models;
			auto add_model = [&](const shared_str& sec) {
				LPCSTR v = READ_IF_EXISTS(pSettings, r_string, sec.c_str(), "visual", (LPCSTR)nullptr);
				if (v && v[0])
				{
					if (models.size())
						models += ";";
					models += v;
				}
			};
			auto it = m_scenes.find(section);
			if (gun && it != m_scenes.end() && it->second && !it->second->tree.empty())
				for (const SNode& n : it->second->tree.nodes())
					add_model(n.section);
			else
				add_model(section);
			string1024 line;
			xr_sprintf(line, "%s\t%s\t%s\t%u\t%u\t%u\t%u\t%s", name, gun ? "gun" : "part",
				pSettings->r_string(name, "icons_texture"), READ_IF_EXISTS(pSettings, r_u32, name, "inv_grid_x", 0),
				READ_IF_EXISTS(pSettings, r_u32, name, "inv_grid_y", 0), gw, gh, models.c_str());
			slot.section = section;
			slot.line = line;
			slot.w = w;
			slot.h = h;
			slot.new_scene = !had_scene && !gun;
		}

		bool pending = false;
		for (const SBakeSlot& slot : m_bake.slots)
			pending = pending || slot.ticket != 0;
		if (m_bake.next % 100 == 0 && m_bake.next && !pending)
			Msg("- [gunsmith] icon bake: %u of %u", m_bake.next, u32(m_bake.sections.size()));
		if (m_bake.next >= m_bake.sections.size() && !pending)
		{
			string_path path;
			xr_sprintf(path, "%sbake.txt", m_bake.dir);
			if (FILE* f = fopen(path, "wb"))
			{
				fwrite(m_bake.manifest.data(), 1, m_bake.manifest.size(), f);
				fclose(f);
			}
			Msg("- [gunsmith] icon bake: done, %u icons in %s (%u failed); run Tarkov Extract Data\\tools\\gs_icon_bake.py",
				m_bake.done, m_bake.dir, m_bake.failed);
			m_bake.active = false;
		}
	}

	void CPreview::draw_view()
	{
		DRAW_LIST list;
		if (!draw_list(m_view_id, nullptr, list))
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
		const Fvector4 shading = shading_params(m_view_h), grade = grade_params();
		if (!::Render->PreviewRender(VIEW_TEXTURE, m_view_w, m_view_h, 0, 0, m_view_w, m_view_h, view, proj, visuals.data(),
									 xforms.data(), u32(visuals.size()), IRender_interface::ePreviewSupersample, &shading, &grade))
		{
			m_failed = true;
			return;
		}
		m_view_vp.mul(proj, view); // the markers and bounds project with exactly these
		m_view_zoom = m_zoom;
		m_view_pan_x = m_pan_x;
		m_view_pan_y = m_pan_y;

		// The slots, for markers; view_slots() projects them with the matrices of this draw.
		m_view_points.clear();
		IKinematics* K = list.front().first->dcast_PKinematics();
		SSource src;
		if (object_source(m_view_id, src))
			src.visuals->slot_points(CVisuals::eWorld, K, *src.tree, src.owner.c_str(), nullptr, m_view_points);
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
		const Fmatrix& vp = m_view_vp;
		for (const CVisuals::SSlotPoint& p : m_view_points)
		{
			// Fmatrix::transform(Fvector4&, Fvector) returns x, y, z already divided by w (w kept): don't divide
			// again, or every dot is scaled by 1/distance.
			Fvector4 h;
			vp.transform(h, p.pos);
			SViewSlot s;
			s.node = p.node;
			s.slot = p.slot;
			s.visible = h.w > EPS_L;
			s.u = s.visible ? (h.x + 1.f) * 0.5f : 0.f;
			s.v = s.visible ? (1.f - h.y) * 0.5f : 0.f;
			s.visible = s.visible && s.u >= 0.f && s.u <= 1.f && s.v >= 0.f && s.v <= 1.f;
			out.push_back(s);
		}
		if ((g_gs_debug & eGsDebugPreview) && !out.empty() && Device.dwFrame % 60 == 0)
		{
			Fvector c;
			m_view_box.getcenter(c);
			Msg("- [gunsmith] view: camera now yaw %.1f pitch %.1f zoom %.3f pan %.3f,%.3f aspect %.3f; drawn with zoom %.3f pan %.3f,%.3f; "
				"box centre %.3f,%.3f,%.3f radius %.3f; view %ux%u; slot %u:%u at %.3f,%.3f,%.3f -> u %.3f v %.3f",
				m_yaw, m_pitch, m_zoom, m_pan_x, m_pan_y, m_aspect, m_view_zoom, m_view_pan_x, m_view_pan_y,
				c.x, c.y, c.z, m_view_box.getradius(), m_view_w, m_view_h, u32(m_view_points[0].node),
				u32(m_view_points[0].slot), m_view_points[0].pos.x, m_view_points[0].pos.y, m_view_points[0].pos.z,
				out[0].u, out[0].v);
		}
	}

	void CPreview::OnRender()
	{
		if (!available() || !g_pGameLevel)
			return;
		save_icons();
		if (m_bake.active && !m_failed)
			bake_step();

		// New icons in use, from the icon cache or drawn, until the frame's budget is spent (at least one a frame).
		// A cell nobody shows waits until it is asked for again; one with nothing to draw it from waits for one.
		const u32 count = m_pages * PAGE_CELLS;
		const float budget = settings().icon_budget_ms;
		CTimer timer;
		timer.Start();
		bool any = false;
		for (u32 i = 0; i < count && !m_failed; ++i)
		{
			SCell& c = m_cells[i];
			if (!c.dirty || c.used + CELL_HOLD_FRAMES < Device.dwFrame)
				continue;
			if (any && timer.GetElapsed_sec() * 1000.f >= budget)
				break;
			if (!c.cache_tried)
			{
				c.cache_tried = true;
				if (load_icon(i))
				{
					any = true;
					continue;
				}
			}
			u16 id;
			if (!draw_source(c, id))
				continue;
			draw_icon(i, id);
			any = true;
		}
		// The view is drawn while the workbench keeps ticking it.
		if (!m_failed && Device.dwFrame - m_view_tick <= 1 && m_view_id != u16(-1))
		{
			// Only when what it shows changed (gun, build, camera, size), when the builder was just opened, and every
			// 30 frames anyway, so a render target the driver dropped comes back.
			SViewKey key;
			key.id = m_view_id;
			SSource src;
			if (object_source(m_view_id, src))
				key.revision = src.tree->state_revision();
			key.yaw = m_yaw;
			key.pitch = m_pitch;
			key.zoom = m_zoom;
			key.pan_x = m_pan_x;
			key.pan_y = m_pan_y;
			key.aspect = m_aspect;
			key.w = m_view_w;
			key.h = m_view_h;
			const bool reopened = m_view_active + 1 < Device.dwFrame;
			m_view_active = Device.dwFrame;
			if (reopened || !(key == m_view_key) || Device.dwFrame - m_view_drawn >= 30)
			{
				m_view_key = key;
				m_view_drawn = Device.dwFrame;
				draw_view();
			}
		}
	}
}
