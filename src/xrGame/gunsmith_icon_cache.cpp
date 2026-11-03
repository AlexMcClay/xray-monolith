////////////////////////////////////////////////////////////////////////////
//	Module 		: gunsmith_icon_cache.cpp
//	Description : Gunsmith icon disk cache - drawn inventory icons kept as files
//	              between levels and sessions, so a known build needs no part models
////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "gunsmith_icon_cache.h"
#include "gunsmith_config.h"
#include <sys/utime.h>

namespace gunsmith
{
	namespace icon_cache
	{
		namespace
		{
			const u32 MAGIC = 0x31495347; // "GSI1"
			const u32 FORMAT = 2; // a change to the file layout or to how icons are drawn: new names (2: Tarkov's look)
			const u64 FNV_START = 14695981039346656037ull;

			struct SHeader
			{
				u32 magic, w, h, raw, packed;
				u64 check; // FNV-1a of the packed bytes: a damaged file is never unpacked
			};

			u64 fnv(u64 h, const void* data, size_t size)
			{
				const u8* p = static_cast<const u8*>(data);
				for (size_t i = 0; i < size; ++i)
				{
					h ^= p[i];
					h *= 1099511628211ull;
				}
				return h;
			}

			u64 fnv(u64 h, u32 v)
			{
				return fnv(h, &v, sizeof(v));
			}

			string_path g_dir = "";
			bool g_pruned = false;

			// appdata\gunsmith_icons\, made on first use
			LPCSTR folder()
			{
				if (!g_dir[0])
				{
					FS.update_path(g_dir, "$app_data_root$", "gunsmith_icons\\");
					VerifyPath(g_dir);
				}
				return g_dir;
			}

			void file_path(const shared_str& name, string_path& out)
			{
				xr_sprintf(out, "%s%s.gsi", folder(), name.c_str());
			}

			u32 file_age(LPCSTR alias, LPCSTR file)
			{
				string_path fn;
				FS.update_path(fn, alias, file);
				return FS.get_file_age(fn);
			}

			// A part's or receiver's model file time, per section.
			u32 model_age(const shared_str& section)
			{
				static xr_unordered_map<shared_str, u32> ages;
				auto it = ages.find(section);
				if (it != ages.end())
					return it->second;
				u32 age = 0;
				LPCSTR visual = READ_IF_EXISTS(pSettings, r_string, section.c_str(), "visual", (LPCSTR)nullptr);
				if (visual && visual[0])
				{
					string_path file;
					xr_strcpy(file, visual);
					if (!strext(file))
						xr_strcat(file, ".ogf");
					age = file_age("$game_meshes$", file);
				}
				ages[section] = age;
				return age;
			}

			// The preview shaders' file times (shaders\r3\, Gunsmith Framework).
			u32 shader_ages()
			{
				static bool ready = false;
				static u32 ages = 0;
				if (!ready)
				{
					u64 h = FNV_START;
					for (LPCSTR file : { "r3\\models_gs_preview.s", "r3\\gs_preview.vs", "r3\\gs_preview.ps" })
						h = fnv(h, file_age("$game_shaders$", file));
					ages = u32(h ^ (h >> 32));
					ready = true;
				}
				return ages;
			}

			struct SFile
			{
				xr_string path;
				u64 time, size;
			};

			void list(xr_vector<SFile>& out)
			{
				string_path mask;
				xr_sprintf(mask, "%s*.gsi", folder());
				WIN32_FIND_DATAA fd;
				HANDLE h = FindFirstFileA(mask, &fd);
				if (h == INVALID_HANDLE_VALUE)
					return;
				do
				{
					if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
						continue;
					SFile f;
					f.path = xr_string(folder()) + fd.cFileName;
					f.time = (u64(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
					f.size = (u64(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
					out.push_back(f);
				}
				while (FindNextFileA(h, &fd));
				FindClose(h);
			}

			// Once a session: keeps the folder under icon_cache_mb, removing the files used longest ago (a hit
			// touches its file) until it is at three quarters of that.
			void prune()
			{
				if (g_pruned)
					return;
				g_pruned = true;
				const u64 limit = u64(settings().icon_cache_mb) << 20;
				xr_vector<SFile> files;
				list(files);
				u64 total = 0;
				for (const SFile& f : files)
					total += f.size;
				if (total <= limit)
					return;
				std::sort(files.begin(), files.end(), [](const SFile& a, const SFile& b) { return a.time < b.time; });
				u32 removed = 0;
				for (const SFile& f : files)
				{
					if (total <= limit / 4 * 3)
						break;
					if (DeleteFileA(f.path.c_str()))
					{
						total -= f.size;
						++removed;
					}
				}
				Msg("- [gunsmith] icon cache: %u icons used longest ago removed (kept under %u MB)", removed,
					settings().icon_cache_mb);
			}
		}

		bool enabled()
		{
			return settings().icon_cache;
		}

		shared_str file_name(const xr_string& signature, const CTree& tree, u32 w, u32 h, bool part)
		{
			u64 x = FNV_START;
			x = fnv(x, FORMAT);
			x = fnv(x, settings().icon_cache_version);
			x = fnv(x, w);
			x = fnv(x, h);
			x = fnv(x, u32(part));
			x = fnv(x, signature.data(), signature.size());
			for (u8 i = 0; i < tree.size(); ++i)
				x = fnv(x, model_age(tree.node(i).section));
			x = fnv(x, shader_ages());
			string32 name;
			xr_sprintf(name, "%016llx", x);
			return shared_str(name);
		}

		bool load(const shared_str& name, u32 w, u32 h, xr_vector<u8>& pixels)
		{
			prune();
			string_path fn;
			file_path(name, fn);
			FILE* f = fopen(fn, "rb");
			if (!f)
				return false;
			SHeader hd;
			const u32 raw = w * h * 4;
			bool ok = fread(&hd, sizeof(hd), 1, f) == 1 && hd.magic == MAGIC && hd.w == w && hd.h == h && hd.raw == raw &&
				hd.packed && hd.packed <= rtc_csize(raw);
			xr_vector<u8> packed;
			if (ok)
			{
				packed.resize(hd.packed);
				ok = fread(packed.data(), 1, hd.packed, f) == hd.packed && fnv(FNV_START, packed.data(), packed.size()) == hd.check;
			}
			fclose(f);
			if (ok)
			{
				pixels.resize(raw);
				ok = rtc_decompress(pixels.data(), raw, packed.data(), hd.packed) == raw;
			}
			if (ok)
				_utime(fn, nullptr); // used now: pruned last
			else
				DeleteFileA(fn);
			return ok;
		}

		void save(const shared_str& name, u32 w, u32 h, const void* pixels)
		{
			prune();
			const u32 raw = w * h * 4;
			xr_vector<u8> packed(rtc_csize(raw));
			const u32 size = rtc_compress(packed.data(), u32(packed.size()), pixels, raw);
			if (!size)
				return;
			string_path fn, tmp;
			file_path(name, fn);
			xr_sprintf(tmp, "%s.tmp", fn);
			FILE* f = fopen(tmp, "wb");
			if (!f)
				return;
			SHeader hd;
			hd.magic = MAGIC;
			hd.w = w;
			hd.h = h;
			hd.raw = raw;
			hd.packed = size;
			hd.check = fnv(FNV_START, packed.data(), size);
			bool ok = fwrite(&hd, sizeof(hd), 1, f) == 1 && fwrite(packed.data(), 1, size, f) == size;
			ok = fclose(f) == 0 && ok;
			// written whole under another name first, so a crash never leaves half a file under the real one
			if (!ok || !MoveFileExA(tmp, fn, MOVEFILE_REPLACE_EXISTING))
				DeleteFileA(tmp);
		}

		u32 clear()
		{
			xr_vector<SFile> files;
			list(files);
			u32 removed = 0;
			for (const SFile& f : files)
				if (DeleteFileA(f.path.c_str()))
					++removed;
			return removed;
		}
	}
}
