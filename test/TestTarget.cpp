// TestTarget.cpp — 冒烟测试目标：
// 被 TestLoader.exe 启动并注入 HookFont.dll 后，依次调用 GDI CreateFont*
// 四个 API 与 DirectWrite CreateTextFormat，把实际生效的字符集与字体名写入
// TestTarget_result.txt。
//
// 测试用例（对应 test\HookFont.ini）：
//   - 未命中 [FontMap] 的字体（"Arial"）走全局候选列表 FontName=宋体,黑体 → 宋体
//   - 命中 [FontMap] 的字体（"MS Gothic"）用映射值 → 黑体
//   - DirectWrite CreateTextFormat 同样被替换
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
    TestDirectWrite(L"Arial", "DWrite");
    TestDirectWrite(L"MS Gothic", "DWrite-Map");

    return 0;
}
