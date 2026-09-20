#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <unordered_map>

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

		// Ordered substring-replacement table ([TextMap] section): source substring ->
		// replacement text, longest keys applied first. Applied to wide text drawn via
		// ExtTextOutW / DrawTextW / SetWindowTextW before [CharMap]/AutoSC.
		typedef std::vector<std::pair<std::wstring, std::wstring>> TextMapListT;

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

		// Character-set spoofing: when enabled, font-face replacement keeps the
		// engine's original lfCharSet (e.g. SHIFTJIS 0x80) instead of forcing our
		// configured charset. Engines that derive their text encoding from the
		// charset they request (AGE/Shift-JIS engines) then keep decoding correctly
		// while still getting the replaced (CJK-capable) font. Off by default.
		void SetCharsetSpoof(bool bEnable);

		// Font metrics adjustment (SimpleFontHook-style): applied only to faces that
		// are actually replaced. iHeightScale/iWidthScale are percents (100 = keep);
		// iWeight 0 = keep original, 400 = normal, 700 = bold; iItalic -1 = keep,
		// 0 = upright, 1 = italic; iExtraScale is the SetTextCharacterExtra percent
		// (100 = keep). Helps keep layout sane after font substitution.
		void ConfigureFontAdjust(int iHeightScale, int iWidthScale, int iWeight, int iItalic, int iExtraScale);

		// Rendering tweaks (applied only on replaced faces): iQuality forces the
		// lfQuality value (0 = leave; 3 = NONANTIALIASED, 4 = ANTIALIASED,
		// 5 = CLEARTYPE, 6 = CLEARTYPE_NATURAL), iSizeScale scales |lfHeight| by a
		// percent, iMinSize floors |lfHeight| (0 = off). Fixes blurry/too-thin text
		// and tiny engine fonts after substitution.
		void ConfigureFontRender(int iQuality, int iSizeScale, int iMinSize);

		// Line spacing: scale tmHeight/tmAscent/tmDescent reported by
		// GetTextMetricsA/W (the values engines use for line layout). 100 = keep.
		// HookGetTextMetrics() is only needed when configured != 100.
		void ConfigureLineHeight(int iLineHeightScale);
		bool HookGetTextMetrics();

		// Code-page redirect: when the engine converts text with dwSrcCodePage
		// (e.g. 932 = Shift-JIS), GetACP/GetOEMCP/GetCPInfo/MultiByteToWideChar
		// report / convert with dwDstCodePage (e.g. 936 = GBK, 65001 = UTF-8)
		// instead — Shift-JIS engines then decode their byte stream as GBK/UTF-8
		// directly. dwDstCodePage = 0 = off. Default source page: 932.
		void ConfigureCodePageRedirect(uint32_t dwSrcCodePage, uint32_t dwDstCodePage);
		bool HookCodePage();

		// Face-name spoofing: when enabled, GetTextFaceA/W and GetObjectW (font
		// LOGFONT queries) report the engine's originally requested face name
		// instead of the replaced one. Some engines verify the font they just
		// created and misbehave when the name differs (refuse to use it / recreate
		// forever). Off by default.
		void ConfigureFaceSpoof(bool bEnable);
		bool HookFaceName();

		// Font-enumeration spoofing: when the engine enumerates fonts to verify a
		// face exists before using it, report a hit for [FontMap] keys that are not
		// actually installed — engines that skip "missing" fonts still get the
		// replacement instead of giving up. Off by default.
		void ConfigureEnumFontSpoof(bool bEnable);
		bool HookEnumFontFamiliesExW();

		// DrawText support: route DrawTextA/W through the same [CharMap]/AutoSC
		// mapping as ExtTextOut (engines that draw static/button text via DrawText).
		bool HookDrawText();

		// Diagnostics mode: when enabled, font creation is NOT replaced — every
		// font-creation request (face/charset/size/quality) and a sample of text
		// draws are logged instead, so you can see exactly what the engine asks
		// for. Off by default.
		void ConfigureDiagnostic(bool bEnable);

		// Attach the SetTextCharacterExtra hook (inter-character spacing scaling).
		// Only needed when ConfigureFontAdjust received iExtraScale != 100.
		bool HookSetTextCharacterExtra();

		// Diagnostics: report via the log callback every configured target font
		// (global FontName candidates + every [FontMap] value) that is NOT present
		// on the system — so "why didn't it switch?" questions get answered fast.
		// Call after ConfigureFontReplace (and after any fonts\ auto-install so
		// session-registered fonts are visible). Returns the number of missing fonts.
		int CheckFontAvailability(const std::wstring& wsFontNameList, const FontMapListT& vFontMap);

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

		//=====================================================================
		// Character-level text replacement (ExtTextOut / TextOut)
		//=====================================================================
		// Per-character map: source wchar -> target wchar. Any text drawn through
		// GDI ExtTextOutW/A (TextOutW/A internally route through ExtTextOut and are
		// therefore covered too) has its characters mapped before drawing. Typical
		// use: replace Japanese punctuation / kana that a locked engine font cannot
		// render (e.g. 「」-> “”, あ -> 阿), or force half/full-width variants.
		// For ExtTextOutA only entries whose value fits a single byte (<= 0xFF) are
		// applied, byte-by-byte.
		typedef std::unordered_map<wchar_t, wchar_t> CharMapT;

		// Auto traditional -> simplified Chinese mapping: when enabled, wide text
		// drawn through ExtTextOutW is simplified before [CharMap] applies (CJK
		// ideographs only, string length preserved). Requires HookTextOut(). Off by
		// default.
		void ConfigureAutoSC(bool bEnable);

		// Configure the character replacement table. Call before HookTextOut().
		void ConfigureCharMap(const CharMapT& mpChars);

		// Substring replacement table: every occurrence of each source substring in
		// wide text is replaced (longest keys first, no re-scan of inserted text).
		// Applied inside MapCharsW, so it covers ExtTextOutW / DrawTextW and the
		// control-text path. Off when the table is empty.
		void ConfigureTextMap(const TextMapListT& vTextMap);

		// Control-text replacement: when enabled, SetWindowTextA/W text that does not
		// match the title rule is also mapped through [TextMap]/[CharMap]/AutoSC —
		// covers static controls and buttons in engines that use controls instead of
		// owner-drawn text. Independent of HookTitleWindow (which has priority when
		// the text matches the raw title).
		void ConfigureControlText(bool bEnable);
		bool HookControlText();

		// Attach the ExtTextOutW/A hooks. Requires ConfigureCharMap() first.
		bool HookTextOut();

		// Attach the GetGlyphOutlineA/W hooks. Maps the requested character through
		// the same [CharMap] table before the glyph outline is produced — a fallback
		// for engines that fetch glyph bitmaps directly instead of going through
		// font objects / text output (e.g. old DirectX rasterizers). Requires
		// ConfigureCharMap() first.
		bool HookGlyphOutline();

		// Optional log sink used by the hooks (dllmain registers its logger here);
		// hooks call it for diagnostics such as "text replaced". NULL = no logging.
		typedef void(*LogCallback)(const wchar_t* wsFmt, ...);
		void SetLogCallback(LogCallback pfn);
	}
}
