#pragma once
#include <Windows.h>

#include "Mem_Auto.h"

//Ria's Utility Library X
namespace Rut
{
	namespace MemX
	{
		BOOL WriteMemory(LPVOID lpAddress, LPCVOID lpBuffer, SIZE_T nSize);
		BOOL ReadMemory(LPVOID lpAddress, LPVOID lpBuffer, SIZE_T nSize);
		// pFind/return are full pointer-width so the search works in both 32/64-bit.
		ULONG_PTR MemSearch(ULONG_PTR pFind, SIZE_T szFind, PBYTE pToFind, SIZE_T szToFind, BOOL backward = FALSE);
	}
}


