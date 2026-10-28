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
	// Visual keys of a part section, cached per section.
	//   visual, hud_visual                 model paths without .ogf (world, first person)
	//   mount_offset_pos, mount_offset_rot applied after the mount (metres; degrees H,P,B)
	//   aim_bone, aim_point                the part is a sight: the point the eye lines up with when
	//                                      aiming (model space, moving with that bone, default the root)
	//   fire_bone, fire_point              the part is on the muzzle chain: shots and flash start
	//                                      there when it is the last such part in the tree
	// All positions are in the model's own space (as exported) and follow the named bone's
	// motion; how a bone is oriented at rest doesn't matter.
	struct SPartVisual
	{
		shared_str visual, hud_visual;
		Fmatrix offset;
		bool has_aim;
		shared_str aim_bone;
		Fvector aim_point;
		bool has_fire;
		shared_str fire_bone;
		Fvector fire_point;
	};

	const SPartVisual& part_visual(LPCSTR section);

	// A virtual mount on a model that has no bone of that name, from keys on `section`:
	//   gs_mount_<name>_pos, gs_mount_<name>_rot   in the model's space (metres; degrees H,P,B)
	//   gs_mount_<name>_bone                        bone whose motion it follows (default the root
	//                                               bone), e.g. the magazine bone an animation moves
	//   gs_mount_<name>_scale                       uniform scale for the part and everything on it
	//                                               (default 1), for HUD models not at true scale
	// False when the section has neither pos nor rot.
	bool read_virtual_mount(LPCSTR section, LPCSTR name, Fmatrix& out, shared_str* bone = nullptr);

	// Whether a slot's mount resolves on a model: a real bone, or a virtual mount on one of
	// the sections (either may be null).
	bool mount_resolves(IKinematics* model, LPCSTR name, LPCSTR section, LPCSTR section2);

	// The child models of one weapon. Two contexts: the world model and the HUD model.
	// A context is rebuilt when the tree's layout (sections, parents, slots) or the root
	// model changes; condition changes don't rebuild anything.
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

		// HUD: the receiver's aim point minus the fitted sight's aim point, in the receiver
		// model's space. False without a receiver aim point or a sight part.
		bool aim_delta(IKinematics* root, Fvector& delta);

		bool has_children(int ctx) const { return !m_ctx[ctx].children.empty(); }

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
			bool has_root_aim = false;
			u16 root_aim_bone = 0;
			Fvector root_aim;
		};

		SContext m_ctx[2];

		void update_local(SContext& c, IKinematics* root);
	};
}
