////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_icon_cache.h
//	Description : Gunsmith icon disk cache - drawn inventory icons kept as files
//	              between levels and sessions, so a known build needs no part models
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "gunsmith_tree.h"

namespace gunsmith
{
	// Files in appdata\gunsmith_icons\, one per icon: <name>.gsi (a small header, then the cell's pixels packed with
	// rtc_compress). A name is a hash of everything that changes the picture: the build's signature, the cell size
	// and side, the file times of every part's model and of the preview shaders, the format and icon_cache_version.
	// So a re-exported model, a shader change or a new version number makes new names; the old files age out (the
	// folder is kept under icon_cache_mb, oldest first). [gunsmith_settings] icon_cache = false turns it off.
	namespace icon_cache
	{
		bool enabled();

		// The file name (without folder) for a build: `signature` is CPreview's text of the tree.
		shared_str file_name(const xr_string& signature, const CTree& tree, u32 w, u32 h, bool part);

		// The w x h pixels (4 bytes each) of a cached icon; false when there is none (or it doesn't match).
		bool load(const shared_str& name, u32 w, u32 h, xr_vector<u8>& pixels);
		void save(const shared_str& name, u32 w, u32 h, const void* pixels);

		// Deletes every cached icon (console gs_icon_cache_clear). Returns how many files.
		u32 clear();
	}
}
