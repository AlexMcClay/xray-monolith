#include "stdafx.h"
#include "dxWallMarkArray.h"

#include "dxUIShader.h"

void dxWallMarkArray::Copy(IWallMarkArray& _in)
{
	*this = *(dxWallMarkArray*)&_in;
}

dxWallMarkArray::~dxWallMarkArray()
{
	for (ShaderIt it = m_CollideMarks.begin(); it != m_CollideMarks.end(); ++it)
		it->destroy();
}

#if defined(USE_DX10) || defined(USE_DX11)
// Optional [wallmark_shaders] section of system.ltx: `texture name prefix = shader`. A mark whose texture name
// starts with a listed prefix (case ignored; first match in file order) is drawn with that shader instead of
// effects\wallmark, e.g. `tkvfx\wm\pbr\ = effects\tkvfx_wallmark_pbr` for G-buffer decals. No section = vanilla.
static LPCSTR mapped_wallmark_shader(LPCSTR texture)
{
	static xr_vector<std::pair<shared_str, shared_str>> map;
	static bool loaded = false;
	if (!loaded)
	{
		loaded = true;
		LPCSTR S = "wallmark_shaders";
		if (pSettings->section_exist(S))
		{
			u32 n = pSettings->line_count(S);
			for (u32 i = 0; i < n; i++)
			{
				LPCSTR prefix, shader;
				if (pSettings->r_line(S, i, &prefix, &shader) && prefix && prefix[0] && shader && shader[0])
					map.push_back(std::make_pair(shared_str(prefix), shared_str(shader)));
			}
		}
	}
	for (u32 i = 0; i < map.size(); i++)
		if (0 == _strnicmp(texture, *map[i].first, map[i].first.size()))
			return *map[i].second;
	return NULL;
}
#endif

void dxWallMarkArray::AppendMark(LPCSTR s_textures)
{
	ref_shader s;

#if defined(USE_DX10) || defined(USE_DX11)
	if (LPCSTR mapped = mapped_wallmark_shader(s_textures))
	{
		s.create(mapped, s_textures);
		m_CollideMarks.push_back(s);
		return;
	}
#endif

#if defined(USE_DX11)

	LPCSTR sh_name = "effects\\wallmark";

	if (RImplementation.o.ssfx_blood)
	{
		// Use the blood shader for any texture with the name wm_blood_*
		if (strstr(s_textures, "wm_blood_"))
			sh_name = "effects\\wallmark_blood";
	}

	s.create(sh_name, s_textures);

#else
	s.create("effects\\wallmark", s_textures);
#endif

	m_CollideMarks.push_back(s);
}

void dxWallMarkArray::clear()
{
	return m_CollideMarks.clear();
}

bool dxWallMarkArray::empty()
{
	return m_CollideMarks.empty();
}

wm_shader dxWallMarkArray::GenerateWallmark()
{
	wm_shader res;
	if (!m_CollideMarks.empty())
		((dxUIShader*)&*res)->hShader = m_CollideMarks[::Random.randI(0, m_CollideMarks.size())];
	return res;
}

ref_shader* dxWallMarkArray::dxGenerateWallmark()
{
	return m_CollideMarks.empty() ? NULL : &m_CollideMarks[::Random.randI(0, m_CollideMarks.size())];
}
