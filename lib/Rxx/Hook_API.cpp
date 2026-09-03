#include "Hook_API.h"
#include "Hook.h"
#include "Hook_API_DEF.h"
#include "Str.h"

#include <Windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <cwchar>
#include <algorithm>


namespace Rut
{
	namespace HookX
	{
		//=====================================================================
		// Shared font-replacement state
		//=====================================================================
		static DWORD        sg_dwCharSet = DEFAULT_CHARSET;
		static std::wstring sg_wsGlobalFontW;      // resolved global replacement (first installed candidate)
		static FontMapListT   sg_vFontMap;           // ordered per-font map (may contain wildcards)


		//=====================================================================
		// Small helpers
		//=====================================================================
		static std::wstring TrimW(const std::wstring& wsStr)
		{
			size_t b = wsStr.find_first_not_of(L" \t\r\n");
			if (b == std::wstring::npos) return L"";
			size_t e = wsStr.find_last_not_of(L" \t\r\n");
			return wsStr.substr(b, e - b + 1);
		}

		// Split a candidate list on ',' / '，' and trim each item.
		static void ParseCandidateList(const std::wstring& wsList, std::vector<std::wstring>& vOut)
		{
			vOut.clear();
			std::wstring cur;
			for (wchar_t ch : wsList)
			{
				if (ch == L',' || ch == L'\xFF0C') // ',' or full-width '，'
				{
					std::wstring item = TrimW(cur);
					if (!item.empty()) vOut.push_back(item);
					cur.clear();
				}
				else cur += ch;
			}
			std::wstring last = TrimW(cur);
			if (!last.empty()) vOut.push_back(last);
		}

		// Case-insensitive wildcard match supporting '*' (any run) and '?' (one char).
		static bool WildcardMatchW(const wchar_t* wsPattern, const wchar_t* wsText)
		{
			if (!wsPattern || !wsText) return wsPattern == wsText;

			const wchar_t* p = wsPattern;
			const wchar_t* t = wsText;
			const wchar_t* starP = NULL;
			const wchar_t* starT = NULL;

			while (*t)
			{
				if (*p == L'?') { p++; t++; }
				else if (*p == L'*') { starP = p++; starT = t; }
				else if (towupper(*p) == towupper(*t)) { p++; t++; }
				else if (starP) { p = starP + 1; t = ++starT; }
				else return false;
			}
			while (*p == L'*') p++;
			return *p == L'\0';
		}


		//=====================================================================
		// Font existence check (EnumFontFamiliesExW)
		//=====================================================================
		struct FontMatchCtx { const wchar_t* target; bool found; };

		static int CALLBACK EnumFontProc(const LOGFONTW* lpelfe, const TEXTMETRICW*, DWORD, LPARAM lParam)
		{
			FontMatchCtx* ctx = (FontMatchCtx*)lParam;
			if (_wcsicmp(lpelfe->lfFaceName, ctx->target) == 0) { ctx->found = true; return 0; }
			return 1;
		}

		static bool IsFontInstalledW(const wchar_t* wsFaceName)
		{
			if (!wsFaceName || !wsFaceName[0]) return false;

			bool installed = false;
			HDC hdc = GetDC(NULL);
			if (hdc)
			{
				LOGFONTW lf = { 0 };
				lf.lfCharSet = DEFAULT_CHARSET;
				wcsncpy_s(lf.lfFaceName, LF_FACESIZE, wsFaceName, _TRUNCATE);
				FontMatchCtx ctx = { wsFaceName, false };
				EnumFontFamiliesExW(hdc, &lf, EnumFontProc, (LPARAM)&ctx, 0);
				installed = ctx.found;
				ReleaseDC(NULL, hdc);
			}
			return installed;
		}

		// Return the first installed candidate, or the raw first candidate if none is installed.
		static std::wstring ResolveFirstInstalled(const std::vector<std::wstring>& vCandidates)
		{
			if (vCandidates.empty()) return L"";
			for (const auto& name : vCandidates)
				if (IsFontInstalledW(name.c_str()))
					return name;
			return vCandidates.front();
		}


		//=====================================================================
		// Face-name resolution: per-font map first, then the global list.
		// Returns the original pointer when nothing should change, otherwise a
		// pointer into a thread-local buffer (valid until the next call on this thread).
		//=====================================================================
		static thread_local std::wstring tls_wsResultW;
		static thread_local std::string  tls_sResultA;
		static thread_local std::wstring tls_wsTemp;

		static const wchar_t* ResolveFontNameW(const wchar_t* wsRequested)
		{
			if (!wsRequested) return wsRequested;

			// 1) per-font map: exact match first, then first wildcard hit (definition order)
			std::wstring wsReq = TrimW(wsRequested);
			const std::wstring* pMapVal = NULL;

			for (const auto& kv : sg_vFontMap) // pass 1: exact (non-wildcard) keys
			{
				bool bWild = (kv.first.find(L'*') != std::wstring::npos || kv.first.find(L'?') != std::wstring::npos);
				if (!bWild && _wcsicmp(kv.first.c_str(), wsReq.c_str()) == 0) { pMapVal = &kv.second; break; }
			}
			if (!pMapVal)
			{
				for (const auto& kv : sg_vFontMap) // pass 2: wildcard keys, first hit wins
				{
					bool bWild = (kv.first.find(L'*') != std::wstring::npos || kv.first.find(L'?') != std::wstring::npos);
					if (bWild && WildcardMatchW(kv.first.c_str(), wsReq.c_str())) { pMapVal = &kv.second; break; }
				}
			}

			if (pMapVal)
			{
				std::vector<std::wstring> vCand;
				ParseCandidateList(*pMapVal, vCand);
				tls_wsResultW = ResolveFirstInstalled(vCand);
				if (!tls_wsResultW.empty()) return tls_wsResultW.c_str();
			}

			// 2) global replacement
			if (!sg_wsGlobalFontW.empty())
			{
				tls_wsResultW = sg_wsGlobalFontW;
				return tls_wsResultW.c_str();
			}

			// 3) unchanged
			return wsRequested;
		}

		static const char* ResolveFontNameA(const char* cpRequested)
		{
			if (!cpRequested) return cpRequested;

			tls_wsTemp = StrX::StrToWStr(cpRequested, CP_ACP);
			const wchar_t* wsRes = ResolveFontNameW(tls_wsTemp.c_str());
			if (wsRes == tls_wsTemp.c_str())
				return cpRequested; // unchanged
			tls_sResultA = StrX::WStrToStr(wsRes, CP_ACP);
			return tls_sResultA.c_str();
		}


		//=====================================================================
		// Configuration
		//=====================================================================
		void ConfigureFontReplace(uint32_t uiCharSet, const std::wstring& wsFontNameList, const FontMapListT& vFontMap)
		{
			sg_dwCharSet = uiCharSet;

			// normalize the font map (trim keys and values, drop empty entries, keep order)
			sg_vFontMap.clear();
			for (const auto& kv : vFontMap)
			{
				std::wstring key = TrimW(kv.first);
				std::wstring val = TrimW(kv.second);
				if (!key.empty() && !val.empty())
					sg_vFontMap.emplace_back(std::move(key), std::move(val));
			}

			// resolve the global replacement to the first installed candidate
			std::vector<std::wstring> vCand;
			ParseCandidateList(wsFontNameList, vCand);
			sg_wsGlobalFontW = ResolveFirstInstalled(vCand);
		}


		//=====================================================================
		// GDI font-creation hooks
		//=====================================================================
		//*********Start Hook CreateFontA*******
		static pCreateFontA rawCreateFontA = CreateFontA;
		HFONT WINAPI newCreateFontA(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName)
		{
			const char* sFace = ResolveFontNameA(pszFaceName);
			if (sFace != pszFaceName)
			{
				iCharSet = sg_dwCharSet;
				pszFaceName = sFace;
			}
			return rawCreateFontA(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
		}

		bool HookCreateFontA()
		{
			return DetourAttachFunc(&rawCreateFontA, newCreateFontA);
		}
		//*********END Hook CreateFontA*********


		//*********Start Hook CreateFontW*******
		static pCreateFontW rawCreateFontW = CreateFontW;
		HFONT WINAPI newCreateFontW(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName)
		{
			const wchar_t* wsFace = ResolveFontNameW(pszFaceName);
			if (wsFace != pszFaceName)
			{
				iCharSet = sg_dwCharSet;
				pszFaceName = wsFace;
			}
			return rawCreateFontW(cHeight, cWidth, cEscapement, cOrientation, cWeight, bItalic, bUnderline, bStrikeOut, iCharSet, iOutPrecision, iClipPrecision, iQuality, iPitchAndFamily, pszFaceName);
		}

		bool HookCreateFontW()
		{
			return DetourAttachFunc(&rawCreateFontW, newCreateFontW);
		}
		//*********END Hook CreateFontW*********


		//*********Start Hook CreateFontIndirectA*******
		static pCreateFontIndirectA rawCreateFontIndirectA = CreateFontIndirectA;
		HFONT WINAPI newCreateFontIndirectA(LOGFONTA* lplf)
		{
			const char* sFace = ResolveFontNameA(lplf->lfFaceName);
			if (sFace != lplf->lfFaceName)
			{
				lplf->lfCharSet = (BYTE)sg_dwCharSet;
				strncpy_s(lplf->lfFaceName, LF_FACESIZE, sFace, _TRUNCATE);
			}
			return rawCreateFontIndirectA(lplf);
		}

		bool HookCreateFontIndirectA()
		{
			return DetourAttachFunc(&rawCreateFontIndirectA, newCreateFontIndirectA);
		}
		//*********END Hook CreateFontIndirectA*********


		//*********Start Hook CreateFontIndirectW*******
		static pCreateFontIndirectW rawCreateFontIndirectW = CreateFontIndirectW;
		HFONT WINAPI newCreateFontIndirectW(LOGFONTW* lplf)
		{
			const wchar_t* wsFace = ResolveFontNameW(lplf->lfFaceName);
			if (wsFace != lplf->lfFaceName)
			{
				lplf->lfCharSet = (BYTE)sg_dwCharSet;
				wcsncpy_s(lplf->lfFaceName, LF_FACESIZE, wsFace, _TRUNCATE);
			}
			return rawCreateFontIndirectW(lplf);
		}

		bool HookCreateFontIndirectW()
		{
			return DetourAttachFunc(&rawCreateFontIndirectW, newCreateFontIndirectW);
		}
		//*********END Hook CreateFontIndirectW*********


		//=====================================================================
		// DirectWrite support
		// DWriteCreateFactory -> patch IDWriteFactory::CreateTextFormat (vtable
		// slot 15, after IUnknown's 0/1/2) so DirectWrite-based games get the
		// same font replacement. No dependency on dwrite.lib (loaded dynamically).
		//=====================================================================
		enum {
			VTBL_CreateTextFormat = 15,                       // verified by compiler asm (offset 60)
			VTBL_CreateTextLayout = 18,                       // 16 was WRONG: CreateTypography(16)+GetGdiInterop(17) precede it
			VTBL_CreateGdiCompatibleTextLayout = 19,          // 17 was WRONG: right after CreateTextLayout
			VTBL_TextLayout_SetFontFamilyName = 31,           // verified by compiler asm (offset 124)
		};

		typedef HRESULT(WINAPI* pDWriteCreateFactory)(DWORD factoryType, const IID& riid, IUnknown** factory);
		typedef HRESULT(STDMETHODCALLTYPE* pCreateTextFormat)(
			IUnknown* pThis, LPCWSTR fontFamilyName, IUnknown* fontCollection,
			DWORD fontWeight, DWORD fontStyle, DWORD fontStretch, FLOAT fontSize,
			LPCWSTR localeName, IUnknown** textFormat);
		typedef HRESULT(STDMETHODCALLTYPE* pCreateTextLayout)(
			IUnknown* pThis, const WCHAR* string, UINT32 stringLength, IUnknown* textFormat,
			FLOAT maxWidth, FLOAT maxHeight, IUnknown** textLayout);
		typedef HRESULT(STDMETHODCALLTYPE* pCreateGdiCompatibleTextLayout)(
			IUnknown* pThis, const WCHAR* string, UINT32 stringLength, IUnknown* textFormat,
			FLOAT layoutWidth, FLOAT layoutHeight, FLOAT pixelsPerDip, const void* transform,
			BOOL useGdiNatural, IUnknown** textLayout);
		struct DWTextRange { UINT32 start; UINT32 length; }; // matches DWRITE_TEXT_RANGE (by value)
		typedef HRESULT(STDMETHODCALLTYPE* pSetFontFamilyName)(
			IUnknown* pThis, const WCHAR* fontFamilyName, DWTextRange textRange);

		static pDWriteCreateFactory                          g_rawDWriteCreateFactory = NULL;
		static std::unordered_map<void*, pCreateTextFormat>  g_mpRawCreateTextFormat; // factory vtable -> slot 15
		static std::unordered_map<void*, pCreateTextLayout>  g_mpRawCreateTextLayout; // factory vtable -> slot 16
		static std::unordered_map<void*, pCreateGdiCompatibleTextLayout> g_mpRawGdiLayout; // factory vtable -> slot 17
		static std::unordered_map<void*, pSetFontFamilyName> g_mpRawSetFontFamilyName; // textlayout vtable -> slot 31



		static HRESULT STDMETHODCALLTYPE HookCreateTextFormat(
			IUnknown* pThis, LPCWSTR fontFamilyName, IUnknown* fontCollection,
			DWORD fontWeight, DWORD fontStyle, DWORD fontStretch, FLOAT fontSize,
			LPCWSTR localeName, IUnknown** textFormat)
		{
			pCreateTextFormat raw = NULL;
			if (pThis)
			{
				void** vtbl = *(void***)pThis;
				auto ite = g_mpRawCreateTextFormat.find(vtbl);
				if (ite != g_mpRawCreateTextFormat.end()) raw = ite->second;
				else raw = (pCreateTextFormat)vtbl[VTBL_CreateTextFormat]; // safety fallback
			}

			if (fontFamilyName) fontFamilyName = ResolveFontNameW(fontFamilyName);
			return raw(pThis, fontFamilyName, fontCollection, fontWeight, fontStyle, fontStretch, fontSize, localeName, textFormat);
		}

		static HRESULT STDMETHODCALLTYPE HookSetFontFamilyName(
			IUnknown* pThis, const WCHAR* fontFamilyName, DWTextRange textRange)
		{
			pSetFontFamilyName raw = NULL;
			if (pThis)
			{
				void** vtbl = *(void***)pThis;
				auto ite = g_mpRawSetFontFamilyName.find(vtbl);
				if (ite != g_mpRawSetFontFamilyName.end()) raw = ite->second;
				else raw = (pSetFontFamilyName)vtbl[VTBL_TextLayout_SetFontFamilyName]; // safety fallback
			}

			if (fontFamilyName) fontFamilyName = ResolveFontNameW(fontFamilyName);
			return raw(pThis, fontFamilyName, textRange);
		}

		// Patch the shared IDWriteTextLayout vtable so future SetFontFamilyName calls
		// get font replacement too (once per vtable).
		static void PatchTextLayoutVtbl(IUnknown* pLayout)
		{
			if (!pLayout) return;

			void** vtbl = *(void***)pLayout;
			if (g_mpRawSetFontFamilyName.count(vtbl)) return; // already patched

			DWORD oldProtect = 0;
			if (VirtualProtect(&vtbl[VTBL_TextLayout_SetFontFamilyName], sizeof(void*), PAGE_READWRITE, &oldProtect))
			{
				g_mpRawSetFontFamilyName[vtbl] = (pSetFontFamilyName)vtbl[VTBL_TextLayout_SetFontFamilyName];
				vtbl[VTBL_TextLayout_SetFontFamilyName] = (void*)&HookSetFontFamilyName;
				VirtualProtect(&vtbl[VTBL_TextLayout_SetFontFamilyName], sizeof(void*), oldProtect, &oldProtect);
			}
		}

		static HRESULT STDMETHODCALLTYPE HookCreateTextLayout(
			IUnknown* pThis, const WCHAR* string, UINT32 stringLength, IUnknown* textFormat,
			FLOAT maxWidth, FLOAT maxHeight, IUnknown** textLayout)
		{
			pCreateTextLayout raw = NULL;
			if (pThis)
			{
				void** vtbl = *(void***)pThis;
				auto ite = g_mpRawCreateTextLayout.find(vtbl);
				if (ite != g_mpRawCreateTextLayout.end()) raw = ite->second;
				else raw = (pCreateTextLayout)vtbl[VTBL_CreateTextLayout]; // safety fallback
			}

			HRESULT hr = raw(pThis, string, stringLength, textFormat, maxWidth, maxHeight, textLayout);
			if (SUCCEEDED(hr) && textLayout && *textLayout)
				PatchTextLayoutVtbl((IUnknown*)*textLayout);
			return hr;
		}

		static HRESULT STDMETHODCALLTYPE HookCreateGdiCompatibleTextLayout(
			IUnknown* pThis, const WCHAR* string, UINT32 stringLength, IUnknown* textFormat,
			FLOAT layoutWidth, FLOAT layoutHeight, FLOAT pixelsPerDip, const void* transform,
			BOOL useGdiNatural, IUnknown** textLayout)
		{
			pCreateGdiCompatibleTextLayout raw = NULL;
			if (pThis)
			{
				void** vtbl = *(void***)pThis;
				auto ite = g_mpRawGdiLayout.find(vtbl);
				if (ite != g_mpRawGdiLayout.end()) raw = ite->second;
				else raw = (pCreateGdiCompatibleTextLayout)vtbl[VTBL_CreateGdiCompatibleTextLayout]; // safety fallback
			}

			HRESULT hr = raw(pThis, string, stringLength, textFormat, layoutWidth, layoutHeight, pixelsPerDip, transform, useGdiNatural, textLayout);
			if (SUCCEEDED(hr) && textLayout && *textLayout)
				PatchTextLayoutVtbl((IUnknown*)*textLayout);
			return hr;
		}

		static void PatchFactoryVtbl(IUnknown* pFactory)
		{
			if (!pFactory) return;

			void** vtbl = *(void***)pFactory;
			if (g_mpRawCreateTextFormat.count(vtbl)) return; // already patched

			DWORD oldProtect = 0;
			// slot 15: CreateTextFormat
			if (VirtualProtect(&vtbl[VTBL_CreateTextFormat], sizeof(void*), PAGE_READWRITE, &oldProtect))
			{
				g_mpRawCreateTextFormat[vtbl] = (pCreateTextFormat)vtbl[VTBL_CreateTextFormat];
				vtbl[VTBL_CreateTextFormat] = (void*)&HookCreateTextFormat;
				VirtualProtect(&vtbl[VTBL_CreateTextFormat], sizeof(void*), oldProtect, &oldProtect);
			}
			// slot 16: CreateTextLayout
			if (VirtualProtect(&vtbl[VTBL_CreateTextLayout], sizeof(void*), PAGE_READWRITE, &oldProtect))
			{
				g_mpRawCreateTextLayout[vtbl] = (pCreateTextLayout)vtbl[VTBL_CreateTextLayout];
				vtbl[VTBL_CreateTextLayout] = (void*)&HookCreateTextLayout;
				VirtualProtect(&vtbl[VTBL_CreateTextLayout], sizeof(void*), oldProtect, &oldProtect);
			}
			// slot 17: CreateGdiCompatibleTextLayout
			if (VirtualProtect(&vtbl[VTBL_CreateGdiCompatibleTextLayout], sizeof(void*), PAGE_READWRITE, &oldProtect))
			{
				g_mpRawGdiLayout[vtbl] = (pCreateGdiCompatibleTextLayout)vtbl[VTBL_CreateGdiCompatibleTextLayout];
				vtbl[VTBL_CreateGdiCompatibleTextLayout] = (void*)&HookCreateGdiCompatibleTextLayout;
				VirtualProtect(&vtbl[VTBL_CreateGdiCompatibleTextLayout], sizeof(void*), oldProtect, &oldProtect);
			}
		}

		static HRESULT WINAPI NewDWriteCreateFactory(DWORD factoryType, const IID& riid, IUnknown** factory)
		{
			HRESULT hr = g_rawDWriteCreateFactory(factoryType, riid, factory);
			if (SUCCEEDED(hr) && factory && *factory)
				PatchFactoryVtbl(*factory);
			return hr;
		}

		bool HookDirectWrite()
		{
			if (g_rawDWriteCreateFactory) return true;

			HMODULE hDWrite = LoadLibraryW(L"dwrite.dll");
			if (!hDWrite) return false;

			g_rawDWriteCreateFactory = (pDWriteCreateFactory)GetProcAddress(hDWrite, "DWriteCreateFactory");
			if (!g_rawDWriteCreateFactory) return false;

			return DetourAttachFunc(&g_rawDWriteCreateFactory, NewDWriteCreateFactory);
		}
		//=====================================================================

		//*********Start Hook Title Window (CreateWindowExA/W + SetWindowTextA/W)*******
		static std::wstring sg_wsNewTitle;
		static std::wstring sg_wsRawTitle;
		static thread_local std::string tls_sTitleNewA;
		static pCreateWindowExA RawCreateWindowExA = CreateWindowExA;
		static pCreateWindowExW RawCreateWindowExW = CreateWindowExW;
		static pSetWindowTextA RawSetWindowTextA = SetWindowTextA;
		static pSetWindowTextW RawSetWindowTextW = SetWindowTextW;

		static bool TitleMatchesW(const wchar_t* wsTitle)
		{
			return wsTitle && !sg_wsRawTitle.empty() && _wcsicmp(wsTitle, sg_wsRawTitle.c_str()) == 0;
		}
		static bool TitleMatchesA(const char* sTitle)
		{
			if (!sTitle || sg_wsRawTitle.empty()) return false;
			tls_wsTemp = StrX::StrToWStr(sTitle, CP_ACP);
			return _wcsicmp(tls_wsTemp.c_str(), sg_wsRawTitle.c_str()) == 0;
		}

		HWND WINAPI NewCreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, INT X, INT Y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
		{
			if (TitleMatchesA(lpWindowName))
			{
				tls_sTitleNewA = StrX::WStrToStr(sg_wsNewTitle, CP_ACP);
				lpWindowName = tls_sTitleNewA.c_str();
			}
			return RawCreateWindowExA(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
		}

		HWND WINAPI NewCreateWindowExW(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, INT X, INT Y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
		{
			if (TitleMatchesW(lpWindowName)) lpWindowName = sg_wsNewTitle.c_str();
			return RawCreateWindowExW(dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
		}

		BOOL WINAPI NewSetWindowTextA(HWND hWnd, LPCSTR lpString)
		{
			if (TitleMatchesA(lpString))
			{
				tls_sTitleNewA = StrX::WStrToStr(sg_wsNewTitle, CP_ACP);
				lpString = tls_sTitleNewA.c_str();
			}
			return RawSetWindowTextA(hWnd, lpString);
		}

		BOOL WINAPI NewSetWindowTextW(HWND hWnd, LPCWSTR lpString)
		{
			if (TitleMatchesW(lpString)) lpString = sg_wsNewTitle.c_str();
			return RawSetWindowTextW(hWnd, lpString);
		}

		bool HookTitleWindow(const wchar_t* wsRawTitle, const wchar_t* wsPatchTitle)
		{
			sg_wsNewTitle = wsPatchTitle ? wsPatchTitle : L"";
			sg_wsRawTitle = wsRawTitle ? wsRawTitle : L"";

			if (sg_wsRawTitle.empty() || sg_wsNewTitle.empty()) return false;

			bool ok = true;
			ok = DetourAttachFunc(&RawCreateWindowExA, NewCreateWindowExA) && ok;
			ok = DetourAttachFunc(&RawCreateWindowExW, NewCreateWindowExW) && ok;
			ok = DetourAttachFunc(&RawSetWindowTextA, NewSetWindowTextA) && ok;
			ok = DetourAttachFunc(&RawSetWindowTextW, NewSetWindowTextW) && ok;
			return ok;
		}

		bool HookTitleExA(const char* cpRawTitle, const char* cpPatchTitle)
		{
			tls_wsTemp = StrX::StrToWStr(cpRawTitle ? cpRawTitle : "", CP_ACP);
			tls_wsResultW = StrX::StrToWStr(cpPatchTitle ? cpPatchTitle : "", CP_ACP);
			return HookTitleWindow(tls_wsTemp.c_str(), tls_wsResultW.c_str());
		}
		//*********END Hook Title Window*******


		//=====================================================================
		// GDI+ support: hook GdipCreateFontFamilyFromName so GDI+ based engines
		// get the same font replacement.
		//=====================================================================
		typedef INT(WINAPI* pGdipCreateFontFamilyFromName)(const WCHAR* name, void* fontCollection, void** fontFamily);

		static pGdipCreateFontFamilyFromName g_rawGdipCreateFontFamilyFromName = NULL;

		static INT WINAPI HookGdipCreateFontFamilyFromName(const WCHAR* name, void* fontCollection, void** fontFamily)
		{
			if (name) name = ResolveFontNameW(name);
			return g_rawGdipCreateFontFamilyFromName(name, fontCollection, fontFamily);
		}

		bool HookGdiplus()
		{
			if (g_rawGdipCreateFontFamilyFromName) return true;

			HMODULE hGdiplus = LoadLibraryW(L"gdiplus.dll");
			if (!hGdiplus) return false;

			g_rawGdipCreateFontFamilyFromName = (pGdipCreateFontFamilyFromName)GetProcAddress(hGdiplus, "GdipCreateFontFamilyFromName");
			if (!g_rawGdipCreateFontFamilyFromName) return false;

			return DetourAttachFunc(&g_rawGdipCreateFontFamilyFromName, HookGdipCreateFontFamilyFromName);
		}
		//=====================================================================


		//=====================================================================
		// Auto-install fonts shipped in <dll dir>\fonts\
		//=====================================================================
		int InstallFontsFromDirectory(const wchar_t* wsDllDir)
		{
			if (!wsDllDir || !wsDllDir[0]) return 0;

			std::wstring wsFontsDir = wsDllDir;
			if (!wsFontsDir.empty() && wsFontsDir.back() != L'\\') wsFontsDir += L'\\';
			wsFontsDir += L"fonts";

			if (GetFileAttributesW(wsFontsDir.c_str()) == INVALID_FILE_ATTRIBUTES) return 0;

			const wchar_t* wsExts[] = { L"*.ttf", L"*.ttc", L"*.otf" };
			int nInstalled = 0;
			for (const wchar_t* wsExt : wsExts)
			{
				std::wstring wsPattern = wsFontsDir + L"\\" + wsExt;
				WIN32_FIND_DATAW fd = { 0 };
				HANDLE hFind = FindFirstFileW(wsPattern.c_str(), &fd);
				if (hFind == INVALID_HANDLE_VALUE) continue;
				do
				{
					if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
					std::wstring wsFull = wsFontsDir + L"\\" + fd.cFileName;
					if (AddFontResourceW(wsFull.c_str()) > 0) nInstalled++;
				} while (FindNextFileW(hFind, &fd));
				FindClose(hFind);
			}

			if (nInstalled > 0)
				SendMessageTimeoutW(HWND_BROADCAST, WM_FONTCHANGE, 0, 0, SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG, 2000, NULL);

			return nInstalled;
		}
		//=====================================================================


		//=====================================================================
		// Character-level text replacement (ExtTextOut / TextOut)
		//=====================================================================
		static std::unordered_map<wchar_t, wchar_t> sg_mpCharMapW; // wchar -> wchar (ExtTextOutW)
		static std::unordered_map<char, char>       sg_mpCharMapA; // byte  -> byte (ExtTextOutA, values <= 0xFF)
		static bool                                 sg_bCharMapEnabled = false;
		static LogCallback                          sg_pfnLog = NULL;

		void SetLogCallback(LogCallback pfn)
		{
			sg_pfnLog = pfn;
		}

		void ConfigureCharMap(const CharMapT& mpChars)
		{
			sg_mpCharMapW.clear();
			sg_mpCharMapA.clear();

			for (const auto& kv : mpChars)
			{
				if (kv.first == kv.second) continue;
				sg_mpCharMapW[kv.first] = kv.second;
				if (kv.first <= 0xFF && kv.second <= 0xFF)
					sg_mpCharMapA[(char)kv.first] = (char)kv.second;
			}

			sg_bCharMapEnabled = !sg_mpCharMapW.empty();
		}

		// Map chars of a wide string in place. ExtTextOut strings are NOT guaranteed
		// null-terminated, so we always work with the explicit length.
		static thread_local std::wstring tls_wsTextW;

		static const wchar_t* MapCharsW(const wchar_t* wsIn, size_t nLen)
		{
			if (!sg_bCharMapEnabled || !wsIn || nLen == 0) return wsIn;

			// fast path: no mapped char at all -> hand back the original pointer
			size_t i = 0;
			for (; i < nLen; ++i)
				if (sg_mpCharMapW.count(wsIn[i])) break;
			if (i == nLen) return wsIn;

			tls_wsTextW.assign(wsIn, nLen);
			for (; i < nLen; ++i)
			{
				auto ite = sg_mpCharMapW.find(tls_wsTextW[i]);
				if (ite != sg_mpCharMapW.end()) tls_wsTextW[i] = ite->second;
			}
			return tls_wsTextW.c_str();
		}

		static thread_local std::string tls_sTextA;

		static const char* MapCharsA(const char* cpIn, size_t nLen)
		{
			if (!sg_bCharMapEnabled || !cpIn || nLen == 0) return cpIn;

			size_t i = 0;
			for (; i < nLen; ++i)
				if (sg_mpCharMapA.count(cpIn[i])) break;
			if (i == nLen) return cpIn;

			tls_sTextA.assign(cpIn, nLen);
			for (; i < nLen; ++i)
			{
				auto ite = sg_mpCharMapA.find(tls_sTextA[i]);
				if (ite != sg_mpCharMapA.end()) tls_sTextA[i] = ite->second;
			}
			return tls_sTextA.c_str();
		}


		//*********Start Hook ExtTextOutW*******
		static pExtTextOutW rawExtTextOutW = ExtTextOutW;

		BOOL WINAPI newExtTextOutW(HDC hdc, INT x, INT y, UINT options, CONST RECT* lprect, LPCWSTR lpString, UINT c, CONST INT* lpDx)
		{
			const wchar_t* wsMapped = MapCharsW(lpString, c);
			if (wsMapped != lpString && sg_pfnLog)
				sg_pfnLog(L"[CharMap] ExtTextOutW: \"%ls\" -> \"%ls\"", lpString, wsMapped);
			return rawExtTextOutW(hdc, x, y, options, lprect, wsMapped, c, lpDx);
		}

		bool HookExtTextOutW()
		{
			return DetourAttachFunc(&rawExtTextOutW, newExtTextOutW);
		}
		//*********END Hook ExtTextOutW*********


		//*********Start Hook ExtTextOutA*******
		static pExtTextOutA rawExtTextOutA = ExtTextOutA;

		BOOL WINAPI newExtTextOutA(HDC hdc, INT x, INT y, UINT options, CONST RECT* lprect, LPCSTR lpString, UINT c, CONST INT* lpDx)
		{
			const char* sMapped = MapCharsA(lpString, c);
			if (sMapped != lpString && sg_pfnLog)
				sg_pfnLog(L"[CharMap] ExtTextOutA: \"%hs\" -> \"%hs\"", lpString, sMapped);
			return rawExtTextOutA(hdc, x, y, options, lprect, sMapped, c, lpDx);
		}

		bool HookExtTextOutA()
		{
			return DetourAttachFunc(&rawExtTextOutA, newExtTextOutA);
		}
		//*********END Hook ExtTextOutA*********


		// TextOutW/A are deliberately NOT hooked: on Windows both route through
		// ExtTextOutW/A internally, so hooking only the latter avoids double-mapping
		// a string that was already replaced (and keeps the surface small).
		bool HookTextOut()
		{
			bool ok = HookExtTextOutW();
			ok = HookExtTextOutA() && ok;
			if (!ok && sg_pfnLog)
				sg_pfnLog(L"[CharMap] ExtTextOut hook failed");
			return ok;
		}


		//*********Start Hook GetGlyphOutlineA/W*******
		// Maps the requested character through [CharMap] before the outline is
		// fetched. uChar is a full Unicode code point even in the A variant, so both
		// variants consult the wide map. Fallback for engines that grab glyph
		// bitmaps directly (bypassing font objects / text output).
		static pGetGlyphOutlineA rawGetGlyphOutlineA = GetGlyphOutlineA;

		DWORD WINAPI newGetGlyphOutlineA(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2)
		{
			if (sg_bCharMapEnabled)
			{
				auto ite = sg_mpCharMapW.find((wchar_t)uChar);
				if (ite != sg_mpCharMapW.end() && ite->second != (wchar_t)uChar)
				{
					if (sg_pfnLog)
						sg_pfnLog(L"[CharMap] GetGlyphOutlineA: U+%04X -> U+%04X", uChar, (unsigned)ite->second);
					uChar = ite->second;
				}
			}
			return rawGetGlyphOutlineA(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
		}

		bool HookGetGlyphOutlineA()
		{
			return DetourAttachFunc(&rawGetGlyphOutlineA, newGetGlyphOutlineA);
		}


		static pGetGlyphOutlineW rawGetGlyphOutlineW = GetGlyphOutlineW;

		DWORD WINAPI newGetGlyphOutlineW(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2)
		{
			if (sg_bCharMapEnabled)
			{
				auto ite = sg_mpCharMapW.find((wchar_t)uChar);
				if (ite != sg_mpCharMapW.end() && ite->second != (wchar_t)uChar)
				{
					if (sg_pfnLog)
						sg_pfnLog(L"[CharMap] GetGlyphOutlineW: U+%04X -> U+%04X", uChar, (unsigned)ite->second);
					uChar = ite->second;
				}
			}
			return rawGetGlyphOutlineW(hdc, uChar, fuFormat, lpgm, cjBuffer, pvBuffer, lpmat2);
		}

		bool HookGetGlyphOutlineW()
		{
			return DetourAttachFunc(&rawGetGlyphOutlineW, newGetGlyphOutlineW);
		}

		bool HookGlyphOutline()
		{
			bool ok = HookGetGlyphOutlineA();
			ok = HookGetGlyphOutlineW() && ok;
			if (!ok && sg_pfnLog)
				sg_pfnLog(L"[CharMap] GetGlyphOutline hook failed");
			return ok;
		}
		//*********END Hook GetGlyphOutlineA/W*******
		//=====================================================================
	}
}
