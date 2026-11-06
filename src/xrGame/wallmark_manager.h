#pragma once


// Igor DEFINE_VECTOR(ref_shader, SHADER_VECTOR, SHADER_VECTOR_IT);
class CObject;
class CWalmarkManager
{
private:
	FactoryPtr<IWallMarkArray> m_wallmarks;
	Fvector m_pos;
public:
	CObject* m_owner;
	CWalmarkManager();
	~CWalmarkManager();
	void Load(LPCSTR section);
	void Clear();
	void AddWallmark(const Fvector& dir, const Fvector& start_pos, float range, float wallmark_size,
	                 IWallMarkArray& wallmarks_vector, int t);
	void PlaceWallmarks(const Fvector& start_pos);

	void __stdcall StartWorkflow();
};

// Decals on moving objects: optional [wallmarks] keys in system.ltx, read once. Defaults keep vanilla behaviour.
//   dynamic_objects = false : blood splatter and drops, grenade marks and wallmarks_manager():place() also mark
//                             dynamic objects with a kinematic visual (cars, crates, doors, barrels, bodies) through
//                             the skeleton wallmark path, not only level geometry. Bullets already do.
//   object_ttl = 0          : lifetime (s) of marks on objects that aren't creatures (props, cars, doors) placed
//                             without their own ttl; 0 = the skeleton default (r__wallmark_ttl / [wallmarks] skeleton_ttl)
struct SWallmarkGameSettings
{
	bool dynamic_objects;
	float object_ttl;
};
const SWallmarkGameSettings& wallmark_game_settings();

// ttl for a mark on O: object_ttl if set and O isn't a creature, else 0 (= the engine's default)
float object_wallmark_ttl(CObject* O);

// Put one of `marks` on dynamic object O where the ray from `start` along `dir` meets its bones (skeleton wallmark).
// `range` = distance from start to the hit (the ray starts just before it, as for bullets). Respects the bone's
// material: flSuppressWallmarks, and flBloodmark when `blood`. False if O can't take a mark (no bones, the player).
bool place_object_wallmark(CObject* O, IWallMarkArray* marks, const Fvector& start, const Fvector& dir, float range,
                           float size, float ttl = 0.f, bool ignore_opt = false, bool blood = false, u16 element = u16(-1));
