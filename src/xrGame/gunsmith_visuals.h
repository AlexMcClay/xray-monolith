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
		bool has_fire2; // `fire_point2`: a launcher part's muzzle (the weapon's second fire point), on the part's root
		Fvector fire_point2;
		// `aim_point2`, `aim_dir2`: a launcher part's own sight (its eye point and line, as aim_point and aim_dir), on
		// the part's root: aimed along in the weapon's grenade mode
		bool has_aim2;
		Fvector aim_point2, aim_fwd2, aim_up2;
		// `gs_follow_bones`: bones of the part's model that move as the root model's (the receiver's) bones of the
		// same names: a launcher's tube, trigger and leaf sight, its grenade, played by the gun's motions
		xr_vector<shared_str> follow_bones;
		float magnification, magnification_min, zoom_factor;
		u32 zoom_steps;
		shared_str scope_texture;
		bool has_eye;
		Fvector eye_point;
		float eye_offset;
		float eye_shift; // aim_eye_shift: an eyecup moves the eye of the optic it is fitted on back by this (metres)
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

		// Tactical devices: per mode what shines (infrared ones only show to night vision) and, from
		// tactical_mode_<n>_light / _laser / _light_point / _laser_point (else light_section, light_point...),
		// the light and laser definitions and where they come out, in the model's space.
		enum { eTacLight = 1, eTacLaser = 2, eTacIrLight = 4, eTacIrLaser = 8, eTacAnyLight = 5, eTacAnyLaser = 10 };
		struct STacMode
		{
			shared_str text; // the mode as the ini reads it (no spaces: "light,laser", "ir_light", "light_high")
			shared_str light, laser;
			Fvector light_point, laser_point;
			Fvector light_lens, laser_lens; // where each lens glows (tactical_mode_<n>_light_lens...; else the point)
		};
		xr_vector<u8> tac_modes; // per mode: eTac* flags
		xr_vector<STacMode> tac_mode_data; // per mode
		bool is_tactical() const { return !tac_modes.empty(); }

		// The left hand on the part (grip IK, gunsmith_grip_ik.h), per Tarkov GripType 0 and 1: the left hand
		// bone in the model's space and the fingers' rotations relative to their parents (grip_hand_<type>,
		// grip_fingers_<type>, grip_priority, grip_type; see the Gunsmith config reference).
		bool has_grip[2];
		Fmatrix grip_hand[2];
		Fvector grip_fingers[2][15]; // radians
		int grip_priority;
		int grip_type;
		// gs_rounds: the part's round mounts (a magazine's patron_<n>, each a gs_mount_<name>_*), see SRoundShow
		xr_vector<shared_str> round_mounts;
		// The flags of mode `mode` (clamped to the last mode).
		u8 tac_flags(u8 mode) const { return tac_modes.empty() ? 0 : tac_modes[_min(u32(mode), u32(tac_modes.size() - 1))]; }
		const STacMode& tac_mode(u8 mode) const { return tac_mode_data[_min(u32(mode), u32(tac_mode_data.size() - 1))]; }
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

	// The rounds a gun shows in first person (Gunsmith Docs animations-and-sounds.md, "Rounds in the gun"), as Tarkov:
	// on a part's round mounts (gs_rounds on the part: a magazine's patron_<n> holds a round while it shows n or more;
	// the HUD section's gs_round_mag the same on the receiver, a tube's round at its mouth), the HUD section's chambers
	// (gs_round_chamber) and ejection ports (gs_round_port: mount names, several for a cylinder or a double barrel).
	// Each is the model its ammo section names, gs_round_visual for a round, gs_round_case_visual for a fired case
	// (first-person copies in <folder>\hud\), made in the mount's frame.
	struct SRoundShow
	{
		enum { eNone = 0, eRound = 1, eCase = 2 };
		u32 mag = 0;                    // the rounds the magazine shows
		xr_vector<shared_str> mag_sect; // their ammo sections, top first (past the end: the last one)
		xr_vector<u8> chambers, ports;  // per mount, as the HUD section lists them (past the end: eNone)
		shared_str chamber_sect, port_sect;
	};

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
		// The second fire point (grenades): the last part with a `fire_point2`, if any.
		bool fire_point2(int ctx, IKinematics* root, const Fmatrix& root_xform, Fvector& pos);

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

		// HUD: a bone of the fitted parts' first-person models shown or hidden (all parts with a bone of that name;
		// kept for parts fitted later): the Framework's launcher grenade, in the hand or gone
		void set_hud_bone_hidden(const shared_str& bone, bool hidden);

		// HUD: the launcher's own sight (the last part with an aim_point2) in the receiver model's space: its eye
		// point, its line (unit) and a key for it (mode 0x80).
		bool sight2(IKinematics* root, Fvector& point, Fvector& fwd, Fvector& up, SSightKey& key);

		// A drawn part's frame in the root model's space, its bone's current pose included: points
		// on the part (light_point, ...) map through it. False when that node isn't drawn.
		bool part_frame(int ctx, IKinematics* root, u8 node, Fmatrix& out);

		// HUD: the fitted sight's part data and, when it is an optic with aim_eye_point, where the eye goes in
		// the receiver model's space (z is the gun's forward axis): the eye point, moved back by a fitted
		// eyecup's aim_eye_shift, and no further forward than another fitted optic's eye point (the head stops
		// at its eyepiece) or the HUD section's gs_aim_eye_far. Null without a sight.
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

		// HUD: whether the build has round mounts (a part's or the HUD section's) and a chamber among them; and the
		// rounds `show` says drawn on them (after render(), the same root and transform).
		bool has_rounds() const;
		bool has_chamber() const { return !m_ctx[eHud].round_chambers.empty(); }
		u32 chamber_count() const { return u32(m_ctx[eHud].round_chambers.size()); }
		u32 port_count() const { return u32(m_ctx[eHud].round_ports.size()); }
		void render_rounds(IKinematics* root, const Fmatrix& root_xform, const SRoundShow& show);

		// Prototype (research/44 phase 2): each drawn part as a script attachment would need it: its model, the part
		// it hangs on (index in this list, -1 the root model), the parent model's bone and the offset on that bone's
		// own transform (script_attachment: parent x bone x offset), and the bones it has hidden (sight modes). The
		// offset is the bone's inverse bind pose times the mount, so it doesn't change as the bone moves.
		struct SAttMount
		{
			u8 node = 0;
			int parent = -1;
			shared_str model, bone;
			Fmatrix offset;
			xr_vector<shared_str> hidden;
			bool follow = false; // has gs_follow_bones (not carried over by the prototype)
		};
		void attachment_mounts(int ctx, IKinematics* root, xr_vector<SAttMount>& out) const;

	private:
		struct SRoundSlot
		{
			int child = -1;     // the part it is on (index in children), -1: the root model
			u16 bone = 0;       // on that model
			Fmatrix mount;      // in the bone's frame (model space)
			u32 number = 0;     // patron_<n>: shown while the magazine shows n or more
			IRenderVisual* visual = nullptr;
			shared_str path;    // the model asked for (visual: it, or null when it didn't load)
			Fmatrix xform;      // handed to the renderer; must outlive the frame
		};
		struct SContext;
		// A part bone moving as a root bone (gs_follow_bones): the root bone's motion relative to its parent (the
		// part's mount moves with that), turned about the part bone's own pivot (a GP-25 on clips made for a GP-34)
		struct SFollow
		{
			SContext* c;
			u8 node;          // the part's node (its child is found by it: the children vector moves)
			IKinematics* root;
			u16 root_bone, root_parent;
			Fvector root_pivot; // the root bone's rest position in the root model's space
		};
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
			xr_vector<SFollow*> follow; // owned (freed by clear); a callback's parameter must not move
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
			// gs_aim_eye_far: the furthest forward (receiver model z) the eye goes behind an optic
			bool has_eye_far = false;
			float eye_far = 0.f;
			// round mounts (HUD): the receiver's (gs_round_mag) and the parts' in tree order, each part's as gs_rounds
			// lists them; the chambers and ports
			xr_vector<SRoundSlot> mag_rounds;
			xr_vector<SRoundSlot> round_chambers, round_ports;
		};

		SContext m_ctx[2];
		xr_vector<shared_str> m_hud_hidden; // set_hud_bone_hidden
		void apply_hidden(SChild& ch);
		static void follow_callback(CBoneInstance* B);
		u8 m_sight_node = 0; // the node the gun aims through (sight_node), set by ensure()
		u8 m_sight_mode = 0; // that node's mode (its sight and mode), set by ensure()
		u32 m_sight_rev = 0; // the tree's state revision m_sight_node and m_sight_mode were read for
		bool m_sight_rev_valid = false;

		void update_local(SContext& c, IKinematics* root);
		void show_modes(SContext& c, const CTree& tree);
		void build_rounds(SContext& c, IKinematics* root, LPCSTR receiver, LPCSTR hud_sect);
		void draw_round(SContext& c, IKinematics* root, const Fmatrix& root_xform, SRoundSlot& s, const shared_str& path);
		SSightAim aim_of(const SChild& ch) const;
		const SChild* pick_sight(const SContext& c) const;
		// the receiver's own sight line (gs_aim_point + gs_aim_dir) is aimed along when no part is a sight, or the
		// part is an iron sight (aim_priority 0, no magnification): the AK-12's rear sight onto its front post
		bool use_root_line(const SContext& c, const SChild* sight) const;
		// An optic's eye point in the root model's space, moved back along its sight line by the aim_eye_shift of
		// the first part below it in the tree that has one (its eyecup). The context's local transforms are current.
		void optic_eye(const SContext& c, const SChild& optic, const SSightAim& aim, Fvector& eye) const;
	};
}
