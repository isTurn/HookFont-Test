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
    TestSetWindowTextW("WindowTitle");

    return 0;
}
