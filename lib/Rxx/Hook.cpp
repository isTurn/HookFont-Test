#include "Hook.h"
#include "Mem.h"

#include <Windows.h>

#include "../../third/detours/include/detours.h"


namespace Rut
{
	namespace HookX
	{
		bool WriteHookCode(uintptr_t uiRawAddress, uintptr_t uiNewAddress, size_t szHookCode)
		{
#if defined(_M_IX86)
			UCHAR code[0xF];
			memset(code, 0x90, 0xF);

			*(UCHAR*)(code + 0) = 0xE9;
			*(DWORD*)(code + 1) = (DWORD)(uiNewAddress - uiRawAddress - 5);

			return MemX::WriteMemory((LPVOID)uiRawAddress, &code, szHookCode) == TRUE;
#else
			(void)uiRawAddress; (void)uiNewAddress; (void)szHookCode;
			return false; // manual inline hook is not supported on x64
#endif
		}

		bool WriteHookCode_RET(uintptr_t uiRawAddress, uintptr_t uiRetAddress, uintptr_t uiNewAddress)
		{
#if defined(_M_IX86)
			UCHAR code[0xF];
			memset(code, 0x90, 0xF);

			*(UCHAR*)(code + 0) = 0xE9;
			*(DWORD*)(code + 1) = (DWORD)(uiNewAddress - uiRawAddress - 5);

			return MemX::WriteMemory((LPVOID)uiRawAddress, &code, (size_t)(uiRetAddress - uiRawAddress)) == TRUE;
#else
			(void)uiRawAddress; (void)uiRetAddress; (void)uiNewAddress;
			return false;
#endif
		}

		bool SetHook(uintptr_t uiRawAddr, uintptr_t uiTarAddr, size_t szRawSize)
		{
#if defined(_M_IX86)
			DWORD old = 0;
			DWORD rva = 0;
			BYTE rawJmp[] = { 0xE9,0x00,0x00,0x00,0x00 };
			BYTE retJmp[] = { 0xE9,0x00,0x00,0x00,0x00 };
			BYTE tarCal[] = { 0xE8,0x00,0x00,0x00,0x00 };

			BOOL protect = VirtualProtect((LPVOID)uiRawAddr, 0x1000, PAGE_EXECUTE_READWRITE, &old);
			PBYTE alloc = (PBYTE)VirtualAlloc(NULL, 0x1000, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (alloc && protect)
			{
				//Copy the Code for the original address to alloc address
				memcpy(alloc, (PVOID)uiRawAddr, szRawSize);

				//Write Jmp Code
				rva = (DWORD)(alloc - (PBYTE)uiRawAddr - sizeof(rawJmp));
				memcpy(&rawJmp[1], &rva, sizeof(DWORD));
				memcpy((PBYTE)uiRawAddr, rawJmp, sizeof(rawJmp));

				//Write Call TarFunc Code
				rva = (DWORD)(uiTarAddr - (uintptr_t)(&alloc[szRawSize]) - sizeof(tarCal));
				memcpy(&tarCal[1], &rva, sizeof(DWORD));
				memcpy(&alloc[szRawSize], tarCal, sizeof(tarCal));

				//Write Ret Code
				rva = (DWORD)((uiRawAddr + szRawSize) - (uintptr_t)(&alloc[szRawSize + sizeof(tarCal)]) - sizeof(retJmp));
				memcpy(&retJmp[1], &rva, sizeof(DWORD));
				memcpy(&alloc[szRawSize + sizeof(tarCal)], retJmp, sizeof(retJmp));

				return true;
			}
			else
			{
				return false;
			}
#else
			(void)uiRawAddr; (void)uiTarAddr; (void)szRawSize;
			return false;
#endif
		}

		bool DetourAttachFunc(void* ppRawFunc, void* pNewFunc)
		{
			DetourRestoreAfterWith();
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			LONG erroAttach = DetourAttach((PVOID*)ppRawFunc, pNewFunc);
			LONG erroCommit = DetourTransactionCommit();

			return (erroAttach == NO_ERROR && erroCommit == NO_ERROR);
		}

		bool DetourDetachFunc(void* ppRawFunc, void* pNewFunc)
		{
			DetourRestoreAfterWith();
			DetourTransactionBegin();
			DetourUpdateThread(GetCurrentThread());

			LONG erroDetach = DetourDetach((PVOID*)ppRawFunc, pNewFunc);
			LONG erroCommit = DetourTransactionCommit();

			return (erroDetach == NO_ERROR && erroCommit == NO_ERROR);
		}
	}
}
