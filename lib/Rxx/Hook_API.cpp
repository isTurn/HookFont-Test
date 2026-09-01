#include "Hook_API.h"
#include "Hook.h"
#include "Hook_API_DEF.h"

#include <Windows.h>
#include <string>


namespace Rut
{
	namespace HookX
	{
		// Shared patch state (charset + font name). The A variants use an ANSI
		// (system codepage) face name, the W variants use the wide face name.
		static DWORD         sg_dwCharSet = DEFAULT_CHARSET;
		static std::string   sg_sFontNameA;
		static std::wstring  sg_wsFontNameW;


		//*********Start Hook CreateFontA*******
		static pCreateFontA rawCreateFontA = CreateFontA;
		HFONT WINAPI newCreateFontA(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
		{
			if (!sg_sFontNameA.empty())
			{
				iCharSet = sg_dwCharSet;
				pszFaceName = sg_sFontNameA.c_str();
			}
			return rawCreateFontA(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
		}

		bool HookCreateFontA(const uint32_t uiCharSet, const char* cpFontName)
		{
			sg_dwCharSet = uiCharSet;
			sg_sFontNameA = cpFontName ? cpFontName : "";
			return DetourAttachFunc(&rawCreateFontA, newCreateFontA);
		}
		//*********END Hook CreateFontA*********


		//*********Start Hook CreateFontW*******
		static pCreateFontW rawCreateFontW = CreateFontW;
		HFONT WINAPI newCreateFontW(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
		{
			if (!sg_wsFontNameW.empty())
			{
				iCharSet = sg_dwCharSet;
				pszFaceName = sg_wsFontNameW.c_str();
			}
			return rawCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
		}

		bool HookCreateFontW(const uint32_t uiCharSet, const wchar_t* wpFontName)
		{
			sg_dwCharSet = uiCharSet;
			sg_wsFontNameW = wpFontName ? wpFontName : L"";
			return DetourAttachFunc(&rawCreateFontW, newCreateFontW);
		}
		//*********END Hook CreateFontW*********


		//*********Start Hook CreateFontIndirectA*******
		static pCreateFontIndirectA rawCreateFontIndirectA = CreateFontIndirectA;
		HFONT WINAPI newCreateFontIndirectA(LOGFONTA* lplf)
		{
			if (!sg_sFontNameA.empty())
			{
				lplf->lfCharSet = (BYTE)sg_dwCharSet;
				strcpy_s(lplf->lfFaceName, sg_sFontNameA.c_str());
			}
			return rawCreateFontIndirectA(lplf);
		}

		bool HookCreateFontIndirectA(const uint32_t uiCharSet, const char* cpFontName)
		{
			sg_dwCharSet = uiCharSet;
			sg_sFontNameA = cpFontName ? cpFontName : "";
			return DetourAttachFunc(&rawCreateFontIndirectA, newCreateFontIndirectA);
		}
		//*********END Hook CreateFontIndirectA*********


		//*********Start Hook CreateFontIndirectW*******
		static pCreateFontIndirectW rawCreateFontIndirectW = CreateFontIndirectW;
		HFONT WINAPI newCreateFontIndirectW(LOGFONTW* lplf)
		{
			if (!sg_wsFontNameW.empty())
			{
				lplf->lfCharSet = (BYTE)sg_dwCharSet;
				wcscpy_s(lplf->lfFaceName, sg_wsFontNameW.c_str());
			}
			return rawCreateFontIndirectW(lplf);
		}

		bool HookCreateFontIndirectW(const uint32_t uiCharSet, const wchar_t* wpFontName)
		{
			sg_dwCharSet = uiCharSet;
			sg_wsFontNameW = wpFontName ? wpFontName : L"";
			return DetourAttachFunc(&rawCreateFontIndirectW, newCreateFontIndirectW);
		}
		//*********END Hook CreateFontIndirectW*********


		//*********Start Hook CreateWindowExA*******
		static std::string sg_sNewTitle;
		static std::string sg_sRawTitle;
		static pCreateWindowExA RawCreateWindowExA = CreateWindowExA;
		HWND WINAPI NewCreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, INT X, INT Y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
		{
			if (lpWindowName && !sg_sRawTitle.empty() && !lstrcmpA(lpWindowName, sg_sRawTitle.c_str()))
			{
				lpWindowName = sg_sNewTitle.c_str();
				DetourDetachFunc(&RawCreateWindowExA, NewCreateWindowExA); // patch once
			}

			return RawCreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
		}

		bool HookTitleExA(const char* cpRawTitle, const char* cpPatchTitle)
		{
			// copy the strings: the caller may pass temporaries, so never keep raw pointers
			sg_sNewTitle = cpPatchTitle ? cpPatchTitle : "";
			sg_sRawTitle = cpRawTitle ? cpRawTitle : "";
			return DetourAttachFunc(&RawCreateWindowExA, NewCreateWindowExA);
		}
		//*********END Hook CreateWindowExA*********
	}
}
