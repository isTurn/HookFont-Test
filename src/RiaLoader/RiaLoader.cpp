#include <Windows.h>

#include <cstdarg>
#include <exception>
#include <string>
#include <vector>

#include "../../lib/Rxx/File.h"
#include "../../lib/Rxx/Str.h"
#include "../../lib/Rxx/INI.h"
#include "../../third/detours/include/detours.h"

using namespace Rcf::INI;
using namespace Rut::FileX;
using namespace Rut::StrX;


static std::wstring g_wsLogPath;


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

static bool IsFullPath(const std::wstring& wsPath)
{
	// "C:\..." or "C:/..."
	if (wsPath.size() >= 3 && wsPath[1] == L':' && (wsPath[2] == L'\\' || wsPath[2] == L'/')) return true;
	// UNC "\\server\share\..."
	if (wsPath.size() >= 2 && wsPath[0] == L'\\' && wsPath[1] == L'\\') return true;
	return false;
}

// Convert to an 8.3 short path so the (ANSI-only) Detours dll-name list can
// survive CJK directory names.
static std::wstring GetShortPath(const std::wstring& wsPath)
{
	wchar_t wsShort[MAX_PATH] = { 0 };
	if (GetShortPathNameW(wsPath.c_str(), wsShort, MAX_PATH) != 0)
	{
		return std::wstring(wsShort);
	}
	return wsPath;
}


INT APIENTRY WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nShowCmd)
{
	// Resolve everything relative to this EXE's own directory,
	// so the launcher works no matter where it is invoked from.
	wchar_t wsExePath[MAX_PATH] = { 0 };
	GetModuleFileNameW(hInstance, wsExePath, MAX_PATH);

	std::wstring wsExePathStr = wsExePath;
	std::wstring wsExeDir  = PathRemoveFileName(wsExePathStr);                    // keeps trailing '\'
	std::wstring wsExeBase = PathRemoveExtension(PathGetFileName(wsExePathStr));
	std::wstring wsIniPath = wsExeDir + wsExeBase + L".ini";
	g_wsLogPath            = wsExeDir + wsExeBase + L".log";

	try
	{
		INI_File ini(wsIniPath);
		if (!ini.Has(L"RiaLoader"))
		{
			MessageBoxW(NULL, (L"[" + std::wstring(L"RiaLoader") + L"] section not found in " + wsIniPath).c_str(), L"RiaLoader", MB_OK | MB_ICONERROR);
			return -1;
		}

		KeysMap& sec = ini[L"RiaLoader"];

		std::wstring wsTargetRaw = ReadIniKey(sec, L"TargetEXE", std::wstring());
		uint32_t     uiDllCount  = ReadIniKey(sec, L"TargetDLLCount", (uint32_t)0);

		if (wsTargetRaw.empty())
		{
			MessageBoxW(NULL, L"TargetEXE is empty in the INI!", L"RiaLoader", MB_OK | MB_ICONERROR);
			return -1;
		}

		std::wstring wsTargetExe = IsFullPath(wsTargetRaw) ? wsTargetRaw : wsExeDir + wsTargetRaw;
		std::wstring wsTargetDir = PathRemoveFileName(wsTargetExe);

		if (GetFileAttributesW(wsTargetExe.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			MessageBoxW(NULL, (L"Target EXE not found: " + wsTargetExe).c_str(), L"RiaLoader", MB_OK | MB_ICONERROR);
			return -1;
		}

		// Collect the DLLs to inject. DetourCreateProcessWithDllsW takes an ANSI
		// dll-name list even in its W form, so pass short (8.3) paths to be safe
		// with non-ASCII folder names.
		std::vector<std::string> vecDllAnsi;
		for (uint32_t ite = 0; ite < uiDllCount; ite++)
		{
			std::wstring wsKey    = L"TargetDLLName_" + std::to_wstring(ite);
			std::wstring wsDllRaw = ReadIniKey(sec, wsKey.c_str(), std::wstring());
			if (wsDllRaw.empty()) continue;

			std::wstring wsDllPath = IsFullPath(wsDllRaw) ? wsDllRaw : wsExeDir + wsDllRaw;
			vecDllAnsi.emplace_back(WStrToStr(GetShortPath(wsDllPath), CP_ACP));
		}

		std::vector<LPCSTR> vecDllPtrs;
		for (auto& sDll : vecDllAnsi) { vecDllPtrs.push_back(sDll.c_str()); }

		LogPrint(L"Launching: %ls", wsTargetExe.c_str());
		LogPrint(L"Working dir: %ls", wsTargetDir.c_str());
		for (auto& sDll : vecDllAnsi) { LogPrint(L"  Inject: %hs", sDll.c_str()); }

		STARTUPINFOW si = { 0 };
		PROCESS_INFORMATION pi = { 0 };
		si.cb = sizeof(si);

		// Run the game with its own folder as the working directory,
		// so the game's relative file accesses keep working.
		if (DetourCreateProcessWithDllsW(
			wsTargetExe.c_str(), NULL, NULL, NULL, FALSE, CREATE_SUSPENDED,
			NULL, wsTargetDir.c_str(), &si, &pi,
			(DWORD)vecDllPtrs.size(), vecDllPtrs.data(), NULL))
		{
			ResumeThread(pi.hThread);
			CloseHandle(pi.hThread);
			CloseHandle(pi.hProcess);

			LogPrint(L"OK: process started, %u dll(s) injected.", (DWORD)vecDllPtrs.size());
		}
		else
		{
			DWORD dwError = GetLastError();
			LogPrint(L"DetourCreateProcessWithDllsW failed, error=%u", dwError);
			MessageBoxW(NULL, L"DetourCreateProcessWithDlls Failed!", L"RiaLoader", MB_OK | MB_ICONERROR);
			return -1;
		}
	}
	catch (const std::exception& err)
	{
		LogPrint(L"RiaLoader error: %S", err.what());
		MessageBoxA(NULL, err.what(), "RiaLoader", MB_OK | MB_ICONERROR);
		return -1;
	}

	return 0;
}
