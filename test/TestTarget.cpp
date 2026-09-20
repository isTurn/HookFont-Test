// TestTarget.cpp — 冒烟测试目标：
// 被 HookFont.exe（RiaLoader）启动并注入 HookFont.dll 后，依次调用 GDI
// CreateFont* 四个 API、DirectWrite（CreateTextFormat + CreateTextLayout +
// SetFontFamilyName）、GDI+ GdipCreateFontFamilyFromName、SetWindowTextW，
// 把实际生效的字符集与字体名写入 TestTarget_result.txt。
//
// 测试用例（对应 test\HookFont.ini）：
//   - 未命中 [FontMap] 的字体（"Arial"）走全局候选列表 FontName=宋体,黑体 → 宋体
//   - 命中 [FontMap] 精确键（"MS Gothic"）用映射值 → 黑体
//   - 命中 [FontMap] 通配键（"MS PGothic" → "MS*"）→ 黑体
//   - DirectWrite CreateTextFormat / CreateTextLayout+SetFontFamilyName 同样被替换
//   - GDI+ GdipCreateFontFamilyFromName 被替换
//   - 窗口标题（CreateWindowExW + SetWindowTextW）被替换为 NewWindowTitle
#include <windows.h>
#include <cstdio>
#include <cwchar>
#include <string>
#include <dwrite.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "dwrite.lib")


static void WriteResult(const char* tag, BYTE charset, const wchar_t* face)
{
    // dump the face name as UTF-8 hex so non-ASCII names (黑体) survive any console/CRT locale
    char utf8[256] = { 0 };
    WideCharToMultiByte(CP_UTF8, 0, face, -1, utf8, 255, NULL, NULL);

    FILE* f = nullptr;
    fopen_s(&f, "TestTarget_result.txt", "a");
    if (f)
    {
        fprintf(f, "[%s] Charset=0x%02X Font=UTF8Hex=", tag, charset);
        for (const char* p = utf8; *p; p++) fprintf(f, "%02X", (unsigned char)*p);
        fprintf(f, "\r\n");
        fclose(f);
    }
}


static void TestCreateFontA(const char* request, const char* tag)
{
    HFONT hf = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, request);
    LOGFONTA lf = { 0 };
    GetObjectA(hf, sizeof(lf), &lf);
    wchar_t face[MAX_PATH] = { 0 };
    MultiByteToWideChar(CP_ACP, 0, lf.lfFaceName, -1, face, MAX_PATH);
    WriteResult(tag, lf.lfCharSet, face);
    DeleteObject(hf);
}

static void TestCreateFontIndirectA(const char* request, const char* tag)
{
    LOGFONTA lf = { 0 };
    lf.lfHeight = 16;
    lf.lfCharSet = ANSI_CHARSET;
    lstrcpyA(lf.lfFaceName, request);
    HFONT hf = CreateFontIndirectA(&lf);
    LOGFONTA lfOut = { 0 };
    GetObjectA(hf, sizeof(lfOut), &lfOut);
    wchar_t face[MAX_PATH] = { 0 };
    MultiByteToWideChar(CP_ACP, 0, lfOut.lfFaceName, -1, face, MAX_PATH);
    WriteResult(tag, lfOut.lfCharSet, face);
    DeleteObject(hf);
}

static void TestCreateFontW(const wchar_t* request, const char* tag)
{
    HFONT hf = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, request);
    LOGFONTW lf = { 0 };
    GetObjectW(hf, sizeof(lf), &lf);
    WriteResult(tag, (BYTE)lf.lfCharSet, lf.lfFaceName);
    DeleteObject(hf);
}

static void TestCreateFontIndirectW(const wchar_t* request, const char* tag)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = 16;
    lf.lfCharSet = ANSI_CHARSET;
    wcscpy_s(lf.lfFaceName, request);
    HFONT hf = CreateFontIndirectW(&lf);
    LOGFONTW lfOut = { 0 };
    GetObjectW(hf, sizeof(lfOut), &lfOut);
    WriteResult(tag, (BYTE)lfOut.lfCharSet, lfOut.lfFaceName);
    DeleteObject(hf);
}

// Charset-spoofing probe: request a SHIFTJIS(0x80) font like a Japanese
// AGE-style engine does. With CharsetSpoof=false the hook forces the configured
// charset (0x86); with CharsetSpoof=true it must keep 0x80 while still swapping
// the face name. Read from TestTarget_result.txt.
static void TestCharsetSpoof()
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = 16;
    lf.lfCharSet = SHIFTJIS_CHARSET;            // 0x80, as requested by Shift-JIS engines
    wcscpy_s(lf.lfFaceName, L"MS Gothic");
    HFONT hf = CreateFontIndirectW(&lf);
    LOGFONTW lfOut = { 0 };
    GetObjectW(hf, sizeof(lfOut), &lfOut);
    WriteResult("SpoofProbe", (BYTE)lfOut.lfCharSet, lfOut.lfFaceName);
    DeleteObject(hf);
}

static void TestDirectWrite(const wchar_t* request, const char* tag)
{
    IDWriteFactory* factory = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory);
    if (FAILED(hr) || !factory) { WriteResult(tag, 0, L"DWRITE_FACTORY_FAIL"); return; }

    IDWriteTextFormat* fmt = nullptr;
    hr = factory->CreateTextFormat(request, NULL,
                                   DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                   16.0f, L"", &fmt);
    if (FAILED(hr) || !fmt) { WriteResult(tag, 0, L"DWRITE_FORMAT_FAIL"); factory->Release(); return; }

    wchar_t name[64] = { 0 };
    fmt->GetFontFamilyName(name, 64);
    WriteResult(tag, 0, name); // DirectWrite has no charset concept -> report 0

    fmt->Release();
    factory->Release();
}

// CreateTextLayout + SetFontFamilyName path (font family is changed AFTER the
// layout is created, exercising the IDWriteTextLayout::SetFontFamilyName hook).
static void TestDirectWriteLayout(const wchar_t* request, const char* tag)
{
    IDWriteFactory* factory = nullptr;
    HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&factory);
    if (FAILED(hr) || !factory) { WriteResult(tag, 0, L"DWRITE_FACTORY_FAIL"); return; }

    IDWriteTextFormat* fmt = nullptr;
    hr = factory->CreateTextFormat(L"Arial", NULL,
                                   DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                   16.0f, L"", &fmt);
    if (FAILED(hr) || !fmt) { WriteResult(tag, 0, L"DWRITE_FORMAT_FAIL"); factory->Release(); return; }

    IDWriteTextLayout* layout = nullptr;
    hr = factory->CreateTextLayout(L"TestText", 8, fmt, 200.0f, 40.0f, &layout);
    if (FAILED(hr) || !layout) { WriteResult(tag, 0, L"LAYOUT_FAIL"); fmt->Release(); factory->Release(); return; }

    DWRITE_TEXT_RANGE range = { 0, 8 };

    layout->SetFontFamilyName(request, range);

    wchar_t name[64] = { 0 };
    layout->GetFontFamilyName(0, name, 64);
    WriteResult(tag, 0, name);

    layout->Release();
    fmt->Release();
    factory->Release();
}

// GDI+ GdipCreateFontFamilyFromName path (dynamically loaded, no gdiplus link).
static void TestGdiplus(const wchar_t* request, const char* tag)
{
    typedef int (WINAPI* FnCreateFamily)(const WCHAR*, void*, void**);
    typedef int (WINAPI* FnGetFamilyName)(void*, WCHAR*, WCHAR);
    typedef int (WINAPI* FnStartup)(ULONG_PTR*, const void*, void*);
    typedef void (WINAPI* FnShutdown)(ULONG_PTR);

    HMODULE hGdiplus = LoadLibraryW(L"gdiplus.dll");
    if (!hGdiplus) { WriteResult(tag, 0, L"GDI+_LOAD_FAIL"); return; }

    FnStartup fnStartup = (FnStartup)GetProcAddress(hGdiplus, "GdiplusStartup");
    FnShutdown fnShutdown = (FnShutdown)GetProcAddress(hGdiplus, "GdiplusShutdown");
    FnCreateFamily fnCreate = (FnCreateFamily)GetProcAddress(hGdiplus, "GdipCreateFontFamilyFromName");
    FnGetFamilyName fnGetName = (FnGetFamilyName)GetProcAddress(hGdiplus, "GdipGetFamilyName");

    ULONG_PTR token = 0;
    struct { UINT32 version; void* cb; BOOL suppressBg; BOOL suppressExt; } input = { 1, NULL, FALSE, FALSE };
    if (!fnStartup || !fnShutdown || !fnCreate || !fnGetName ||
        fnStartup(&token, &input, NULL) != 0) { WriteResult(tag, 0, L"GDI+_INIT_FAIL"); return; }

    void* family = nullptr;
    if (fnCreate(request, NULL, &family) != 0 || !family) { WriteResult(tag, 0, L"GDI+_FAMILY_FAIL"); fnShutdown(token); return; }

    wchar_t name[64] = { 0 };
    fnGetName(family, name, 0); // 0 = LANG_NEUTRAL
    WriteResult(tag, 0, name);

    fnShutdown(token);
}

// Window title replacement via CreateWindowExW + SetWindowTextW (both hooked).
static void TestSetWindowTextW(const char* tag)
{
    WNDCLASSW wc = { 0 };
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"HFTestCls";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, L"HFTestCls", L"TestTitle", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) { WriteResult(tag, 0, L"WND_FAIL"); return; }

    SetWindowTextW(hwnd, L"TestTitle");

    wchar_t buf[64] = { 0 };
    GetWindowTextW(hwnd, buf, 64);
    WriteResult(tag, 0, buf);

    DestroyWindow(hwnd);
}


// Character-level replacement: draw text through ExtTextOutW / TextOutW / ExtTextOutA
// onto a memory DC. The injected DLL hooks ExtTextOut and rewrites mapped chars
// ([CharMap]: 「-> “, あ-> 阿 ...); TextOut routes through ExtTextOut so it is
// covered too. The rendered glyphs cannot be read back cheaply, so the actual
// rewrite is verified from the hook's own diagnostics (HookFont.log lines
// "[CharMap] ExtTextOutW: ... -> ..."); here we just prove the calls succeed and
// don't crash. ExtTextOutA should be a no-op for non-ASCII maps (values > 0xFF).
static void TestTextOut(const char* tag)
{
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) { WriteResult(tag, 0, L"DC_FAIL"); return; }

    HFONT hf = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"宋体");
    HGDIOBJ hOld = SelectObject(hdc, hf);

    const wchar_t* wsW = L"ExtTextOutW「あ」";
    BOOL b1 = ExtTextOutW(hdc, 10, 10, 0, NULL, wsW, (UINT)wcslen(wsW), NULL);

    const wchar_t* wsT = L"あああ";
    BOOL b2 = TextOutW(hdc, 10, 30, wsT, (int)wcslen(wsT));

    const char* sA = "ExtTextOutA";
    BOOL b3 = ExtTextOutA(hdc, 10, 50, 0, NULL, sA, (UINT)strlen(sA), NULL);

    SelectObject(hdc, hOld);
    DeleteObject(hf);
    DeleteDC(hdc);

	WriteResult(tag, (BYTE)(b1 && b2 && b3), L"TEXTOUT_OK");
}


// Auto simplified-Chinese mapping: draw a traditional-ideograph string through
// ExtTextOutW. With AutoSC=true the injected DLL rewrites 東->东 before drawing;
// the rewrite is verified from HookFont.log ("[AutoSC] N char(s) traditional ->
// simplified"). Here we only prove the call succeeds and doesn't crash.
static void TestAutoSC(const char* tag)
{
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) { WriteResult(tag, 0, L"DC_FAIL"); return; }

    HFONT hf = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"宋体");
    HGDIOBJ hOld = SelectObject(hdc, hf);

    const wchar_t* wsT = L"東京都庁";   // 東->东, 都->都, 京->京, 庁->厅(simplified of 廳)
    BOOL b1 = ExtTextOutW(hdc, 10, 10, 0, NULL, wsT, (UINT)wcslen(wsT), NULL);

    SelectObject(hdc, hOld);
    DeleteObject(hf);
    DeleteDC(hdc);

    WriteResult(tag, (BYTE)b1, L"AUTOSC_OK");
}


// Font metrics adjustment: create a font with known metrics through
// CreateFontIndirectW; when FontHeightScale/FontWidthScale/FontWeight are set
// (default 100/100/0 keep original), the read-back LOGFONT reflects the scaled
// values. Verified across runs: baseline vs. scaled config in test/HookFont.ini.
static void TestFontMetrics(const char* tag)
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = 20;
    lf.lfWidth = 10;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = ANSI_CHARSET;
    wcscpy_s(lf.lfFaceName, L"MS Gothic");   // hit [FontMap] -> replaced font
    HFONT hf = CreateFontIndirectW(&lf);
    LOGFONTW lfOut = { 0 };
    GetObjectW(hf, sizeof(lfOut), &lfOut);
    wchar_t buf[96];
    swprintf_s(buf, 96, L"h=%d w=%d wt=%d", (int)lfOut.lfHeight, (int)lfOut.lfWidth, (int)lfOut.lfWeight);
    WriteResult(tag, (BYTE)lfOut.lfCharSet, buf);
    DeleteObject(hf);
}


// Rendering tweaks: FontSizeScale (percent) + MinFontSize (|lfHeight| floor),
// applied on replaced faces. With scale=150 / min=16, lfHeight 10 -> 15 -> 16
// (clamped), lfHeight 20 -> 30. Verified by comparing baseline vs configured runs.
static void TestRenderTweaks(const char* tag)
{
    LOGFONTW lf = { 0 };
    lf.lfCharSet = ANSI_CHARSET;
    wcscpy_s(lf.lfFaceName, L"MS Gothic");   // hit [FontMap] -> replaced font

    lf.lfHeight = 10;
    HFONT hfA = CreateFontIndirectW(&lf);
    LOGFONTW loA = { 0 };
    GetObjectW(hfA, sizeof(loA), &loA);

    lf.lfHeight = 20;
    HFONT hfB = CreateFontIndirectW(&lf);
    LOGFONTW loB = { 0 };
    GetObjectW(hfB, sizeof(loB), &loB);

    wchar_t buf[96];
    swprintf_s(buf, 96, L"a=%d b=%d", (int)loA.lfHeight, (int)loB.lfHeight);
    DeleteObject(hfA);
    DeleteObject(hfB);
    WriteResult(tag, 1, buf);
}


// Line spacing: GetTextMetrics values the engine sees. With LineHeightScale=200
// tmHeight/ascent/descent double while internal leading stays consistent.
static void TestTextMetrics(const char* tag)
{
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) { WriteResult(tag, 0, L"DC_FAIL"); return; }

    HFONT hf = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"MS Gothic");
    HGDIOBJ hOld = SelectObject(hdc, hf);

    TEXTMETRICW tm = { 0 };
    BOOL ok = GetTextMetricsW(hdc, &tm);
    wchar_t buf[96];
    swprintf_s(buf, 96, L"H=%d A=%d D=%d IL=%d", (int)tm.tmHeight, (int)tm.tmAscent, (int)tm.tmDescent, (int)tm.tmInternalLeading);

    SelectObject(hdc, hOld);
    DeleteObject(hf);
    DeleteDC(hdc);
    WriteResult(tag, (BYTE)ok, buf);
}


// Code-page redirect: with CPRedirectCodePage=65001, GetACP reports 65001 and
// GetCPInfo(932) returns UTF-8's CPINFO (MaxCharSize=4 instead of 2).
static void TestCodePage(const char* tag)
{
    UINT acp = GetACP();
    CPINFO info = { 0 };
    BOOL ok = GetCPInfo(932, &info);
    wchar_t buf[96];
    swprintf_s(buf, 96, L"acp=%u cpiMax=%u", acp, (UINT)info.MaxCharSize);
    WriteResult(tag, (BYTE)ok, buf);
}


// Glyph-level replacement:
// GetGlyphOutlineW/A. The injected DLL maps あ (U+3042) -> 阿 before the outline
// is fetched; the rewrite is verified from HookFont.log ("[CharMap]
// GetGlyphOutline*: U+3042 -> U+963F"). Here we only prove the calls succeed
// (return value != GDI_ERROR) and don't crash. GGO_METRICS avoids any need for a
// real glyph bitmap.
static void TestGlyphOutline(const char* tag)
{
    HDC hdc = CreateCompatibleDC(NULL);
    if (!hdc) { WriteResult(tag, 0, L"DC_FAIL"); return; }

    HFONT hf = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"宋体");
    HGDIOBJ hOld = SelectObject(hdc, hf);

    MAT2 mat = { {0,1},{0,0},{0,0},{0,1} }; // identity transform
    GLYPHMETRICS gmW = { 0 };
    DWORD rw = GetGlyphOutlineW(hdc, 0x3042 /* あ */, 0 /* GGO_METRICS */, &gmW, 0, NULL, &mat);

    GLYPHMETRICS gmA = { 0 };
    DWORD ra = GetGlyphOutlineA(hdc, 0x3042 /* あ */, 0 /* GGO_METRICS */, &gmA, 0, NULL, &mat);

    SelectObject(hdc, hOld);
    DeleteObject(hf);
    DeleteDC(hdc);

    BOOL ok = (rw != GDI_ERROR && ra != GDI_ERROR);
    WriteResult(tag, (BYTE)ok, L"GLYPH_OK");
}


// GDI+ one-step paths: GdipCreateFont (family+size, family may be cached) and
// GdipCreateFontFromLogfontW (LOGFONT-based). The injected DLL replaces the face
// name, so the resulting font's family name should come back as the mapped font.
static void TestGdiplusCreate(const wchar_t* request, const char* tag)
{
    typedef int (WINAPI* FnStartup)(ULONG_PTR*, const void*, void*);
    typedef void (WINAPI* FnShutdown)(ULONG_PTR);
    typedef int (WINAPI* FnCreateFamily)(const WCHAR*, void*, void**);
    typedef int (WINAPI* FnCreateFont)(void*, float, int, int, void**);
    typedef int (WINAPI* FnGetFontFamily)(void*, void**);
    typedef int (WINAPI* FnGetFamilyName)(void*, WCHAR*, WCHAR);
    typedef int (WINAPI* FnDeleteFont)(void*);
    typedef int (WINAPI* FnDeleteFamily)(void*);
    typedef int (WINAPI* FnCreateFontFromLogfontW)(HDC, const LOGFONTW*, void**);

    HMODULE hGdiplus = LoadLibraryW(L"gdiplus.dll");
    if (!hGdiplus) { WriteResult(tag, 0, L"GDI+_LOAD_FAIL"); return; }

    FnStartup fnStartup = (FnStartup)GetProcAddress(hGdiplus, "GdiplusStartup");
    FnShutdown fnShutdown = (FnShutdown)GetProcAddress(hGdiplus, "GdiplusShutdown");
    FnCreateFamily fnCreateFamily = (FnCreateFamily)GetProcAddress(hGdiplus, "GdipCreateFontFamilyFromName");
    FnCreateFont fnCreateFont = (FnCreateFont)GetProcAddress(hGdiplus, "GdipCreateFont");
    FnGetFontFamily fnGetFontFamily = (FnGetFontFamily)GetProcAddress(hGdiplus, "GdipGetFamily");
    FnGetFamilyName fnGetFamilyName = (FnGetFamilyName)GetProcAddress(hGdiplus, "GdipGetFamilyName");
    FnDeleteFont fnDeleteFont = (FnDeleteFont)GetProcAddress(hGdiplus, "GdipDeleteFont");
    FnDeleteFamily fnDeleteFamily = (FnDeleteFamily)GetProcAddress(hGdiplus, "GdipDeleteFontFamily");
    FnCreateFontFromLogfontW fnFromLogW = (FnCreateFontFromLogfontW)GetProcAddress(hGdiplus, "GdipCreateFontFromLogfontW");

    ULONG_PTR token = 0;
    struct { UINT32 version; void* cb; BOOL suppressBg; BOOL suppressExt; } input = { 1, NULL, FALSE, FALSE };
    if (!fnStartup || !fnShutdown || !fnCreateFamily || !fnCreateFont || !fnGetFontFamily ||
        !fnGetFamilyName || !fnDeleteFont || !fnDeleteFamily || !fnFromLogW ||
        fnStartup(&token, &input, NULL) != 0) { WriteResult(tag, 0, L"GDI+_INIT_FAIL"); return; }

    // --- GdipCreateFont path (family created through GdipCreateFontFamilyFromName) ---
    void* family = nullptr;
    void* font = nullptr;
    wchar_t name1[64] = { 0 };
    if (fnCreateFamily(request, NULL, &family) != 0 || !family) { wcscpy_s(name1, L"FAMILY_FAIL"); }
    else if (fnCreateFont(family, 16.0f, 0, 0, &font) != 0 || !font) { wcscpy_s(name1, L"CREATEFONT_FAIL"); }
    else
    {
        void* familyOut = nullptr;
        if (fnGetFontFamily(font, &familyOut) != 0 || !familyOut) wcscpy_s(name1, L"GETFAM_FAIL");
        else { fnGetFamilyName(familyOut, name1, 0); fnDeleteFamily(familyOut); }
        fnDeleteFont(font);
    }
    if (family) fnDeleteFamily(family);
    WriteResult(tag, 0, name1);

    // --- GdipCreateFontFromLogfontW path ---
    LOGFONTW lf = { 0 };
    lf.lfHeight = 16;
    wcscpy_s(lf.lfFaceName, request);
    void* font2 = nullptr;
    wchar_t name2[64] = { 0 };
    if (fnFromLogW(GetDC(NULL), &lf, &font2) != 0 || !font2) { wcscpy_s(name2, L"FROMLOG_FAIL"); }
    else
    {
        void* familyOut2 = nullptr;
        if (fnGetFontFamily(font2, &familyOut2) != 0 || !familyOut2) wcscpy_s(name2, L"GETFAM_FAIL");
        else { fnGetFamilyName(familyOut2, name2, 0); fnDeleteFamily(familyOut2); }
        fnDeleteFont(font2);
    }
    std::string tagLog = std::string(tag) + "-Log";
    WriteResult(tagLog.c_str(), 0, name2);

    fnShutdown(token);
}


int main()
{
    // give the injected DLL's worker thread time to install the hooks
    Sleep(800);

    DeleteFileW(L"TestTarget_result.txt");

    TestCreateFontA("Arial", "CreateFontA");
    TestCreateFontIndirectA("Arial", "CreateFontIndirectA");
    TestCreateFontW(L"Arial", "CreateFontW");
    TestCreateFontIndirectW(L"Arial", "CreateFontIndirectW");
    TestCreateFontW(L"MS Gothic", "CreateFontW-Map");
    TestCreateFontIndirectW(L"MS Gothic", "CreateFontIndirectW-Map");
    TestCreateFontW(L"MS PGothic", "CreateFontW-Wildcard");
    TestDirectWrite(L"Arial", "DWrite");
    TestDirectWrite(L"MS Gothic", "DWrite-Map");
    TestDirectWriteLayout(L"Arial", "DWrite-Layout");
    TestDirectWriteLayout(L"MS Gothic", "DWrite-Layout-Map");
    TestGdiplus(L"Arial", "Gdiplus");
    TestGdiplus(L"MS Gothic", "Gdiplus-Map");
    TestGdiplusCreate(L"MS Gothic", "GdiplusCreate");
    TestSetWindowTextW("WindowTitle");
    TestTextOut("TextOut");
    TestAutoSC("AutoSC");
    TestGlyphOutline("Glyph");
    TestCharsetSpoof();
    TestFontMetrics("FontMetrics");
    TestRenderTweaks("RenderTweaks");
    TestTextMetrics("TextMetrics");
    TestCodePage("CodePage");

    return 0;
}
