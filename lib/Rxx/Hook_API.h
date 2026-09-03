#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

//Ria's Utility Library X
namespace Rut
{
	namespace HookX
	{
		// Ordered font map (preserves the INI definition order, which matters for
		// wildcard precedence): requested face -> replacement (the value may itself
		// be a comma-separated candidate list, the first installed font will be used).
		// Keys may contain '*' / '?' wildcards for fuzzy matching.
		typedef std::vector<std::pair<std::wstring, std::wstring>> FontMapListT;

		// Configure the shared font-replacement state. Call before the Hook* functions.
		//   uiCharSet      : charset forced onto GDI font creations (0x86 = GB2312,
		//                    0x81 = Japanese, 1 = DEFAULT_CHARSET). Applied together with
		//                    a face replacement.
		//   wsFontNameList : global replacement as a comma-separated candidate list,
		//                    e.g. L"黑体, 微软雅黑"; the first installed font is used.
		//                    An empty string disables the global replacement.
		//   vFontMap       : ordered per-font override, requested face -> replacement
		//                    (may also be a candidate list, may contain wildcards).
		//                    Takes precedence over the global list.
		void ConfigureFontReplace(uint32_t uiCharSet, const std::wstring& wsFontNameList, const FontMapListT& vFontMap);

		// Attach the four GDI font-creation hooks (state configured via ConfigureFontReplace).
		// Return: true on success, false on failure.
		bool HookCreateFontA();
		bool HookCreateFontW();
		bool HookCreateFontIndirectA();
		bool HookCreateFontIndirectW();

		// Hook DWriteCreateFactory and patch IDWriteFactory::CreateTextFormat AND
		// CreateTextLayout (plus IDWriteTextLayout::SetFontFamilyName) so that
		// DirectWrite-based games get the same font replacement. x86 & x64.
		bool HookDirectWrite();

		// Hook gdiplus.dll GdipCreateFontFamilyFromName so GDI+ based engines
		// get the same font replacement. x86 & x64.
		bool HookGdiplus();

		// Auto-install fonts shipped next to the DLL (<dll dir>\fonts\*.ttf/ttc/otf)
		// via AddFontResourceW. Works per-session; safe, no admin required.
		// wsDllDir: directory of the Hook DLL. Returns the number of fonts registered.
		int InstallFontsFromDirectory(const wchar_t* wsDllDir);

		// Window title replacement. Hooks CreateWindowExA/W and SetWindowTextA/W once
		// and patches the first window whose title matches wsRawTitle.
		bool HookTitleWindow(const wchar_t* wsRawTitle, const wchar_t* wsPatchTitle);

		// Backward-compatible ANSI helper (internally converts and calls HookTitleWindow).
		bool HookTitleExA(const char* cpRawTitle, const char* cpPatchTitle);
	}
}
