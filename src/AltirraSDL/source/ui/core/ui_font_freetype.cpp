// Adapt ImGui's upstream loader to an optional OS library. Keeping symbol
// resolution here lets the portable executable start without FreeType.
#include <SDL3/SDL.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_GLYPH_H
#include FT_SIZES_H
#include FT_SYNTHESIS_H
#include FT_BBOX_H
#include "ui_font_freetype.h"

namespace {
	struct Library {
		SDL_SharedObject *handle = nullptr;
		~Library() { if (handle) SDL_UnloadObject(handle); }
	} s_library;
	decltype(&::FT_Activate_Size) pFT_Activate_Size = nullptr;
	decltype(&::FT_Add_Default_Modules) pFT_Add_Default_Modules = nullptr;
	decltype(&::FT_Done_Face) pFT_Done_Face = nullptr;
	decltype(&::FT_Done_Library) pFT_Done_Library = nullptr;
	decltype(&::FT_Done_Size) pFT_Done_Size = nullptr;
	decltype(&::FT_Get_Char_Index) pFT_Get_Char_Index = nullptr;
	decltype(&::FT_GlyphSlot_Embolden) pFT_GlyphSlot_Embolden = nullptr;
	decltype(&::FT_GlyphSlot_Oblique) pFT_GlyphSlot_Oblique = nullptr;
	decltype(&::FT_Library_Version) pFT_Library_Version = nullptr;
	decltype(&::FT_Load_Glyph) pFT_Load_Glyph = nullptr;
	decltype(&::FT_New_Library) pFT_New_Library = nullptr;
	decltype(&::FT_New_Memory_Face) pFT_New_Memory_Face = nullptr;
	decltype(&::FT_New_Size) pFT_New_Size = nullptr;
	decltype(&::FT_Outline_Get_BBox) pFT_Outline_Get_BBox = nullptr;
	decltype(&::FT_Property_Set) pFT_Property_Set = nullptr;
	decltype(&::FT_Render_Glyph) pFT_Render_Glyph = nullptr;
	decltype(&::FT_Request_Size) pFT_Request_Size = nullptr;
	decltype(&::FT_Select_Charmap) pFT_Select_Charmap = nullptr;
	decltype(&::FT_Set_Pixel_Sizes) pFT_Set_Pixel_Sizes = nullptr;
}

bool ATUIInitNativeFontRenderer() {
	if (s_library.handle) return true;
#ifdef __APPLE__
	const char *libraries[] = {"libfreetype.6.dylib",
		"/opt/homebrew/opt/freetype/lib/libfreetype.6.dylib",
		"/usr/local/opt/freetype/lib/libfreetype.6.dylib"};
#elif defined(_WIN32)
	const char *libraries[] = {"freetype.dll", "libfreetype-6.dll"};
#else
	const char *libraries[] = {"libfreetype.so.6"};
#endif
	for (const char *name : libraries) {
		s_library.handle = SDL_LoadObject(name);
		if (s_library.handle) break;
	}
	if (!s_library.handle) return false;
	bool complete = true;
	pFT_Activate_Size = reinterpret_cast<decltype(pFT_Activate_Size)>(SDL_LoadFunction(s_library.handle, "FT_Activate_Size"));
	complete = complete && pFT_Activate_Size;
	pFT_Add_Default_Modules = reinterpret_cast<decltype(pFT_Add_Default_Modules)>(SDL_LoadFunction(s_library.handle, "FT_Add_Default_Modules"));
	complete = complete && pFT_Add_Default_Modules;
	pFT_Done_Face = reinterpret_cast<decltype(pFT_Done_Face)>(SDL_LoadFunction(s_library.handle, "FT_Done_Face"));
	complete = complete && pFT_Done_Face;
	pFT_Done_Library = reinterpret_cast<decltype(pFT_Done_Library)>(SDL_LoadFunction(s_library.handle, "FT_Done_Library"));
	complete = complete && pFT_Done_Library;
	pFT_Done_Size = reinterpret_cast<decltype(pFT_Done_Size)>(SDL_LoadFunction(s_library.handle, "FT_Done_Size"));
	complete = complete && pFT_Done_Size;
	pFT_Get_Char_Index = reinterpret_cast<decltype(pFT_Get_Char_Index)>(SDL_LoadFunction(s_library.handle, "FT_Get_Char_Index"));
	complete = complete && pFT_Get_Char_Index;
	pFT_GlyphSlot_Embolden = reinterpret_cast<decltype(pFT_GlyphSlot_Embolden)>(SDL_LoadFunction(s_library.handle, "FT_GlyphSlot_Embolden"));
	complete = complete && pFT_GlyphSlot_Embolden;
	pFT_GlyphSlot_Oblique = reinterpret_cast<decltype(pFT_GlyphSlot_Oblique)>(SDL_LoadFunction(s_library.handle, "FT_GlyphSlot_Oblique"));
	complete = complete && pFT_GlyphSlot_Oblique;
	pFT_Library_Version = reinterpret_cast<decltype(pFT_Library_Version)>(SDL_LoadFunction(s_library.handle, "FT_Library_Version"));
	complete = complete && pFT_Library_Version;
	pFT_Load_Glyph = reinterpret_cast<decltype(pFT_Load_Glyph)>(SDL_LoadFunction(s_library.handle, "FT_Load_Glyph"));
	complete = complete && pFT_Load_Glyph;
	pFT_New_Library = reinterpret_cast<decltype(pFT_New_Library)>(SDL_LoadFunction(s_library.handle, "FT_New_Library"));
	complete = complete && pFT_New_Library;
	pFT_New_Memory_Face = reinterpret_cast<decltype(pFT_New_Memory_Face)>(SDL_LoadFunction(s_library.handle, "FT_New_Memory_Face"));
	complete = complete && pFT_New_Memory_Face;
	pFT_New_Size = reinterpret_cast<decltype(pFT_New_Size)>(SDL_LoadFunction(s_library.handle, "FT_New_Size"));
	complete = complete && pFT_New_Size;
	pFT_Outline_Get_BBox = reinterpret_cast<decltype(pFT_Outline_Get_BBox)>(SDL_LoadFunction(s_library.handle, "FT_Outline_Get_BBox"));
	complete = complete && pFT_Outline_Get_BBox;
	pFT_Property_Set = reinterpret_cast<decltype(pFT_Property_Set)>(SDL_LoadFunction(s_library.handle, "FT_Property_Set"));
	complete = complete && pFT_Property_Set;
	pFT_Render_Glyph = reinterpret_cast<decltype(pFT_Render_Glyph)>(SDL_LoadFunction(s_library.handle, "FT_Render_Glyph"));
	complete = complete && pFT_Render_Glyph;
	pFT_Request_Size = reinterpret_cast<decltype(pFT_Request_Size)>(SDL_LoadFunction(s_library.handle, "FT_Request_Size"));
	complete = complete && pFT_Request_Size;
	pFT_Select_Charmap = reinterpret_cast<decltype(pFT_Select_Charmap)>(SDL_LoadFunction(s_library.handle, "FT_Select_Charmap"));
	complete = complete && pFT_Select_Charmap;
	pFT_Set_Pixel_Sizes = reinterpret_cast<decltype(pFT_Set_Pixel_Sizes)>(SDL_LoadFunction(s_library.handle, "FT_Set_Pixel_Sizes"));
	complete = complete && pFT_Set_Pixel_Sizes;
	if (!complete) {
		SDL_UnloadObject(s_library.handle);
		s_library.handle = nullptr;
		return false;
	}
	return true;
}

// Include the pinned upstream implementation with OS entry points redirected
// to the resolved table. No changes to the vendored ImGui source are needed.
#define FT_Activate_Size pFT_Activate_Size
#define FT_Add_Default_Modules pFT_Add_Default_Modules
#define FT_Done_Face pFT_Done_Face
#define FT_Done_Library pFT_Done_Library
#define FT_Done_Size pFT_Done_Size
#define FT_Get_Char_Index pFT_Get_Char_Index
#define FT_GlyphSlot_Embolden pFT_GlyphSlot_Embolden
#define FT_GlyphSlot_Oblique pFT_GlyphSlot_Oblique
#define FT_Library_Version pFT_Library_Version
#define FT_Load_Glyph pFT_Load_Glyph
#define FT_New_Library pFT_New_Library
#define FT_New_Memory_Face pFT_New_Memory_Face
#define FT_New_Size pFT_New_Size
#define FT_Outline_Get_BBox pFT_Outline_Get_BBox
#define FT_Property_Set pFT_Property_Set
#define FT_Render_Glyph pFT_Render_Glyph
#define FT_Request_Size pFT_Request_Size
#define FT_Select_Charmap pFT_Select_Charmap
#define FT_Set_Pixel_Sizes pFT_Set_Pixel_Sizes
#include "imgui_freetype.cpp"
