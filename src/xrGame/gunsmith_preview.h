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
	// Two $user$ textures, drawn by IRender_interface::PreviewRender (DX10/11 only):
	//   $user$gs_icons  inventory icons: an atlas of side views, muzzle to the right like
	//                   vanilla icons, one cell per build, redrawn when the build changes
	//   $user$gs_view   the workbench view: a perspective the player orbits
	// A live weapon is drawn from its own models; a section from its gs_default_build.
	class CPreview : public pureRender
	{
	public:
		enum
		{
			ICON_W = 500, // a cell: twice a 5x2 inventory icon, so the UI's filtering smooths it
			ICON_H = 200,
			ATLAS_W = 2048,
			ATLAS_H = 2048,
			VIEW_W = 1024, // default view texture size; view_resolution() changes it
			VIEW_H = 512,
			VIEW_MAX = 2048,
		};

		CPreview(); // use instance()
		~CPreview();

		static CPreview& instance();
		static void destroy(); // level unload: frees the section models

		// Whether previews can be drawn (DX10/11 renderer, preview shader present).
		bool available() const;

		// The icon of a weapon object (gs_mode = full) or of a part item carrying parts, or of a
		// section's default build: a rect in the atlas (pixels) with the section's inventory
		// aspect. A new or changed build is drawn on the next frame. False when previews aren't
		// available or the object has nothing to show.
		bool icon_rect(u16 id, Frect& rect);
		bool icon_rect(LPCSTR section, Frect& rect);

		// Workbench view: what it shows, its camera, and view_tick() every frame it is shown.
		void view_object(u16 id);
		void view_section(LPCSTR section);
		void view_clear();
		// `aspect` is the shown view's width / height on screen; the texture is stretched to it.
		void view_camera(float yaw_deg, float pitch_deg, float zoom, float pan_x, float pan_y, float aspect);
		void view_tick();
		void view_resolution(u32 w, u32 h);
		u32 view_width() const { return m_view_w; }
		u32 view_height() const { return m_view_h; }

		// The build's box on screen with the current camera, in u, v across the view (0..1 from
		// the top left; can lie outside). False before the first view is drawn.
		bool view_bounds(Frect& uv) const;

		// The slots' mount points (from the last view drawn) through the current camera, so they
		// match the image drawn this frame: u, v across the view (0..1, from the top left), and
		// whether the point is in front of the camera and inside the view.
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
			shared_str key; // "o<id>:<build crc>" or "s<section>"
			u16 id = u16(-1);
			shared_str section;
			u32 used = 0; // frame last asked for
			u32 w = 0, h = 0; // drawn area, from the cell's top left
			bool dirty = false;
			bool part = false; // a part item: drawn from its other side, so it faces like a gun
		};

		bool icon_request(LPCSTR key, u16 id, LPCSTR section, bool part, Frect& rect);
		bool draw_list(u16 id, LPCSTR section, DRAW_LIST& out);
		SScene* scene(LPCSTR section);
		void draw_icon(u32 index);
		void draw_view();
		void view_matrices(const Fbox& box, Fmatrix& view, Fmatrix& proj) const;

		xr_vector<SCell> m_cells;
		xr_map<shared_str, SScene*> m_scenes;
		bool m_failed = false;

		u16 m_view_id = u16(-1);
		shared_str m_view_section;
		u32 m_view_tick = 0;
		float m_yaw = 0.f, m_pitch = 10.f, m_zoom = 1.f, m_pan_x = 0.f, m_pan_y = 0.f, m_aspect = 2.f;
		Fbox m_view_box; // of the last view drawn
		u32 m_view_w = VIEW_W, m_view_h = VIEW_H;
		xr_vector<CVisuals::SSlotPoint> m_view_points; // its slots, in the receiver model's space
	};
}
