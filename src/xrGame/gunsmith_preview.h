////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_preview.h
//	Description : Gunsmith previews - weapons drawn with their part models into
//	              textures that 2D UI shows (inventory icons, the workbench view)
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "../xrEngine/pure.h"
#include "gunsmith_visuals.h"

class CWeapon;
class IRenderVisual;

namespace gunsmith
{
	// $user$ textures, drawn by IRender_interface::PreviewRender (DX10/11 only):
	//   $user$gs_icons, $user$gs_icons1...  inventory icons: atlas pages of side views, muzzle to
	//                   the right like vanilla icons, one cell per build (every object with the same
	//                   parts in the same slots shares it); a page is added when every cell is in use
	//                   (icon_pages at most)
	//   $user$gs_view   the workbench view: a perspective the player orbits
	// A cell is filled from the icon disk cache when it has the build (gunsmith_icon_cache.h), else drawn
	// from the models of an object with that build (or a section's gs_default_build) and then saved there.
	//
	// A cell shown by the UI must be asked for again every frame it is shown (icon()): a cell nobody
	// asked for this frame or the last can be given to another build, never one still asked for.
	class CPreview : public pureRender
	{
	public:
		enum
		{
			ICON_W = 500, // a cell: twice a 5x2 inventory icon, so the UI's filtering smooths it
			ICON_H = 200,
			ATLAS_W = 2048,
			ATLAS_H = 2048,
			PAGE_COLS = ATLAS_W / ICON_W,
			PAGE_CELLS = PAGE_COLS * (ATLAS_H / ICON_H),
			MAX_PAGES = 8,
			BAKE_CELL = 50, // a static icon's pixels a cell (Anomaly's inventory grid)
			BAKE_W = 2048, // the bake's target: slots of BAKE_SLOT x BAKE_SLOT, a picture in each
			BAKE_H = 1024,
			BAKE_SLOT = 512,
			BAKE_SLOTS = (BAKE_W / BAKE_SLOT) * (BAKE_H / BAKE_SLOT),
			VIEW_W = 1024, // default view texture size; view_resolution() changes it
			VIEW_H = 512,
			VIEW_MAX = 2048,
		};

		// What an icon request found. eNone: nothing to draw (not a full-mode gun or a part carrying parts,
		// previews unavailable, or every cell of every page in use: keep the static icon). eStock: a weapon
		// with its section's default build, when the caller has a static icon for that (no cell taken).
		// ePending: a cell is taken but not drawn yet (keep the static icon). eReady: the cell shows it.
		enum EIcon
		{
			eIconNone,
			eIconStock,
			eIconPending,
			eIconReady,
		};
		struct SIcon
		{
			EIcon state = eIconNone;
			u32 cell = 0; // index over every page
			LPCSTR texture = nullptr; // the cell's page
			Frect rect; // in the page, pixels
		};

		CPreview(); // use instance()
		~CPreview();

		static CPreview& instance();
		static void destroy(); // level unload: frees the section models

		// Whether previews can be drawn (DX10/11 renderer, preview shader present).
		bool available() const;

		// The icon of a weapon object (gs_mode = full) or of a part item carrying parts, or of a
		// section's default build: a cell with the section's inventory aspect. A new or changed
		// build is drawn within a few frames (icon_budget_ms of drawing a frame). With
		// `stock_static`, a weapon whose build is its section's default build takes no cell (eStock).
		// Asking marks the cell in use for this frame.
		void icon(u16 id, bool stock_static, SIcon& out);
		void icon(LPCSTR section, SIcon& out);
		static LPCSTR page_texture(u32 page);

		// Loads the part models of every Gunsmith gun and part item carrying parts on the level (their world
		// models), so neither their icons nor their first sight in the world wait for files. For a loading screen.
		// Returns how many objects.
		u32 preload();

		// Bakes the static icons: every section whose icons_texture is in ui\gunsmith\ (guns with their default
		// build, parts on their own), drawn as the live icons are, at the static icon's size (BAKE_CELL a cell),
		// into appdata\gunsmith_bake\<section>.tga with bake.txt (section, kind, icons_texture, x, y, w, h in cells,
		// the model files), a few a frame (console gs_icon_bake [filter]). Tarkov Extract Data\tools\gs_icon_bake.py
		// puts them into the shipped sheets. `filter`: only sections whose name contains it.
		void bake_start(LPCSTR filter);

		// Workbench view: what it shows, its camera, and view_tick() every frame it is shown.
		void view_object(u16 id);
		void view_clear();
		// `aspect` is the shown view's width / height on screen; the texture is stretched to it.
		void view_camera(float yaw_deg, float pitch_deg, float zoom, float pan_x, float pan_y, float aspect);
		void view_tick();
		void view_resolution(u32 w, u32 h);
		u32 view_width() const { return m_view_w; }
		u32 view_height() const { return m_view_h; }

		// The build's box on screen as last drawn, in u, v across the view (0..1 from the top
		// left; can lie outside). False before the first view is drawn.
		bool view_bounds(Frect& uv) const;

		// The slots' points (CVisuals::slot_points) projected with the very matrices the last view
		// was drawn with, so they always match the image: u, v across the view (0..1, from the top
		// left), and whether the point is in front of the camera and inside the view.
		struct SViewSlot
		{
			u8 node, slot;
			float u, v;
			bool visible;
		};
		void view_slots(xr_vector<SViewSlot>& out) const;

		virtual void OnRender();

	private:
		typedef xr_vector<std::pair<IRenderVisual*, Fmatrix>> DRAW_LIST;

		struct SScene; // a section's default build, loaded once
		struct SCell
		{
			shared_str key; // "b<hash of the build's signature>", "p" added for a part item
			u16 id = u16(-1); // the object that last asked for it, drawn from its own models; none: the default build
			shared_str section;
			shared_str file; // its icon cache file, or empty
			u32 used = 0; // frame last asked for
			u32 w = 0, h = 0; // drawn area, from the cell's top left
			bool dirty = false;
			bool drawn = false; // holds the build's picture
			bool part = false; // a part item: drawn from its other side, so it faces like a gun
			bool cache_tried = false;
		};
		// An object's build, cached per object and layout revision.
		struct SBuild
		{
			shared_str section, key;
			xr_string sig;
			u32 revision = 0;
			bool valid = false, part = false;
			bool stock = false; // its section's default build
		};
		// A section's default build.
		struct SDefault
		{
			shared_str key;
			xr_string sig;
			CTree tree;
		};
		// A drawn icon on its way back from the GPU, to be saved in the icon cache.
		struct SSave
		{
			u32 ticket;
			shared_str file;
			u32 w, h;
		};

		void icon_request(const shared_str& key, const xr_string& sig, const CTree& tree, u16 id, LPCSTR section,
		                  bool part, SIcon& out);
		int free_cell();
		void release(u32 index);
		const SBuild& object_build(u16 id, const CTree& tree, const shared_str& section, bool part);
		const SDefault& default_build(const shared_str& section);
		bool draw_source(const SCell& c, u16& id);
		// Draws a build into the rect (x, y, w, h) of `target` (tw x th), posed and lit as an icon.
		bool render_build(const DRAW_LIST& list, LPCSTR section, bool part, u32 w, u32 h, LPCSTR target, u32 tw, u32 th,
		                  u32 x, u32 y);
		void bake_step();
		bool load_icon(u32 index);
		void save_icons();
		void cell_origin(u32 index, u32& page, u32& x, u32& y) const;
		bool draw_list(u16 id, LPCSTR section, DRAW_LIST& out);
		SScene* scene(LPCSTR section);
		void draw_icon(u32 index, u16 id);
		void draw_view();
		void view_matrices(const Fbox& box, Fmatrix& view, Fmatrix& proj) const;

		xr_vector<SCell> m_cells; // PAGE_CELLS a page, m_pages pages
		u32 m_pages = 1;
		xr_unordered_map<shared_str, u32> m_index; // cell key -> cell
		xr_unordered_map<u16, SBuild> m_builds;
		xr_unordered_map<shared_str, SDefault> m_defaults;
		xr_vector<SSave> m_saves;
		xr_vector<u8> m_pixels; // scratch for loading and saving icons

		struct SBakeSlot
		{
			u32 ticket = 0;
			shared_str section, line; // its bake.txt line (without the file's own fields)
			u32 w = 0, h = 0;
			bool new_scene = false; // its scene was made for the bake: freed once read back
		};
		struct SBake
		{
			bool active = false;
			xr_vector<shared_str> sections;
			u32 next = 0, done = 0, failed = 0;
			SBakeSlot slots[BAKE_SLOTS];
			string_path dir;
			xr_string manifest;
		};
		SBake m_bake;
		xr_map<shared_str, SScene*> m_scenes;
		bool m_failed = false;

		u16 m_view_id = u16(-1);
		u32 m_view_tick = 0;
		float m_yaw = 0.f, m_pitch = 10.f, m_zoom = 1.f, m_pan_x = 0.f, m_pan_y = 0.f, m_aspect = 2.f;
		Fbox m_view_box; // of the last view drawn
		u32 m_view_w = VIEW_W, m_view_h = VIEW_H;
		xr_vector<CVisuals::SSlotPoint> m_view_points; // its slots, in the receiver model's space
		Fmatrix m_view_vp; // projection * view the last view was drawn with
		float m_view_zoom = 1.f, m_view_pan_x = 0.f, m_view_pan_y = 0.f; // its camera (debug log)
		// What the last view was drawn for: it is redrawn when any of it changes (it was redrawn every frame)
		struct SViewKey
		{
			u16 id = u16(-1);
			u32 revision = 0;
			float yaw = 0.f, pitch = 0.f, zoom = 0.f, pan_x = 0.f, pan_y = 0.f, aspect = 0.f;
			u32 w = 0, h = 0;
			bool operator==(const SViewKey& o) const
			{
				return id == o.id && revision == o.revision && yaw == o.yaw && pitch == o.pitch &&
					zoom == o.zoom && pan_x == o.pan_x && pan_y == o.pan_y && aspect == o.aspect && w == o.w && h == o.h;
			}
		};
		SViewKey m_view_key;
		u32 m_view_drawn = 0; // frame of the last draw
		u32 m_view_active = 0; // last frame the view was ticked and checked
	};
}
