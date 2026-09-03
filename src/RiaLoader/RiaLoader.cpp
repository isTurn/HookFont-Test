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


// ============================================================================
// Command-line support
//   HookFont.exe                 : launch game from INI (default)
//   HookFont.exe <game.exe> [..] : launch the given exe, extra args passed through
//   HookFont.exe -pid <PID>      : inject DLLs into an already-running process
// ============================================================================
struct CmdLine
{
	bool         bInjectPid = false;
	DWORD        dwPid = 0;
	std::wstring wsExeOverride;   // command-line-specified game exe (may be relative)
	std::wstring wsGameArgs;      // extra args passed through to the game
};

static CmdLine ParseCommandLine()
{
	CmdLine cl;

	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv) return cl;

	for (int i = 1; i < argc; ++i)
	{
		std::wstring arg = argv[i];
		if (arg == L"-pid" || arg == L"--pid" || arg == L"/pid")
		{
			if (i + 1 < argc)
			{
				cl.bInjectPid = true;
				cl.dwPid = wcstoul(argv[++i], nullptr, 10);
			}
		}
		else if (cl.wsExeOverride.empty())
		{
			cl.wsExeOverride = arg;  // first positional arg = target exe
		}
		else
		{
			if (!cl.wsGameArgs.empty()) cl.wsGameArgs += L' ';
			cl.wsGameArgs += arg;    // remaining args = game args
		}
	}

	LocalFree(argv);
	return cl;
}


// ============================================================================
// Inject into an already-running process
// ============================================================================

// Check whether a 32-bit HookFont.exe is allowed to inject into the target.
// On a 64-bit OS the x86 launcher runs as WOW64, so the target must also be
// WOW64 (32-bit). On a 32-bit OS every process is 32-bit and injectable.
static bool IsTarget32Bit(HANDLE hProcess)
{
	BOOL bSelfWow64 = FALSE;
	if (IsWow64Process(GetCurrentProcess(), &bSelfWow64) && bSelfWow64)
	{
		BOOL bTargetWow64 = FALSE;
		if (!IsWow64Process(hProcess, &bTargetWow64)) return false;
		return bTargetWow64 != FALSE;
	}
	return true;
}

// Inject a single DLL via VirtualAllocEx + WriteProcessMemory + CreateRemoteThread(LoadLibraryW).
// Waits for the loader thread and verifies a non-NULL module handle so a bad
// path / architecture surfaces as an error instead of a silent no-op.
static bool InjectDllIntoProcess(HANDLE hProcess, const std::wstring& wsDllPath, std::wstring& wsErr)
{
	size_t cbDll = (wsDllPath.size() + 1) * sizeof(wchar_t);
	void* pRemote = VirtualAllocEx(hProcess, nullptr, cbDll, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!pRemote)
	{
		wsErr = L"VirtualAllocEx failed";
		return false;
	}

	if (!WriteProcessMemory(hProcess, pRemote, wsDllPath.c_str(), cbDll, nullptr))
	{
		wsErr = L"WriteProcessMemory failed";
		VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
		return false;
	}

	HMODULE hK32 = GetModuleHandleW(L"kernel32.dll");
	FARPROC pLoadLib = GetProcAddress(hK32, "LoadLibraryW");
	HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, (LPTHREAD_START_ROUTINE)pLoadLib, pRemote, 0, nullptr);
	if (!hThread)
	{
		wsErr = L"CreateRemoteThread failed";
		VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);
		return false;
	}

	WaitForSingleObject(hThread, 15000);
	DWORD dwExit = 0;
	GetExitCodeThread(hThread, &dwExit);
	CloseHandle(hThread);
	VirtualFreeEx(hProcess, pRemote, 0, MEM_RELEASE);

	if (dwExit == 0)
	{
		wsErr = L"LoadLibraryW returned NULL (bad path / wrong architecture)";
		return false;
	}
	return true;
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

	CmdLine cl = ParseCommandLine();

	try
	{
		INI_File ini(wsIniPath);
		if (!ini.Has(L"RiaLoader"))
		{
			MessageBoxW(NULL, (L"[" + std::wstring(L"RiaLoader") + L"] section not found in " + wsIniPath).c_str(), L"RiaLoader", MB_OK | MB_ICONERROR);
			return -1;
		}

		KeysMap& sec = ini[L"RiaLoader"];

		std::wstring wsTargetRaw = cl.wsExeOverride.empty()
			? ReadIniKey(sec, L"TargetEXE", std::wstring())
			: cl.wsExeOverride;
		uint32_t     uiDllCount  = ReadIniKey(sec, L"TargetDLLCount", (uint32_t)0);

		// Collect the DLLs to inject. For the Detours create call we need an
		// ANSI dll-name list (short 8.3 paths survive CJK folder names); for the
		// running-process injection we keep the full wide paths (LoadLibraryW).
		std::vector<std::wstring> vecDllWide;
		std::vector<std::string>  vecDllAnsi;
		for (uint32_t ite = 0; ite < uiDllCount; ite++)
		{
			std::wstring wsKey    = L"TargetDLLName_" + std::to_wstring(ite);
			std::wstring wsDllRaw = ReadIniKey(sec, wsKey.c_str(), std::wstring());
			if (wsDllRaw.empty()) continue;

			std::wstring wsDllPath = IsFullPath(wsDllRaw) ? wsDllRaw : wsExeDir + wsDllRaw;
			vecDllWide.emplace_back(wsDllPath);
			vecDllAnsi.emplace_back(WStrToStr(GetShortPath(wsDllPath), CP_ACP));
		}

		if (vecDllWide.empty())
		{
			MessageBoxW(NULL, L"No DLL to inject (TargetDLLCount / TargetDLLName_*).", L"RiaLoader", MB_OK | MB_ICONERROR);
			return -1;
		}

		// ==============================
		// Mode A: inject into a running process
		// ==============================
		if (cl.bInjectPid)
		{
			LogPrint(L"Mode: inject into pid %u", cl.dwPid);

			HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, cl.dwPid);
			if (!hProc)
			{
				LogPrint(L"  OpenProcess failed, error=%u", GetLastError());
				MessageBoxW(NULL, (L"Cannot open process pid=" + std::to_wstring(cl.dwPid) + L".").c_str(), L"RiaLoader", MB_OK | MB_ICONERROR);
				return -1;
			}

			if (!IsTarget32Bit(hProc))
			{
				LogPrint(L"  Target is 64-bit, this build is 32-bit only");
				MessageBoxW(NULL, L"The target process is 64-bit, but this build is 32-bit only.\nUse the x64 build (if available) or a 32-bit process.", L"RiaLoader", MB_OK | MB_ICONERROR);
				CloseHandle(hProc);
				return -1;
			}

			bool bOkAll = true;
			for (auto& wsDll : vecDllWide)
			{
				std::wstring wsErr;
				LogPrint(L"  Inject: %ls", wsDll.c_str());
				if (!InjectDllIntoProcess(hProc, wsDll, wsErr))
				{
					LogPrint(L"    FAILED: %ls", wsErr.c_str());
					MessageBoxW(NULL, (L"Inject failed: " + wsDll + L"\n" + wsErr).c_str(), L"RiaLoader", MB_OK | MB_ICONERROR);
					bOkAll = false;
					break;
				}
				LogPrint(L"    OK");
			}

			CloseHandle(hProc);
			return bOkAll ? 0 : -1;
		}

		// ==============================
		// Mode B: launch game with injected DLLs
		// ==============================
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

		std::vector<LPCSTR> vecDllPtrs;
		for (auto& sDll : vecDllAnsi) { vecDllPtrs.push_back(sDll.c_str()); }

		// Optional command line passed through to the game. CreateProcessW may
		// modify the buffer, so hand it a writable copy (quoted exe + args).
		std::vector<wchar_t> vecCmdBuf;
		LPWSTR pCmdLine = nullptr;
		if (!cl.wsGameArgs.empty())
		{
			std::wstring wsCmd = L"\"" + wsTargetExe + L"\"" + L" " + cl.wsGameArgs;
			vecCmdBuf.assign(wsCmd.begin(), wsCmd.end());
			vecCmdBuf.push_back(L'\0');
			pCmdLine = vecCmdBuf.data();
		}

		LogPrint(L"Launching: %ls", wsTargetExe.c_str());
		LogPrint(L"Working dir: %ls", wsTargetDir.c_str());
		LogPrint(L"CmdLine: %ls", pCmdLine ? pCmdLine : L"(none)");
		for (auto& sDll : vecDllAnsi) { LogPrint(L"  Inject: %hs", sDll.c_str()); }

		STARTUPINFOW si = { 0 };
		PROCESS_INFORMATION pi = { 0 };
		si.cb = sizeof(si);

		// Run the game with its own folder as the working directory,
		// so the game's relative file accesses keep working.
		if (DetourCreateProcessWithDllsW(
			wsTargetExe.c_str(), pCmdLine, NULL, NULL, FALSE, CREATE_SUSPENDED,
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
