////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_visuals.h
//	Description : Gunsmith part tree - each part rendered as its own child
//	              model, mounted on its parent's model (world and HUD)
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_tree.h"

class IKinematics;
class IRenderVisual;

namespace gunsmith
{
	// Visual keys of a part section (models, mount offset, sight, optic, sight modes, muzzle, tactical device),
	// cached per section. Keys: see the Gunsmith config reference.
	// The part's node mode holds the chosen sight and mode: bits 0-3 the mode, bits 4-5 the sight (tactical
	// devices use their node's mode for themselves: a part that is both keeps its first sight and mode).
	// All positions are in the model's own space (as exported) and follow the named bone's
	// motion; how a bone is oriented at rest doesn't matter.
	struct SPartVisual
	{
		shared_str visual, hud_visual;
		Fmatrix offset;
		bool has_aim;
		int aim_priority;
		shared_str aim_bone;
		Fvector aim_point;
		Fvector aim_fwd, aim_up;
		bool has_fire;
		shared_str fire_bone;
		Fvector fire_point;
		float magnification, magnification_min, zoom_factor;
		u32 zoom_steps;
		shared_str scope_texture;
		bool has_eye;
		Fvector eye_point;
		float eye_offset;
		bool is_optic() const { return has_aim && (magnification > 1.f || zoom_factor > 0.f); }
		xr_vector<float> mode_zooms; // per mode of the first sight (empty: one mode)
		xr_vector<shared_str> mode_bones;
		struct SExtraSight
		{
			Fvector aim_point, aim_fwd, aim_up;
			float magnification;
		};
		xr_vector<SExtraSight> extra_sights; // sights 1.. (sight 0 is aim_point, aim_dir, ...)
		u8 sight_count() const { return u8(1 + extra_sights.size()); }
		u8 mode_count() const { return mode_zooms.empty() ? u8(1) : u8(mode_zooms.size()); }

		enum { eTacLight = 1, eTacLaser = 2 };
		xr_vector<u8> tac_modes; // per mode: eTacLight | eTacLaser
		Fvector light_point, laser_point;
		shared_str light_section, laser_section;
		bool is_tactical() const { return !tac_modes.empty(); }

		// The left hand on the part (grip IK, gunsmith_grip_ik.h), per Tarkov GripType 0 and 1: the left hand
		// bone in the model's space and the fingers' rotations relative to their parents (grip_hand_<type>,
		// grip_fingers_<type>, grip_priority, grip_type; see the Gunsmith config reference).
		bool has_grip[2];
		Fmatrix grip_hand[2];
		Fvector grip_fingers[2][15]; // radians
		int grip_priority;
		int grip_type;
		// The flags of mode `mode` (clamped to the last mode).
		u8 tac_flags(u8 mode) const { return tac_modes.empty() ? 0 : tac_modes[_min(u32(mode), u32(tac_modes.size() - 1))]; }
	};

	const SPartVisual& part_visual(const shared_str& section);
	const SPartVisual& part_visual(LPCSTR section);

	// A part's chosen sight and mode, from its node mode (see SPartVisual): the sight index and the mode,
	// clamped to what the part has.
	inline u8 node_sight(const SPartVisual& pv, u8 node_mode) { return pv.is_tactical() ? 0 : u8(_min(u32((node_mode >> 4) & 3), u32(pv.sight_count() - 1))); }
	inline u8 node_sight_mode(const SPartVisual& pv, u8 node_mode) { return pv.is_tactical() ? 0 : u8(_min(u32(node_mode & 15), u32(pv.mode_count() - 1))); }
	inline u8 make_sight_mode(u8 sight, u8 mode) { return u8(((sight & 3) << 4) | (mode & 15)); }

	// What aiming through a part's chosen sight and mode uses.
	struct SSightAim
	{
		Fvector point, fwd, up;
		bool has_eye;
		Fvector eye;
		float magnification, magnification_min, zoom_factor;
		u32 zoom_steps;
		shared_str scope_texture;
		bool optic() const { return magnification > 1.f || zoom_factor > 0.f; }
	};
	SSightAim sight_aim(const SPartVisual& pv, u8 node_mode);

	// Sight switching. A gun cycles through its fitted cyclable sights (an aim point and
	// aim_priority >= 1: optics and red dots, not iron sights) with vanilla's wpn_func key. The chosen
	// sight's node is saved in node 0's mode, bits 1-7 (bit 0 is the tactical devices' on/off; 0 = automatic).
	inline bool is_cyclable_sight(const SPartVisual& pv) { return pv.has_aim && pv.aim_priority >= 1; }
	inline u8 selected_sight(const CTree& tree) { return tree.selected_sight(); }
	// The node a gun aims through: `selected` when it is a fitted cyclable sight, else the highest
	// aim_priority (ties: the last in the tree). 0 without a sight.
	u8 sight_node(const CTree& tree, u8 selected);

	// A virtual mount on a model that has no bone of that name, from gs_mount_<name>_pos/_rot/_bone/_scale on
	// `section` (model space, metres, degrees H,P,B; follows the bone, default the root; scale for HUD models
	// not at true scale). Keys: see the Gunsmith config reference.
	// False when the section has neither pos nor rot.
	bool read_virtual_mount(LPCSTR section, LPCSTR name, Fmatrix& out, shared_str* bone = nullptr);

	// The child models of one weapon. Two contexts: the world model and the HUD model.
	// A context is rebuilt when the tree's layout (sections, parents, slots) or the root
	// model changes; mode changes only switch the shown mode bones.
	class CVisuals
	{
	public:
		enum
		{
			eWorld = 0,
			eHud = 1
		};

		CVisuals() {}
		~CVisuals() { clear(); }

		void clear();
		void clear(int ctx);

		// `receiver` is the weapon section, `hud_sect` its HUD section (HUD context only; it is
		// checked before the weapon section for virtual mounts and holds gs_aim_bone/gs_aim_point).
		// With `enabled` false the context is emptied.
		void ensure(int ctx, IKinematics* root, const CTree& tree, LPCSTR receiver, LPCSTR hud_sect, bool enabled);

		// Draws the children of a built context on a root model drawn with `root_xform`.
		void render(int ctx, IKinematics* root, const Fmatrix& root_xform);

		// Where shots leave the gun: the fire point of the last muzzle-chain part, if any.
		bool fire_point(int ctx, IKinematics* root, const Fmatrix& root_xform, Fvector& pos);

		// Which sight sight_point measured: its node (0 for the receiver's own sight line), section and mode.
		struct SSightKey
		{
			u8 node = 0xff;
			shared_str section;
			u8 mode = 0;
			bool operator!=(const SSightKey& o) const { return node != o.node || section != o.section || mode != o.mode; }
			LPCSTR name() const { return node == 0 ? "receiver" : section.c_str(); } // for logs
		};
		// HUD: the fitted sight's aim point in the receiver model's space, and a key that changes
		// when a different sight (or the same sight elsewhere) is fitted.
		bool sight_point(IKinematics* root, Fvector& point, SSightKey& key);

		// HUD: the fitted sight's line (aim_dir: forward and up, unit) in the receiver model's space.
		bool sight_dir(IKinematics* root, Fvector& fwd, Fvector& up);

		// A drawn part's frame in the root model's space, its bone's current pose included: points
		// on the part (light_point, ...) map through it. False when that node isn't drawn.
		bool part_frame(int ctx, IKinematics* root, u8 node, Fmatrix& out);

		// HUD: the fitted sight's part data and, when it has aim_eye_point, that point in the
		// receiver model's space. Null without a sight.
		const SPartVisual* sight_eye(IKinematics* root, Fvector& eye, bool& has_eye);

		// Appends each child's model and transform in the root model's space, children's bones
		// recalculated, for drawing the build elsewhere (gunsmith_preview). The caller calculates
		// the root's bones.
		void draw_list(int ctx, IKinematics* root, xr_vector<std::pair<IRenderVisual*, Fmatrix>>& out);

		// Every slot that has a mount_bone, on the root and on each drawn part, in the root model's
		// space (workbench markers): the middle of the part in it, or where a part would mount when
		// it is empty. Arguments as for ensure().
		struct SSlotPoint
		{
			u8 node, slot;
			Fvector pos;
		};
		void slot_points(int ctx, IKinematics* root, const CTree& tree, LPCSTR receiver, LPCSTR hud_sect,
						 xr_vector<SSlotPoint>& out);

	private:
		struct SChild
		{
			u8 node;
			int parent; // index in children, -1 = the root model
			IRenderVisual* visual;
			IKinematics* K;
			u16 mount_bone; // on the parent's model
			Fmatrix mount; // virtual mount (or identity) times the part's mount offset
			Fmatrix local; // in the root model's space
			Fmatrix xform; // world/HUD transform handed to the renderer; must outlive the frame
			const SPartVisual* pv;
			u16 aim_bone, fire_bone;
			xr_vector<u16> mode_bones; // per mode (BI_NONE: not in the model)
			u8 shown_mode;             // the mode whose bone is shown (0xff: none set yet)
		};

		struct SSig
		{
			shared_str section;
			u8 parent, slot;
			bool operator==(const SSig& o) const { return section == o.section && parent == o.parent && slot == o.slot; }
		};

		struct SContext
		{
			IKinematics* root = nullptr;
			shared_str receiver;
			xr_vector<SSig> sig;
			xr_vector<SChild> children;
			bool built = false;
			bool enabled = false; // built with part models (sig empty when not)
			u32 layout_rev = 0; // the tree's layout revision sig was made for
			bool has_root_aim = false;
			u16 root_aim_bone = 0;
			Fvector root_aim;
			// the receiver's own sight line (gs_aim_dir: forward, up), aimed through when no part is a sight
			bool has_root_dir = false;
			Fvector root_fwd, root_up;
		};

		SContext m_ctx[2];
		u8 m_sight_node = 0; // the node the gun aims through (sight_node), set by ensure()
		u8 m_sight_mode = 0; // that node's mode (its sight and mode), set by ensure()
		u32 m_sight_rev = 0; // the tree's state revision m_sight_node and m_sight_mode were read for
		bool m_sight_rev_valid = false;

		void update_local(SContext& c, IKinematics* root);
		void show_modes(SContext& c, const CTree& tree);
		SSightAim aim_of(const SChild& ch) const;
		const SChild* pick_sight(const SContext& c) const;
		// the receiver's own sight line (gs_aim_point + gs_aim_dir) is aimed along when no part is a sight, or the
		// part is an iron sight (aim_priority 0, no magnification): the AK-12's rear sight onto its front post
		bool use_root_line(const SContext& c, const SChild* sight) const;
	};
}
