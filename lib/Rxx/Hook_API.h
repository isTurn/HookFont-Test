#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

//Ria's Utility Library X
namespace Rut
{
	namespace HookX
	{
		// Font name map: requested face -> replacement (the value may itself be a
		// comma-separated candidate list, the first installed font will be used).
		typedef std::unordered_map<std::wstring, std::wstring> FontMapT;

		// Configure the shared font-replacement state. Call before the Hook* functions.
		//   uiCharSet      : charset forced onto GDI font creations (0x86 = GB2312,
		//                    0x81 = Japanese, 1 = DEFAULT_CHARSET). Applied together with
		//                    a face replacement.
		//   wsFontNameList : global replacement as a comma-separated candidate list,
		//                    e.g. L"黑体, 微软雅黑"; the first installed font is used.
		//                    An empty string disables the global replacement.
		//   mpFontMap      : per-font override, requested face -> replacement (may also
		//                    be a candidate list). Takes precedence over the global list.
		void ConfigureFontReplace(uint32_t uiCharSet, const std::wstring& wsFontNameList, const FontMapT& mpFontMap);

		// Attach the four GDI font-creation hooks (state configured via ConfigureFontReplace).
		// Return: true on success, false on failure.
		bool HookCreateFontA();
		bool HookCreateFontW();
		bool HookCreateFontIndirectA();
		bool HookCreateFontIndirectW();

		// Hook DWriteCreateFactory and patch IDWriteFactory::CreateTextFormat so that
		// DirectWrite-based games get the same font replacement. Works on x86 & x64.
		bool HookDirectWrite();

		// Window title replacement (ANSI). Hooks CreateWindowExA once and patches
		// the first window whose title matches cpRawTitle.
		bool HookTitleExA(const char* cpRawTitle, const char* cpPatchTitle);
	}
}
