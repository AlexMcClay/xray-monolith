////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_visuals.cpp
//	Description : Gunsmith part tree - each part rendered as its own child
//	              model, mounted on its parent's model (world and HUD)
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_visuals.h"
#include "gunsmith_config.h"
#include "../Include/xrRender/Kinematics.h"
#include "../Include/xrRender/KinematicsAnimated.h"
#include "../Include/xrRender/RenderVisual.h"

namespace gunsmith
{
	namespace
	{
		Fmatrix make_xform(const Fvector& pos, const Fvector& rot_deg)
		{
			Fvector r = rot_deg;
			r.mul(PI / 180.f);
			Fmatrix m;
			m.setHPB(r.x, r.y, r.z);
			m.translate_over(pos);
			return m;
		}

		Fvector read_vec(LPCSTR section, LPCSTR key)
		{
			Fvector v;
			v.set(0.f, 0.f, 0.f);
			if (section && pSettings->line_exist(section, key))
				v = pSettings->r_fvector3(section, key);
			return v;
		}

		shared_str read_str(LPCSTR section, LPCSTR key)
		{
			if (section && pSettings->line_exist(section, key))
				return pSettings->r_string(section, key);
			return shared_str();
		}

		// `visual` keys may or may not carry the extension (vanilla world visuals usually do).
		bool model_file_exists(const shared_str& path)
		{
			string_path fn;
			const size_t len = xr_strlen(path.c_str());
			if (len > 4 && !_stricmp(path.c_str() + len - 4, ".ogf"))
				xr_strcpy(fn, path.c_str());
			else
				xr_strconcat(fn, path.c_str(), ".ogf");
			return !!FS.exist("$game_meshes$", fn);
		}

		// One log line per section and message.
		void warn_once(LPCSTR section, LPCSTR what, LPCSTR name)
		{
			static xr_set<shared_str> warned;
			string512 key;
			xr_sprintf(key, "%s|%s|%s", section ? section : "", what, name ? name : "");
			if (warned.insert(shared_str(key)).second)
				Msg("! [gunsmith] [%s] %s [%s]", section ? section : "?", what, name ? name : "");
		}
	}

	const SPartVisual& part_visual(LPCSTR section)
	{
		static xr_map<shared_str, SPartVisual> cache;
		const shared_str key(section ? section : "");
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;

		SPartVisual v;
		v.visual = read_str(section, "visual");
		v.hud_visual = read_str(section, "hud_visual");
		v.offset = make_xform(read_vec(section, "mount_offset_pos"), read_vec(section, "mount_offset_rot"));
		v.has_aim = section && (pSettings->line_exist(section, "aim_bone") || pSettings->line_exist(section, "aim_point"));
		v.aim_bone = read_str(section, "aim_bone");
		v.aim_priority = section ? READ_IF_EXISTS(pSettings, r_s32, section, "aim_priority", 0) : 0;
		v.aim_point = read_vec(section, "aim_point");
		v.has_fire = section && (pSettings->line_exist(section, "fire_bone") || pSettings->line_exist(section, "fire_point"));
		v.fire_bone = read_str(section, "fire_bone");
		v.fire_point = read_vec(section, "fire_point");
		v.magnification = section ? READ_IF_EXISTS(pSettings, r_float, section, "magnification", 0.f) : 0.f;
		v.magnification_min = section ? READ_IF_EXISTS(pSettings, r_float, section, "magnification_min", 0.f) : 0.f;
		v.zoom_factor = section ? READ_IF_EXISTS(pSettings, r_float, section, "scope_zoom_factor", 0.f) : 0.f;
		v.zoom_steps = section ? READ_IF_EXISTS(pSettings, r_u32, section, "zoom_step_count", 0) : 0;
		v.scope_texture = read_str(section, "scope_texture");
		v.has_eye = section && pSettings->line_exist(section, "aim_eye_point");
		v.eye_point = read_vec(section, "aim_eye_point");
		v.eye_offset = section ? READ_IF_EXISTS(pSettings, r_float, section, "aim_eye_offset", 0.f) : 0.f;
		// "light | light, laser | laser": one entry per mode, each a comma list of what is on
		if (section && pSettings->line_exist(section, "tactical_modes"))
		{
			LPCSTR modes = pSettings->r_string(section, "tactical_modes");
			const int count = _GetItemCount(modes, '|');
			string256 mode, what;
			for (int m = 0; m < count; ++m)
			{
				_GetItem(modes, m, mode, '|');
				u8 flags = 0;
				for (int w = 0, wc = _GetItemCount(mode, ','); w < wc; ++w)
				{
					_Trim(_GetItem(mode, w, what, ','));
					if (!xr_strcmp(what, "light"))
						flags |= SPartVisual::eTacLight;
					else if (!xr_strcmp(what, "laser"))
						flags |= SPartVisual::eTacLaser;
				}
				if (flags)
					v.tac_modes.push_back(flags);
			}
		}
		v.light_point = read_vec(section, "light_point");
		v.laser_point = read_vec(section, "laser_point");
		v.light_section = read_str(section, "light_section");
		v.laser_section = read_str(section, "laser_section");
		return cache.insert(mk_pair(key, v)).first->second;
	}

	bool read_virtual_mount(LPCSTR section, LPCSTR name, Fmatrix& out, shared_str* bone)
	{
		if (!section || !name || !pSettings->section_exist(section))
			return false;
		string256 pos_key, rot_key, bone_key, scale_key;
		xr_sprintf(pos_key, "gs_mount_%s_pos", name);
		xr_sprintf(rot_key, "gs_mount_%s_rot", name);
		xr_sprintf(bone_key, "gs_mount_%s_bone", name);
		xr_sprintf(scale_key, "gs_mount_%s_scale", name);
		if (!pSettings->line_exist(section, pos_key) && !pSettings->line_exist(section, rot_key))
			return false;
		out = make_xform(read_vec(section, pos_key), read_vec(section, rot_key));
		const float scale = READ_IF_EXISTS(pSettings, r_float, section, scale_key, 1.f);
		if (!fsimilar(scale, 1.f))
		{
			Fmatrix s;
			s.scale(scale, scale, scale);
			out.mulB_43(s);
		}
		if (bone)
			*bone = read_str(section, bone_key);
		return true;
	}

	namespace
	{
		// The mount `name` on a node's model: a bone of that name, else a virtual mount on the
		// owner's section (for the receiver on the HUD, its hud section first). `mount` is
		// relative to `bone`. `warn` logs a missing mount once.
		void resolve_mount(IKinematics* K, const shared_str& name, int ctx, bool on_receiver, LPCSTR owner,
						   LPCSTR hud_sect, bool warn, Fmatrix& mount, u16& bone)
		{
			mount.identity();
			bone = K->LL_BoneID(name);
			if (bone != BI_NONE)
				return;
			shared_str follow;
			const bool found = (on_receiver && ctx == CVisuals::eHud && read_virtual_mount(hud_sect, name.c_str(), mount, &follow)) ||
				read_virtual_mount(owner, name.c_str(), mount, &follow);
			if (!found && warn)
				warn_once(owner, "has no mount bone or gs_mount_<name>_pos for", name.c_str());
			bone = follow.size() ? K->LL_BoneID(follow) : BI_NONE;
			if (follow.size() && bone == BI_NONE && warn)
				warn_once(owner, "has no bone for gs_mount_<name>_bone", follow.c_str());
			if (bone == BI_NONE)
				bone = K->LL_GetBoneRoot();
		}
	}

	bool mount_resolves(IKinematics* model, LPCSTR name, LPCSTR section, LPCSTR section2)
	{
		Fmatrix m;
		if (model && model->LL_BoneID(name) != BI_NONE)
			return true;
		return read_virtual_mount(section, name, m) || read_virtual_mount(section2, name, m);
	}

	void CVisuals::clear()
	{
		clear(eWorld);
		clear(eHud);
	}

	void CVisuals::clear(int ctx)
	{
		SContext& c = m_ctx[ctx];
		for (SChild& ch : c.children)
			if (ch.visual)
				::Render->model_Delete(ch.visual);
		c = SContext();
	}

	u8 sight_node(const CTree& tree, u8 selected)
	{
		if (selected > 0 && selected < tree.size() && is_cyclable_sight(part_visual(tree.node(selected).section.c_str())))
			return selected;
		u8 best = 0;
		int best_priority = 0;
		for (u8 i = 1; i < tree.size(); ++i)
		{
			const SPartVisual& pv = part_visual(tree.node(i).section.c_str());
			if (pv.has_aim && (!best || pv.aim_priority >= best_priority))
			{
				best = i; // highest aim_priority; among equals the last in the tree
				best_priority = pv.aim_priority;
			}
		}
		return best;
	}

	void CVisuals::ensure(int ctx, IKinematics* root, const CTree& tree, LPCSTR receiver, LPCSTR hud_sect, bool enabled)
	{
		SContext& c = m_ctx[ctx];
		m_sight_node = sight_node(tree, selected_sight(tree)); // a switch changes no layout: read it every time

		xr_vector<SSig> sig;
		if (enabled)
		{
			sig.reserve(tree.size());
			for (u32 i = 0; i < tree.size(); ++i)
			{
				const SNode& n = tree.node(u8(i));
				sig.push_back({ n.section, n.parent, n.slot });
			}
		}
		if (c.built && c.root == root && c.receiver == receiver && c.sig == sig)
			return;

		clear(ctx);
		c.built = true;
		c.root = root;
		c.receiver = receiver;
		c.sig.swap(sig);
		if (!enabled || !root || tree.size() < 2)
			return;

		// The receiver's own aim point (HUD only).
		if (ctx == eHud && hud_sect && (pSettings->line_exist(hud_sect, "gs_aim_point") || pSettings->line_exist(hud_sect, "gs_aim_bone")))
		{
			c.has_root_aim = true;
			c.root_aim = read_vec(hud_sect, "gs_aim_point");
			const shared_str bone = read_str(hud_sect, "gs_aim_bone");
			c.root_aim_bone = bone.size() ? root->LL_BoneID(bone) : BI_NONE;
			if (c.root_aim_bone == BI_NONE)
			{
				if (bone.size())
					warn_once(hud_sect, "has no aim bone", bone.c_str());
				c.root_aim_bone = root->LL_GetBoneRoot();
			}
		}

		int child_of[GS_MAX_NODES];
		for (int& v : child_of)
			v = -1;

		SLOTS slots;
		for (u32 i = 1; i < tree.size() && i < GS_MAX_NODES; ++i)
		{
			const SNode& n = tree.node(u8(i));
			const int parent_child = n.parent == 0 ? -1 : child_of[n.parent];
			if (n.parent != 0 && parent_child < 0)
				continue; // the parent has no model to mount on
			IKinematics* parentK = parent_child < 0 ? root : c.children[parent_child].K;

			tree.node_slots(n.parent, slots);
			if (n.slot >= slots.size())
				continue;
			const SSlotInfo& slot = slots[n.slot];
			if (!slot.mount_bone.size())
				continue; // drawn the old way (a baked addon bone) or not at all

			const SPartVisual& pv = part_visual(n.section.c_str());
			const shared_str& path = ctx == eHud ? pv.hud_visual : pv.visual;
			if (!path.size())
			{
				if (ctx == eHud)
					warn_once(n.section.c_str(), "has no hud_visual", "");
				continue;
			}
			if (!model_file_exists(path))
			{
				warn_once(n.section.c_str(), "model file not found", path.c_str());
				continue;
			}

			// The mount: a bone of that name, else a virtual mount on the owner's section.
			Fmatrix mount;
			u16 bone;
			const bool on_receiver = n.parent == 0;
			resolve_mount(parentK, slot.mount_bone, ctx, on_receiver, on_receiver ? receiver : tree.node(n.parent).section.c_str(),
						  hud_sect, true, mount, bone);

			const bool was_hud = ::Render->hud_loading;
			::Render->hud_loading = ctx == eHud;
			IRenderVisual* visual = ::Render->model_Create(path.c_str());
			::Render->hud_loading = was_hud;
			IKinematics* K = visual ? visual->dcast_PKinematics() : nullptr;
			if (!K)
			{
				if (visual)
					::Render->model_Delete(visual);
				warn_once(n.section.c_str(), "model is not skeletal", path.c_str());
				continue;
			}
			K->CalculateBones_Invalidate();
			K->CalculateBones(TRUE);

			SChild ch;
			ch.node = u8(i);
			ch.parent = parent_child;
			ch.visual = visual;
			ch.K = K;
			ch.mount_bone = bone;
			ch.mount.mul_43(mount, pv.offset);
			ch.local.identity();
			ch.xform.identity();
			ch.pv = &pv;
			ch.aim_bone = ch.fire_bone = K->LL_GetBoneRoot();
			if (pv.aim_bone.size())
			{
				const u16 b = K->LL_BoneID(pv.aim_bone);
				if (b != BI_NONE)
					ch.aim_bone = b;
				else
					warn_once(n.section.c_str(), "has no aim bone", pv.aim_bone.c_str());
			}
			if (pv.fire_bone.size())
			{
				const u16 b = K->LL_BoneID(pv.fire_bone);
				if (b != BI_NONE)
					ch.fire_bone = b;
				else
					warn_once(n.section.c_str(), "has no fire bone", pv.fire_bone.c_str());
			}
			child_of[i] = int(c.children.size());
			c.children.push_back(ch);
		}
	}

	void CVisuals::update_local(SContext& c, IKinematics* root)
	{
		for (SChild& ch : c.children)
		{
			IKinematics* parentK = ch.parent < 0 ? root : c.children[ch.parent].K;
			Fmatrix t;
			// Model space, following the bone's motion: LL_GetTransform_R is the bone's pose times its
			// inverse bind pose, so mounts don't depend on how the bone itself is oriented at rest.
			if (ch.parent < 0)
				t.set(parentK->LL_GetTransform_R(ch.mount_bone));
			else
				t.mul_43(c.children[ch.parent].local, parentK->LL_GetTransform_R(ch.mount_bone));
			ch.local.mul_43(t, ch.mount);
		}
	}

	void CVisuals::render(int ctx, IKinematics* root, const Fmatrix& root_xform)
	{
		SContext& c = m_ctx[ctx];
		if (c.children.empty() || c.root != root)
			return;
		update_local(c, root);
		for (SChild& ch : c.children)
		{
			IKinematicsAnimated* ka = ch.visual->dcast_PKinematicsAnimated();
			if (ka)
			{
				ka->UpdateTracks();
				ch.K->CalculateBones_Invalidate();
				ch.K->CalculateBones(TRUE);
			}
			ch.xform.mul_43(root_xform, ch.local);
			::Render->set_Transform(&ch.xform);
			::Render->add_Visual(ch.visual);
		}
	}

	void CVisuals::layout(int ctx, IKinematics* root, xr_vector<std::pair<u8, Fmatrix>>& out)
	{
		out.clear();
		SContext& c = m_ctx[ctx];
		if (c.children.empty() || c.root != root)
			return;
		root->CalculateBones_Invalidate();
		root->CalculateBones(TRUE);
		update_local(c, root);
		for (const SChild& ch : c.children)
			out.push_back(std::make_pair(ch.node, ch.local));
	}

	void CVisuals::draw_list(int ctx, IKinematics* root, xr_vector<std::pair<IRenderVisual*, Fmatrix>>& out)
	{
		SContext& c = m_ctx[ctx];
		if (c.children.empty() || c.root != root)
			return;
		update_local(c, root);
		for (const SChild& ch : c.children)
		{
			ch.K->CalculateBones_Invalidate();
			ch.K->CalculateBones(TRUE);
			out.push_back(std::make_pair(ch.visual, ch.local));
		}
	}

	void CVisuals::slot_points(int ctx, IKinematics* root, const CTree& tree, LPCSTR receiver, LPCSTR hud_sect,
							   xr_vector<SSlotPoint>& out)
	{
		out.clear();
		SContext& c = m_ctx[ctx];
		if (!root || c.root != root)
			return;
		update_local(c, root);
		SLOTS slots;
		for (u32 i = 0; i < tree.size() && i < GS_MAX_NODES; ++i)
		{
			// The node's model and its transform in the root's space.
			IKinematics* K = nullptr;
			Fmatrix local;
			if (i == 0)
			{
				K = root;
				local.identity();
			}
			else
			{
				for (const SChild& ch : c.children)
					if (ch.node == i)
					{
						K = ch.K;
						local = ch.local;
						break;
					}
			}
			if (!K)
				continue; // a part without a model: its slots have nowhere to show

			tree.node_slots(u8(i), slots);
			LPCSTR owner = i == 0 ? receiver : tree.node(u8(i)).section.c_str();
			for (u32 s = 0; s < slots.size(); ++s)
			{
				if (!slots[s].mount_bone.size())
					continue;
				Fmatrix mount, m;
				u16 bone;
				resolve_mount(K, slots[s].mount_bone, ctx, i == 0, owner, hud_sect, false, mount, bone);
				m.mul_43(local, K->LL_GetTransform_R(bone));
				m.mulB_43(mount);
				SSlotPoint p;
				p.node = u8(i);
				p.slot = u8(s);
				p.pos = m.c; // an empty slot: where a part would go
				// A filled slot whose part is drawn: on the part (the middle of its model), not on the mount
				// point inside the parent, which drifts away from the part as the view zooms in.
				const int occupant = tree.find_in_slot(u8(i), u8(s));
				for (const SChild& ch : c.children)
					if (occupant >= 0 && ch.node == u8(occupant) && ch.visual)
					{
						Fvector centre;
						ch.visual->getVisData().box.getcenter(centre);
						ch.local.transform_tiny(p.pos, centre);
						break;
					}
				out.push_back(p);
			}
		}
	}

	bool CVisuals::fire_point(int ctx, IKinematics* root, const Fmatrix& root_xform, Fvector& pos)
	{
		SContext& c = m_ctx[ctx];
		if (c.children.empty() || c.root != root)
			return false;
		const SChild* muzzle = nullptr;
		for (const SChild& ch : c.children)
			if (ch.pv->has_fire)
				muzzle = &ch; // children follow their parents, so the last one ends the chain
		if (!muzzle)
			return false;
		update_local(c, root);
		Fvector p;
		muzzle->K->LL_GetTransform_R(muzzle->fire_bone).transform_tiny(p, muzzle->pv->fire_point);
		muzzle->local.transform_tiny(p);
		root_xform.transform_tiny(pos, p);
		return true;
	}

	const CVisuals::SChild* CVisuals::pick_sight(const SContext& c) const
	{
		// The weapon's choice (sight_node: the selected sight, or the best one), as CWeapon::gs_sight_part
		for (const SChild& ch : c.children)
			if (ch.node == m_sight_node && ch.pv->has_aim)
				return &ch;
		const SChild* sight = nullptr;
		for (const SChild& ch : c.children)
			if (ch.pv->has_aim && (!sight || ch.pv->aim_priority >= sight->pv->aim_priority))
				sight = &ch; // highest aim_priority; among equals the last in the tree
		return sight;
	}

	bool CVisuals::sight_point(IKinematics* root, Fvector& point, shared_str& key)
	{
		SContext& c = m_ctx[eHud];
		if (c.children.empty() || c.root != root)
			return false;
		const SChild* sight = pick_sight(c);
		if (!sight)
			return false;
		update_local(c, root);
		sight->K->LL_GetTransform_R(sight->aim_bone).transform_tiny(point, sight->pv->aim_point);
		sight->local.transform_tiny(point);
		string256 k;
		xr_sprintf(k, "%u:%s", u32(sight->node), c.sig[sight->node].section.c_str());
		key = k;
		return true;
	}

	bool CVisuals::part_frame(int ctx, IKinematics* root, u8 node, Fmatrix& out)
	{
		SContext& c = m_ctx[ctx];
		if (c.children.empty() || c.root != root)
			return false;
		for (const SChild& ch : c.children)
		{
			if (ch.node != node)
				continue;
			update_local(c, root);
			out.mul_43(ch.local, ch.K->LL_GetTransform_R(ch.K->LL_GetBoneRoot()));
			return true;
		}
		return false;
	}

	const SPartVisual* CVisuals::sight_eye(IKinematics* root, Fvector& eye, bool& has_eye)
	{
		has_eye = false;
		SContext& c = m_ctx[eHud];
		if (c.children.empty() || c.root != root)
			return nullptr;
		const SChild* sight = pick_sight(c);
		if (!sight)
			return nullptr;
		if (sight->pv->has_eye)
		{
			update_local(c, root);
			sight->K->LL_GetTransform_R(sight->aim_bone).transform_tiny(eye, sight->pv->eye_point);
			sight->local.transform_tiny(eye);
			has_eye = true;
		}
		return sight->pv;
	}

	bool CVisuals::aim_delta(IKinematics* root, Fvector& delta)
	{
		SContext& c = m_ctx[eHud];
		if (!c.has_root_aim || c.children.empty() || c.root != root)
			return false;
		const SChild* sight = pick_sight(c);
		if (!sight)
			return false;
		update_local(c, root);

		Fvector receiver_aim, sight_aim;
		root->LL_GetTransform_R(c.root_aim_bone).transform_tiny(receiver_aim, c.root_aim);
		sight->K->LL_GetTransform_R(sight->aim_bone).transform_tiny(sight_aim, sight->pv->aim_point);
		sight->local.transform_tiny(sight_aim);
		delta.sub(receiver_aim, sight_aim);
		return true;
	}
}
