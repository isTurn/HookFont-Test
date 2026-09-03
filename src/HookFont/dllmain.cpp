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


static void LogInit(HMODULE hModule)
{
	g_hModule = hModule;

	wchar_t wsPath[MAX_PATH] = { 0 };
	GetModuleFileNameW(hModule, wsPath, MAX_PATH);
	// log file lives next to the DLL itself (independent of the process CWD)
	g_wsLogPath = PathRemoveExtension(std::wstring(wsPath)) + L".log";
}

static void LogPrint(const wchar_t* wsFmt, ...)
{
	if (g_wsLogPath.empty()) return;

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


static void StartHook()
{
	try
	{
		// The INI is resolved from the DLL's own directory, so it works
		// no matter what the current working directory is.
		wchar_t wsPath[MAX_PATH] = { 0 };
		GetModuleFileNameW(g_hModule, wsPath, MAX_PATH);
		std::wstring wsIniPath = PathRemoveExtension(std::wstring(wsPath)) + L".ini";

		if (GetFileAttributesW(wsIniPath.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			LogPrint(L"INI not found: %ls", wsIniPath.c_str());
			return;
		}

		INI_File ini(wsIniPath);
		if (!ini.Has(L"HookFont"))
		{
			LogPrint(L"[HookFont] section not found in %ls", wsIniPath.c_str());
			return;
		}

		KeysMap& keys = ini[L"HookFont"];

		uint32_t     uiCharSet  = ReadIniKey(keys, L"Charset", (uint32_t)0x86);
		std::wstring wsFontName = ReadIniKey(keys, L"FontName", std::wstring(L"黑体"));

		// Let the hooks (e.g. [CharMap] replacement) write diagnostics to the log.
		SetLogCallback(LogPrint);

		// Optional: auto-register fonts shipped in <dll dir>\fonts\ BEFORE resolving
		// the target fonts, so they become visible to EnumFontFamiliesExW below.
		if (ReadIniKey(keys, L"AutoInstallFonts", true))
		{
			wchar_t wsDllDir[MAX_PATH] = { 0 };
			GetModuleFileNameW(g_hModule, wsDllDir, MAX_PATH);
			wchar_t* pSlash = wcsrchr(wsDllDir, L'\\');
			if (pSlash) *pSlash = L'\0';
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

		if (ReadIniKey(keys, L"HookCreateFontA", true))          HookCreateFontA();
		if (ReadIniKey(keys, L"HookCreateFontIndirectA", true))  HookCreateFontIndirectA();
		if (ReadIniKey(keys, L"HookCreateFontW", true))          HookCreateFontW();
		if (ReadIniKey(keys, L"HookCreateFontIndirectW", true))  HookCreateFontIndirectW();
		if (ReadIniKey(keys, L"HookDirectWrite", true))          HookDirectWrite();
		if (ReadIniKey(keys, L"HookGdiplus", true))              HookGdiplus();
		if (ReadIniKey(keys, L"HookTextOut", false))             HookTextOut();
		if (ReadIniKey(keys, L"HookGlyphOutline", false))        HookGlyphOutline();

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

		LogPrint(L"HookFont initialized. Charset=0x%02X Font=%ls FontMap=%d CharMap=%d", uiCharSet, wsFontName.c_str(), (int)vFontMap.size(), (int)mpChars.size());
	}
	catch (const std::exception& err)
	{
		LogPrint(L"HookFont init failed: %S", err.what());
	}
}


// Deferred hooking: the heavy work (INI IO, Detours transactions) runs on a
// worker thread instead of inside DllMain, which avoids running risky calls
// under the loader lock and lets the target process finish its early loading.
static DWORD WINAPI HookWorker(LPVOID)
{
	for (int ite = 0; ite < 200; ite++)
	{
		if (GetModuleHandleW(L"gdi32.dll") != NULL) break;
		Sleep(10);
	}

	StartHook();
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
		break;
	}

	return TRUE;
}

// Legacy export kept for loaders that probe for an exported symbol.
extern "C" VOID __declspec(dllexport) Dir_A() {}
