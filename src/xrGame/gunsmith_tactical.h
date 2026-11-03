////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_tactical.h
//	Description : Gunsmith tactical devices - weapon-mounted flashlights and lasers,
//	              any number per gun, each in its own mode
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_tree.h"
#include "../xrEngine/Render.h"

class IKinematics;
class IRenderVisual;
class CObject;

namespace gunsmith
{
	class CVisuals;

	// A light or laser definition section (light_desc caches them by name). Lights use CTorch's key names where
	// they overlap. Keys: see the Gunsmith tactical devices doc. Visuals are models; a visual drawn with the HUD
	// is loaded from <path's folder>\hud\<name>.
	struct SLightDesc
	{
		bool ir = false; // infrared: shown only while ir_visible (the player's night vision is on)
		// light
		u32 type = 2;
		float range = 30.f;
		Fcolor color;
		float cone = deg2rad(40.f);
		shared_str texture;
		bool volumetric = false;
		float vol_quality = 1.f, vol_intensity = 0.15f, vol_distance = 0.4f;
		// laser: the ray, the beam (a ribbon from the device to the hit), the dot where it hits and its light
		float laser_range = 100.f;
		shared_str beam_visual, dot_visual;
		float beam_width = 0.004f, beam_intensity = 1.f;
		float dot_size_close = 0.017f, dot_size_far = 0.2f; // the dot's half-size, near and at laser_range
		float dot_view_scale = 1.5f; // the dot grows up to this with the viewer's distance (to laser_range)
		float dot_intensity = 1.f;
		float dot_light_range = 0.2f, dot_light_intensity = 3.1f;
		float dot_cone_close = deg2rad(15.f), dot_cone_far = deg2rad(119.f);
		// the lens while lit: a glow facing the camera, brighter as the device points at it
		shared_str lens_visual;
		float lens_size = 0.f, lens_intensity = 1.f;
		float lens_angle_min = 0.6f; // cosine: the flare shows from this angle to the device's axis

		void load(LPCSTR section);
	};

	const SLightDesc& light_desc(const shared_str& section);

	// The tactical devices fitted to one weapon. The weapon's tree holds the state: bit 0 of node 0's mode
	// is the devices-on flag, each device node's mode is its selected mode.
	class CTacticalDevices
	{
	public:
		~CTacticalDevices() { clear(); }

		void clear();
		// Matches the device list to the tree's fitted devices (cheap when nothing changed).
		void sync(const CTree& tree);
		bool empty() const { return m_devices.empty(); }

		// Places and lights every device in its current mode. ctx/root/root_xform: the model the
		// devices are drawn on (CVisuals::eHud with the HUD item's transform, or eWorld with the
		// weapon's). hud_proj: the HUD projection when ctx is the HUD (laser rays, beams and dots are in the
		// world), else null. ignore: skipped by the laser's ray (the holder). player: the lights are the
		// player's own (renderers treat them as the head torch, e.g. less volumetric fog).
		// The strongest device_shadows lights (gunsmith_settings) cast shadows.
		void update(const CTree& tree, CVisuals& vis, int ctx, IKinematics* root, const Fmatrix& root_xform,
					const Fmatrix* hud_proj, CObject* ignore, bool player);
		// Switches everything off.
		void off();
		// Draws the lit lenses placed for `ctx` (from that model's render pass).
		void render(int ctx);
		// Draws the laser beams and dots (world pass, in first person too).
		void render_world();
		// A light (not just a laser) is shining.
		bool lit() const { return m_lit; }

	private:
		enum { eLensLight, eLensLaser, eLensCount };
		struct SDevice
		{
			u8 node = 0;
			shared_str section;
			int mode = -1; // the mode the light and laser are set up for
			const SLightDesc* light_def = nullptr; // this mode's, or null
			const SLightDesc* laser_def = nullptr;
			ref_light light;
			float light_score = 0.f; // brightness x range x cone: who gets the shadows
			bool shadow = false;
			ref_light dot;
			IRenderVisual* beam = nullptr;
			IRenderVisual* dot_vis = nullptr;
			IRenderVisual* lens[eLensCount][2] = {}; // [light, laser][world, HUD]
			Fmatrix beam_xform, dot_xform, lens_xform[eLensCount];
			bool beam_on = false, dot_on = false;
			int lens_ctx[eLensCount] = { -1, -1 }; // the context each lens was placed for this frame, -1 none
		};

		xr_vector<SDevice> m_devices;
		xr_vector<shared_str> m_sig; // "<node>:<section>" of each device, to spot tree changes
		u32 m_rev = 0; // the tree's layout revision m_sig was made for (m_rev_valid)
		bool m_rev_valid = false;
		bool m_lit = false;

		void setup_mode(SDevice& d, u8 mode);
		void free_device(SDevice& d);
		void device_off(SDevice& d);
	};
}
