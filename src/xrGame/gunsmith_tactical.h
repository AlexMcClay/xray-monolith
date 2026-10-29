////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_tactical.h
//	Description : Gunsmith tactical devices - weapon-mounted flashlights and lasers,
//	              any number per gun, each in its own mode (Gunsmith Docs 16)
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

	// A light definition section, with CTorch's key names where they overlap:
	//   type (2 spot), range, color, spot_angle (degrees), spot_texture, glow_texture,
	//   glow_radius, volumetric, volumetric_quality / _intensity / _distance
	// Lasers ([gs_tac_laser_*]) also read:
	//   range (how far the laser reaches, metres), dot_range (the dot's light), dot_glow_texture,
	//   dot_glow_radius, beam_visual (world model; its first-person copy is <path's folder>\hud\<name>),
	//   beam_hud_length (the beam's length in first person, metres)
	struct SLightDesc
	{
		u32 type = 2;
		float range = 30.f;
		Fcolor color;
		float cone = deg2rad(40.f);
		shared_str texture, glow_texture;
		float glow_radius = 0.f;
		bool volumetric = false;
		float vol_quality = 1.f, vol_intensity = 0.15f, vol_distance = 0.4f;
		float dot_range = 0.25f;
		shared_str dot_glow_texture;
		float dot_glow_radius = 0.02f;
		shared_str beam_visual;
		float beam_hud_length = 4.f;

		void load(LPCSTR section);
	};

	// The tactical devices fitted to one weapon. The weapon's tree holds the state: node 0's mode is
	// the devices-on flag, each device node's mode is its selected mode.
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
		// world), else null. ignore: skipped by the laser's ray (the holder).
		void update(const CTree& tree, CVisuals& vis, int ctx, IKinematics* root, const Fmatrix& root_xform,
					const Fmatrix* hud_proj, CObject* ignore);
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
			ref_glow light_glow;
			ref_light dot;
			ref_glow dot_glow;
			IRenderVisual* beam[2] = { nullptr, nullptr }; // world, HUD
			Fmatrix beam_xform;
			int beam_ctx = -1; // the context the beam was placed for this frame, -1 none
		};

		xr_vector<SDevice> m_devices;
		xr_vector<shared_str> m_sig; // "<node>:<section>" of each device, to spot tree changes
		bool m_lit = false;

		void build(SDevice& d, bool shadow);
		void free_device(SDevice& d);
		void device_off(SDevice& d);
	};
}
