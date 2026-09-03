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
		static FontMapT     sg_mpFontMap;          // requested face -> replacement list


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

			// 1) per-font map (case-insensitive)
			std::wstring wsReq = TrimW(wsRequested);
			for (const auto& kv : sg_mpFontMap)
			{
				if (_wcsicmp(kv.first.c_str(), wsReq.c_str()) == 0)
				{
					std::vector<std::wstring> vCand;
					ParseCandidateList(kv.second, vCand);
					tls_wsResultW = ResolveFirstInstalled(vCand);
					if (!tls_wsResultW.empty()) return tls_wsResultW.c_str();
					break;
				}
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
		void ConfigureFontReplace(uint32_t uiCharSet, const std::wstring& wsFontNameList, const FontMapT& mpFontMap)
		{
			sg_dwCharSet = uiCharSet;

			// normalize the font map (trim keys and values, drop empty entries)
			sg_mpFontMap.clear();
			for (const auto& kv : mpFontMap)
			{
				std::wstring key = TrimW(kv.first);
				std::wstring val = TrimW(kv.second);
				if (!key.empty() && !val.empty())
					sg_mpFontMap[key] = val;
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
		enum { VTBL_CreateTextFormat = 15 };

		typedef HRESULT(WINAPI* pDWriteCreateFactory)(DWORD factoryType, const IID& riid, IUnknown** factory);
		typedef HRESULT(STDMETHODCALLTYPE* pCreateTextFormat)(
			IUnknown* pThis, LPCWSTR fontFamilyName, IUnknown* fontCollection,
			DWORD fontWeight, DWORD fontStyle, DWORD fontStretch, FLOAT fontSize,
			LPCWSTR localeName, IUnknown** textFormat);

		static pDWriteCreateFactory                          g_rawDWriteCreateFactory = NULL;
		static std::unordered_map<void*, pCreateTextFormat>  g_mpRawCreateTextFormat; // vtable -> original slot

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

		static void PatchFactoryVtbl(IUnknown* pFactory)
		{
			if (!pFactory) return;

			void** vtbl = *(void***)pFactory;
			if (g_mpRawCreateTextFormat.count(vtbl)) return; // already patched

			DWORD oldProtect = 0;
			if (VirtualProtect(&vtbl[VTBL_CreateTextFormat], sizeof(void*), PAGE_READWRITE, &oldProtect))
			{
				g_mpRawCreateTextFormat[vtbl] = (pCreateTextFormat)vtbl[VTBL_CreateTextFormat];
				vtbl[VTBL_CreateTextFormat] = (void*)&HookCreateTextFormat;
				VirtualProtect(&vtbl[VTBL_CreateTextFormat], sizeof(void*), oldProtect, &oldProtect);
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
