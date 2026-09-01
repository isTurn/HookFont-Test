#pragma once
#include <cstdint>

#include "Hook_API.h"

//Ria's Utility Library X
namespace Rut
{
	namespace HookX
	{
		// The following three manual inline-hook helpers are x86-only by design:
		// they patch a 5-byte relative jump (E9 rel32) which cannot reach arbitrary
		// 64-bit addresses. They are kept for x86 targets only and report failure on x64.
		bool WriteHookCode(uintptr_t uiRawAddress, uintptr_t uiNewAddress, size_t szHookCode);
		bool WriteHookCode_RET(uintptr_t uiRawAddress, uintptr_t uiRetAddress, uintptr_t uiNewAddress);
		bool SetHook(uintptr_t uiRawAddr, uintptr_t uiTarAddr, size_t szRawSize);

		// Detours-based detour (works on both x86 and x64).
		// Return: true on success, false on failure.
		bool DetourAttachFunc(void* ppRawFunc, void* pNewFunc);
		bool DetourDetachFunc(void* ppRawFunc, void* pNewFunc);
	}
}
