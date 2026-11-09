#pragma once
#include "script_export_space.h"
#include "script_game_object.h"

struct ScriptWallmarksArray
{
	FactoryPtr<IWallMarkArray>* m_wallmarks;
	LPCSTR m_section;

	ScriptWallmarksArray(LPCSTR section);
};

class ScriptWallmarksManager
{
private:
	xr_vector<ScriptWallmarksArray*> m_script_wallmarks;

public:
	ScriptWallmarksManager();
	~ScriptWallmarksManager();

	IWallMarkArray* FindSection(LPCSTR section);
	void PlaceWallmark(Fvector dir, Fvector start_pos,
		float trace_dist, float wallmark_size, LPCSTR section,
		CScriptGameObject* ignore_obj, float ttl);
	void PlaceWallmark(Fvector dir, Fvector start_pos,
		float trace_dist, float wallmark_size, LPCSTR section,
		CScriptGameObject* ignore_obj, float ttl, bool random_rotation = true);
	void PlaceWallmark(Fvector dir, Fvector start_pos,
		float trace_dist, float wallmark_size, LPCSTR section,
		CScriptGameObject* ignore_obj, float ttl, float rotation);
	// as place (with a rotation), but the mark spreads: grow_from x size at first, full size after grow_time s
	void PlaceWallmarkGrowing(Fvector dir, Fvector start_pos,
		float trace_dist, float wallmark_size, LPCSTR section,
		CScriptGameObject* ignore_obj, float ttl, float rotation, float grow_time, float grow_from);
	void PlaceSkeletonWallmark(CScriptGameObject* obj, LPCSTR section, Fvector start,
		Fvector dir, float size, float ttl);
};

typedef class_exporter<ScriptWallmarksManager> CScriptWallmarksManager;
