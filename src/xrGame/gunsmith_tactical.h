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

	// A light definition section, with CTorch's key names where they overlap; lasers read a few more.
	// Keys: see the Gunsmith tactical devices doc. A beam_visual's first-person copy is <path's folder>\hud\<name>.
	struct SLightDesc
	{
		u32 type = 2;
		float range = 30.f;
		Fcolor color;
		float cone = deg2rad(40.f);
		shared_str texture;
		bool volumetric = false;
		float vol_quality = 1.f, vol_intensity = 0.15f, vol_distance = 0.4f;
		float dot_range = 0.25f;
		shared_str beam_visual;
		float beam_hud_length = 4.f;

		void load(LPCSTR section);
	};

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
		// weapon's). hud_proj: the HUD projection when ctx is the HUD (laser rays are cast in the
		// world), else null. ignore: skipped by the laser's ray (the holder). player: the lights are the
		// player's own (renderers treat them as the head torch, e.g. less volumetric fog).
		// The strongest device_shadows lights (gunsmith_settings) cast shadows.
		void update(const CTree& tree, CVisuals& vis, int ctx, IKinematics* root, const Fmatrix& root_xform,
					const Fmatrix* hud_proj, CObject* ignore, bool player);
		// Switches everything off.
		void off();
		// Draws the laser beams placed for `ctx` (from that model's render pass).
		void render_beams(int ctx);
		// A light (not just a laser) is shining.
		bool lit() const { return m_lit; }

	private:
		struct SDevice
		{
			u8 node = 0;
			shared_str section;
			ref_light light;
			float light_score = 0.f; // brightness x range x cone: who gets the shadows
			bool shadow = false;
			ref_light dot;
			IRenderVisual* beam[2] = { nullptr, nullptr }; // world, HUD
			float laser_range = 300.f; // the laser section's range (not light range's r2 variant)
			float beam_hud_length = 4.f;
			Fmatrix beam_xform;
			int beam_ctx = -1; // the context the beam was placed for this frame, -1 none
		};

		xr_vector<SDevice> m_devices;
		xr_vector<shared_str> m_sig; // "<node>:<section>" of each device, to spot tree changes
		u32 m_rev = 0; // the tree's layout revision m_sig was made for (m_rev_valid)
		bool m_rev_valid = false;
		bool m_lit = false;

		void build(SDevice& d);
		void free_device(SDevice& d);
		void device_off(SDevice& d);
	};
}
