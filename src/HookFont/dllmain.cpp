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

		uint32_t     uiCharSet   = ReadIniKey(keys, L"Charset", (uint32_t)0x86);
		std::wstring wsFontName  = ReadIniKey(keys, L"FontName", std::wstring(L"黑体"));
		std::string  sFontNameA  = WStrToStr(wsFontName, CP_ACP); // ANSI(GBK) face name for the A APIs

		if (ReadIniKey(keys, L"HookCreateFontA", true))           HookCreateFontA(uiCharSet, sFontNameA.c_str());
		if (ReadIniKey(keys, L"HookCreateFontIndirectA", true))  HookCreateFontIndirectA(uiCharSet, sFontNameA.c_str());
		if (ReadIniKey(keys, L"HookCreateFontW", true))          HookCreateFontW(uiCharSet, wsFontName.c_str());
		if (ReadIniKey(keys, L"HookCreateFontIndirectW", true))  HookCreateFontIndirectW(uiCharSet, wsFontName.c_str());

		// Optional: replace the game window title (common in translation patches).
		if (ReadIniKey(keys, L"HookWindowTitle", false))
		{
			std::string sRawTitle = WStrToStr(ReadIniKey(keys, L"RawWindowTitle", std::wstring()), CP_ACP);
			std::string sNewTitle = WStrToStr(ReadIniKey(keys, L"NewWindowTitle", std::wstring()), CP_ACP);
			if (!sRawTitle.empty() && !sNewTitle.empty())
			{
				HookTitleExA(sRawTitle.c_str(), sNewTitle.c_str());
			}
		}

		LogPrint(L"HookFont initialized. Charset=0x%02X Font=%ls", uiCharSet, wsFontName.c_str());
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
