#include <Windows.h>

#include <cstdarg>
#include <exception>
#include <string>

#include "../../lib/Rxx/INI.h"
#include "../../lib/Rxx/File.h"
#include "../../lib/Rxx/Str.h"
#include "../../lib/Rxx/Hook.h"

using namespace Rcf::INI;
using namespace Rut::FileX;
using namespace Rut::StrX;
using namespace Rut::HookX;


static HMODULE      g_hModule = NULL;
static std::wstring g_wsLogPath;
static std::wstring g_wsIniPath;
static bool         g_bHotReload = false;


static void LogInit(HMODULE hModule)
{
	g_hModule = hModule;

	wchar_t wsPath[MAX_PATH] = { 0 };
	GetModuleFileNameW(hModule, wsPath, MAX_PATH);
	// log file lives next to the DLL itself (independent of the process CWD)
	g_wsLogPath = PathRemoveExtension(std::wstring(wsPath)) + L".log";
	g_wsIniPath = PathRemoveExtension(std::wstring(wsPath)) + L".ini";
}

// Roll the log file once it exceeds 2 MB (long-lived games write a lot of
// diagnostic lines): rename to <log>.old and start fresh, so the disk never
// fills up unnoticed.
static void RollLogIfNeeded()
{
	const ULONGLONG kMaxLogSize = 2ull * 1024ull * 1024ull;
	WIN32_FILE_ATTRIBUTE_DATA fad = { 0 };
	if (GetFileAttributesExW(g_wsLogPath.c_str(), GetFileExInfoStandard, &fad))
	{
		ULONGLONG ullSize = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
		if (ullSize >= kMaxLogSize)
		{
			std::wstring wsOld = g_wsLogPath + L".old";
			DeleteFileW(wsOld.c_str());
			MoveFileW(g_wsLogPath.c_str(), wsOld.c_str());
		}
	}
}

static void LogPrint(const wchar_t* wsFmt, ...)
{
	if (g_wsLogPath.empty()) return;

	RollLogIfNeeded();

	SYSTEMTIME st = { 0 };
	GetLocalTime(&st);

	wchar_t wsLine[1024] = { 0 };
	va_list args;
	va_start(args, wsFmt);
	vswprintf_s(wsLine, wsFmt, args);
	va_end(args);

	wchar_t wsBuf[1200] = { 0 };
	swprintf_s(wsBuf, L"[%02u:%02u:%02u] %ls\r\n", st.wHour, st.wMinute, st.wSecond, wsLine);

	HANDLE hFile = CreateFileW(g_wsLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		DWORD dwWritten = 0;
		WriteFile(hFile, wsBuf, (DWORD)(wcslen(wsBuf) * sizeof(wchar_t)), &dwWritten, NULL);
		CloseHandle(hFile);
	}
}


// Read an INI key with a fallback default (non-throwing).
template <typename T_Value>
static T_Value ReadIniKey(KeysMap& rKeys, const wchar_t* wName, const T_Value& vDefault)
{
	auto ite = rKeys.find(wName);
	if (ite == rKeys.end()) return vDefault;
	return static_cast<T_Value>(ite->second);
}


static ULONGLONG GetIniWriteTime()
{
	WIN32_FILE_ATTRIBUTE_DATA fad = { 0 };
	if (GetFileAttributesExW(g_wsIniPath.c_str(), GetFileExInfoStandard, &fad))
		return ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime;
	return 0;
}


// Apply (or re-apply) every config value. Hook registration happens only once
// (bFirst); later calls (hot reload) just refresh the shared state so tweaks
// like font names / mappings / scales take effect without restarting the game.
static void ApplyConfig(bool bFirst)
{
	if (GetFileAttributesW(g_wsIniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		LogPrint(L"INI not found: %ls", g_wsIniPath.c_str());
		return;
	}

	INI_File ini(g_wsIniPath);
	if (!ini.Has(L"HookFont"))
	{
		LogPrint(L"[HookFont] section not found in %ls", g_wsIniPath.c_str());
		return;
	}

	KeysMap& keys = ini[L"HookFont"];

	// Per-process override: a [HookFont:game.exe] section overrides the shared
	// [HookFont] keys, so one INI can serve several games without editing.
	wchar_t wsProc[MAX_PATH] = { 0 };
	GetModuleFileNameW(NULL, wsProc, MAX_PATH);
	wchar_t* pSlash = wcsrchr(wsProc, L'\\');
	std::wstring wsProcName = pSlash ? (pSlash + 1) : wsProc;
	std::wstring wsOverride = L"HookFont:" + wsProcName;
	if (ini.Has(wsOverride))
	{
		for (auto& kv : ini.GetOrdered(wsOverride))
			keys[kv.first] = kv.second;
		if (bFirst) LogPrint(L"[Config] per-process override [%ls] applied", wsOverride.c_str());
	}

	g_bHotReload = ReadIniKey(keys, L"HotReload", false);

	// Warn about unknown [HookFont] keys: a misspelled key silently does
	// nothing, and the log line makes "why didn't it apply?" instantly clear.
	if (bFirst)
	{
		static const wchar_t* const kKnownKeys[] = {
			L"Charset", L"CharsetSpoof", L"FontName",
			L"FontHeightScale", L"FontWidthScale", L"FontWeight", L"FontItalic", L"FontExtraScale",
			L"FontQuality", L"FontSizeScale", L"MinFontSize", L"LineHeightScale",
			L"CPRedirectFrom", L"CPRedirectCodePage",
			L"HookCreateFontA", L"HookCreateFontIndirectA", L"HookCreateFontW", L"HookCreateFontIndirectW",
			L"HookDirectWrite", L"HookGdiplus", L"AutoInstallFonts",
			L"HookWindowTitle", L"RawWindowTitle", L"NewWindowTitle",
			L"HookTextOut", L"HookGlyphOutline", L"HookDrawText", L"HookSetWindowText",
			L"AutoSC", L"FaceNameSpoof", L"EnumFontSpoof", L"Diagnostic", L"HotReload",
				L"HookSelectObject", L"DpiScaleAuto", L"HookMainModuleOnly",
		};
		for (const auto& kv : ini.GetOrdered(L"HookFont"))
		{
			bool bKnown = false;
			for (const wchar_t* pk : kKnownKeys)
			{
				if (kv.first == pk) { bKnown = true; break; }
			}
			if (!bKnown)
				LogPrint(L"[Config] WARNING: unknown key \"%ls\" in [HookFont] (ignored)", kv.first.c_str());
		}
	}

	uint32_t     uiCharSet  = ReadIniKey(keys, L"Charset", (uint32_t)0x86);
	std::wstring wsFontName = ReadIniKey(keys, L"FontName", std::wstring(L"黑体"));

	// Let the hooks (e.g. [CharMap] replacement) write diagnostics to the log.
	SetLogCallback(LogPrint);

	// Optional: auto-register fonts shipped in <dll dir>\fonts\ BEFORE resolving
	// the target fonts, so they become visible to EnumFontFamiliesExW below.
	if (bFirst && ReadIniKey(keys, L"AutoInstallFonts", true))
	{
		wchar_t wsDllDir[MAX_PATH] = { 0 };
		GetModuleFileNameW(g_hModule, wsDllDir, MAX_PATH);
		wchar_t* pSlash2 = wcsrchr(wsDllDir, L'\\');
		if (pSlash2) *pSlash2 = L'\0';
		int nInstalled = InstallFontsFromDirectory(wsDllDir);
		if (nInstalled > 0) LogPrint(L"Auto-installed %d font(s) from fonts\\", nInstalled);
	}

	// [FontMap] section: requested face -> replacement (value may be a candidate
	// list; keys may contain '*' / '?' wildcards). Read in definition order so
	// wildcard precedence is deterministic.
	FontMapListT vFontMap;
	for (auto& kv : ini.GetOrdered(L"FontMap"))
		vFontMap.emplace_back(kv.first, static_cast<std::wstring>(kv.second));

	ConfigureFontReplace(uiCharSet, wsFontName, vFontMap);

	// Character-set spoofing: keep the engine's requested charset (e.g. SHIFTJIS)
	// and only swap the face name, so Shift-JIS engines (AGE etc.) keep decoding
	// their text correctly. Off by default (legacy global-force behavior).
	bool bCharsetSpoof = ReadIniKey(keys, L"CharsetSpoof", false);
	SetCharsetSpoof(bCharsetSpoof);

	// Face-name spoofing: GetTextFace/GetObjectW report the originally requested
	// name, for engines that verify the font they just created.
	bool bFaceSpoof = ReadIniKey(keys, L"FaceNameSpoof", false);
	ConfigureFaceSpoof(bFaceSpoof);

	// Enumeration spoofing: fake "font exists" for [FontMap] keys that are not
	// installed, so engines that skip missing fonts still get the replacement.
	bool bEnumSpoof = ReadIniKey(keys, L"EnumFontSpoof", false);
	ConfigureEnumFontSpoof(bEnumSpoof);

	// Diagnostics: log every font-creation request and a sample of text draws,
	// and do NOT replace — to see exactly what the engine asks for. Enabled by
	// the INI key or by the HOOKFONT_DIAG environment variable (HookFont.exe -diag).
	wchar_t wsDiag[8] = { 0 };
	DWORD dwDiagLen = GetEnvironmentVariableW(L"HOOKFONT_DIAG", wsDiag, 8);
	bool bDiagnostic = ReadIniKey(keys, L"Diagnostic", false) || (dwDiagLen > 0 && wsDiag[0] == L'1');
	ConfigureDiagnostic(bDiagnostic);

	// Font metrics adjustment (SimpleFontHook-style): scale height/width of
	// replaced fonts, optionally override weight/italic and inter-character
	// spacing. All keys default to "keep original".
	int iHeightScale = (int)ReadIniKey(keys, L"FontHeightScale", 100);
	int iWidthScale  = (int)ReadIniKey(keys, L"FontWidthScale", 100);
	int iWeight      = (int)ReadIniKey(keys, L"FontWeight", 0);
	int iItalic      = (int)ReadIniKey(keys, L"FontItalic", -1);
	int iExtraScale  = (int)ReadIniKey(keys, L"FontExtraScale", 100);
	ConfigureFontAdjust(iHeightScale, iWidthScale, iWeight, iItalic, iExtraScale);

	// Rendering tweaks: forced lfQuality (0 = leave), |lfHeight| percent scale,
	// |lfHeight| floor (0 = off). Applied only on replaced faces.
	int iFontQuality = (int)ReadIniKey(keys, L"FontQuality", 0);
	int iFontSizeScale = (int)ReadIniKey(keys, L"FontSizeScale", 100);
	int iMinFontSize = (int)ReadIniKey(keys, L"MinFontSize", 0);
	ConfigureFontRender(iFontQuality, iFontSizeScale, iMinFontSize);

	// Line spacing: scale the metrics GetTextMetrics reports to the engine
	// (tmHeight/ascent/descent), fixing line overlap after font substitution.
	int iLineHeightScale = (int)ReadIniKey(keys, L"LineHeightScale", 100);
	ConfigureLineHeight(iLineHeightScale);

	// (tier-4) DPI-aware size compensation + main-module-only replacement.
	ConfigureDpiScale((bool)ReadIniKey(keys, L"DpiScaleAuto", false));
	ConfigureMainModuleFilter((bool)ReadIniKey(keys, L"HookMainModuleOnly", false), (void*)g_hModule);

	// Code-page redirect: Shift-JIS engines decode their byte stream with the
	// code page they request (932). Redirect it to 936 (GBK) / 65001 (UTF-8) so
	// the engine reads Chinese text directly. 0 = off; CPRedirectFrom is the
	// engine's presumed page (default 932).
	uint32_t dwCPRedirectFrom = (uint32_t)ReadIniKey(keys, L"CPRedirectFrom", (uint32_t)932);
	uint32_t dwCPRedirectTo   = (uint32_t)ReadIniKey(keys, L"CPRedirectCodePage", (uint32_t)0);
	ConfigureCodePageRedirect(dwCPRedirectFrom, dwCPRedirectTo);

	// Diagnostics: warn loudly when a configured target font is missing, so
	// "font didn't switch" issues are obvious in the log. Runs after fonts\
	// auto-install so session-registered fonts count as available.
	int nMissingFonts = CheckFontAvailability(wsFontName, vFontMap);
	if (nMissingFonts > 0)
		LogPrint(L"[FontCheck] %d target font(s) missing — replacement may not apply. Install them or drop files into fonts\\", nMissingFonts);

	// [CharMap] section: single source character -> single target character.
	// Applied to text drawn via ExtTextOut/TextOut (see HookTextOut below).
	CharMapT mpChars;
	for (auto& kv : ini.GetOrdered(L"CharMap"))
	{
		std::wstring key = kv.first; // Name = std::wstring
		std::wstring val = static_cast<std::wstring>(kv.second);
		if (key.length() != 1 || val.length() != 1) continue;
		mpChars[key[0]] = val[0];
	}
	ConfigureCharMap(mpChars);

	// [TextMap] section: source substring -> replacement text, applied to wide
	// text (ExtTextOutW / DrawTextW / SetWindowTextW) before [CharMap]/AutoSC.
	TextMapListT vTextMap;
	for (auto& kv : ini.GetOrdered(L"TextMap"))
		vTextMap.emplace_back(kv.first, static_cast<std::wstring>(kv.second));
	ConfigureTextMap(vTextMap);

	// Auto traditional -> simplified mapping (applied on ExtTextOutW text before
	// [CharMap]; needs HookTextOut = true below).
	bool bAutoSC = ReadIniKey(keys, L"AutoSC", false);
	ConfigureAutoSC(bAutoSC);

	// Control-text replacement: SetWindowTextA/W text also runs through the
	// mapping tables (independent of the title rule; title rule wins on match).
	bool bControlText = ReadIniKey(keys, L"HookSetWindowText", false);
	ConfigureControlText(bControlText);

	if (bFirst)
	{
		if (ReadIniKey(keys, L"HookCreateFontA", true))          HookCreateFontA();
		if (ReadIniKey(keys, L"HookCreateFontIndirectA", true))  HookCreateFontIndirectA();
		if (ReadIniKey(keys, L"HookCreateFontW", true))          HookCreateFontW();
		if (ReadIniKey(keys, L"HookCreateFontIndirectW", true))  HookCreateFontIndirectW();
		if (ReadIniKey(keys, L"HookDirectWrite", true))          HookDirectWrite();
		if (ReadIniKey(keys, L"HookGdiplus", true))              HookGdiplus();
		if (ReadIniKey(keys, L"HookTextOut", false))             HookTextOut();
		if (ReadIniKey(keys, L"HookGlyphOutline", false))        HookGlyphOutline();
		if (ReadIniKey(keys, L"HookDrawText", false))            HookDrawText();
		if (bControlText)                                       HookControlText();
		if (ReadIniKey(keys, L"HookSelectObject", false))      HookSelectObject();
		if (iExtraScale != 100)                                  HookSetTextCharacterExtra();
		if (iLineHeightScale != 100)                             HookGetTextMetrics();
		if (dwCPRedirectTo)                                      HookCodePage();
		if (bFaceSpoof)                                          HookFaceName();
		if (bEnumSpoof)                                          HookEnumFontFamiliesExW();

		// Optional: replace the game window title (common in translation patches).
		if (ReadIniKey(keys, L"HookWindowTitle", false))
		{
			std::wstring wsRawTitle = ReadIniKey(keys, L"RawWindowTitle", std::wstring());
			std::wstring wsNewTitle = ReadIniKey(keys, L"NewWindowTitle", std::wstring());
			if (!wsRawTitle.empty() && !wsNewTitle.empty())
			{
				HookTitleWindow(wsRawTitle.c_str(), wsNewTitle.c_str());
			}
		}

		LogPrint(L"HookFont initialized. Charset=0x%02X Font=%ls FontMap=%d CharMap=%d TextMap=%d Spoof=%d AutoSC=%d CtrlText=%d CP=%u HotReload=%d", uiCharSet, wsFontName.c_str(), (int)vFontMap.size(), (int)mpChars.size(), (int)vTextMap.size(), (int)bCharsetSpoof, (int)bAutoSC, (int)bControlText, dwCPRedirectTo, (int)g_bHotReload);
	}
	else
	{
		LogPrint(L"[HotReload] config reloaded (Font=%ls FontMap=%d CharMap=%d TextMap=%d Spoof=%d AutoSC=%d CP=%u)", wsFontName.c_str(), (int)vFontMap.size(), (int)mpChars.size(), (int)vTextMap.size(), (int)bCharsetSpoof, (int)bAutoSC, dwCPRedirectTo);
	}
}


// Deferred hooking: the heavy work (INI IO, Detours transactions) runs on a
// worker thread instead of inside DllMain, which avoids running risky calls
// under the loader lock and lets the target process finish its early loading.
// When HotReload is on, the same thread keeps watching the INI file and
// re-applies config when it changes (no restart needed).
static DWORD WINAPI HookWorker(LPVOID)
{
	for (int ite = 0; ite < 200; ite++)
	{
		if (GetModuleHandleW(L"gdi32.dll") != NULL) break;
		Sleep(10);
	}

	try
	{
		ApplyConfig(true);
	}
	catch (const std::exception& err)
	{
		LogPrint(L"HookFont init failed: %S", err.what());
	}

	if (g_bHotReload)
	{
		ULONGLONG ullLast = GetIniWriteTime();
		for (;;)
		{
			Sleep(1000);
			ULONGLONG ullNow = GetIniWriteTime();
			if (ullNow != ullLast)
			{
				ullLast = ullNow;
				try
				{
					ApplyConfig(false);
				}
				catch (const std::exception& err)
				{
					LogPrint(L"HookFont reload failed: %S", err.what());
				}
			}
		}
	}
	return 0;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		LogInit(hModule);
		DisableThreadLibraryCalls(hModule);
		CreateThread(NULL, 0, HookWorker, NULL, 0, NULL);
		break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;

	case DLL_PROCESS_DETACH:
			DumpFontStats();  // (tier-4) requested/replaced font summary
		break;
	}

	return TRUE;
}

// Legacy export kept for loaders that probe for an exported symbol.
extern "C" VOID __declspec(dllexport) Dir_A() {}
