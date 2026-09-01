#pragma once
#include <cstdint>

//Ria's Utility Library X
namespace Rut
{
	namespace HookX
	{
		// Font face / charset replacement (A & W versions).
		// Return: true on success, false on failure.
		bool HookCreateFontA(const uint32_t uiCharSet, const char* cpFontName);
		bool HookCreateFontW(const uint32_t uiCharSet, const wchar_t* wpFontName);
		bool HookCreateFontIndirectA(const uint32_t uiCharSet, const char* cpFontName);
		bool HookCreateFontIndirectW(const uint32_t uiCharSet, const wchar_t* wpFontName);

		// Window title replacement (ANSI). Hooks CreateWindowExA once and patches
		// the first window whose title matches cpRawTitle.
		bool HookTitleExA(const char* cpRawTitle, const char* cpPatchTitle);
	}
}
