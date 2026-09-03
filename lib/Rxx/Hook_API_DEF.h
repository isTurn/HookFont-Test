#pragma once
#include <Windows.h>
#include <shlobj_core.h>


typedef INT(WINAPI* pMessageBoxA)(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType);

typedef HFONT(WINAPI* pCreateFontA)(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCSTR pszFaceName);
typedef HFONT(WINAPI* pCreateFontW)(INT cHeight, INT cWidth, INT cEscapement, INT cOrientation, INT cWeight, DWORD bItalic, DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision, DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily, LPCWSTR pszFaceName);

typedef HFONT(WINAPI* pCreateFontIndirectA)(CONST LOGFONTA* lplf);
typedef HFONT(WINAPI* pCreateFontIndirectW)(CONST LOGFONTW* lplf);

typedef HWND(WINAPI* pCreateWindowExA)(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, INT X, INT Y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);
typedef HWND(WINAPI* pCreateWindowExW)(DWORD dwExStyle, LPCWSTR lpClassName, LPCWSTR lpWindowName, DWORD dwStyle, INT X, INT Y, INT nWidth, INT nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam);

typedef BOOL(WINAPI* pSetWindowTextA)(HWND hWnd, LPCSTR lpString);
typedef BOOL(WINAPI* pSetWindowTextW)(HWND hWnd, LPCWSTR lpString);

typedef BOOL(WINAPI* pExtTextOutA)(HDC hdc, INT x, INT y, UINT options, CONST RECT* lprect, LPCSTR lpString, UINT c, CONST INT* lpDx);
typedef BOOL(WINAPI* pExtTextOutW)(HDC hdc, INT x, INT y, UINT options, CONST RECT* lprect, LPCWSTR lpString, UINT c, CONST INT* lpDx);

typedef DWORD(WINAPI* pGetGlyphOutlineA)(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2);
typedef DWORD(WINAPI* pGetGlyphOutlineW)(HDC hdc, UINT uChar, UINT fuFormat, LPGLYPHMETRICS lpgm, DWORD cjBuffer, LPVOID pvBuffer, const MAT2* lpmat2);

typedef HANDLE(WINAPI* pCreateFileA)(LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
typedef HANDLE(WINAPI* pCreateFileW)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);

typedef HMODULE(WINAPI* pLoadLibraryA)(LPCSTR lpLibFileName);
typedef HMODULE(WINAPI* pLoadLibraryW)(LPCWSTR lpLibFileName);

typedef HMODULE(WINAPI* pLoadLibraryExA)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
typedef HMODULE(WINAPI* pLoadLibraryExW)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);

typedef HRESULT(WINAPI* pSHGetFolderPathA)(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPSTR pszPath);
typedef HRESULT(WINAPI* pSHGetFolderPathW)(HWND hwnd, int csidl, HANDLE hToken, DWORD dwFlags, LPWSTR pszPath);
