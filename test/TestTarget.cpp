// TestTarget.cpp — 冒烟测试目标：
// 被 TestLoader.exe 启动并注入 HookFont.dll 后，依次调用
// CreateFontA / CreateFontIndirectA / CreateFontW / CreateFontIndirectW，
// 再把实际生效的字符集与字体名写入 TestTarget_result.txt。
// 若字体替换生效，四个结果都应为 Charset=0x86, Font=黑体。
#include <windows.h>
#include <cstdio>
#include <cwchar>

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

static void TestCreateFontA()
{
    HFONT hf = CreateFontA(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
    LOGFONTA lf = { 0 };
    GetObjectA(hf, sizeof(lf), &lf);
    wchar_t face[MAX_PATH] = { 0 };
    MultiByteToWideChar(CP_ACP, 0, lf.lfFaceName, -1, face, MAX_PATH);
    WriteResult("CreateFontA", lf.lfCharSet, face);
    DeleteObject(hf);
}

static void TestCreateFontIndirectA()
{
    LOGFONTA lf = { 0 };
    lf.lfHeight = 16;
    lf.lfCharSet = ANSI_CHARSET;
    lstrcpyA(lf.lfFaceName, "Arial");
    HFONT hf = CreateFontIndirectA(&lf);
    LOGFONTA lfOut = { 0 };
    GetObjectA(hf, sizeof(lfOut), &lfOut);
    wchar_t face[MAX_PATH] = { 0 };
    MultiByteToWideChar(CP_ACP, 0, lfOut.lfFaceName, -1, face, MAX_PATH);
    WriteResult("CreateFontIndirectA", lfOut.lfCharSet, face);
    DeleteObject(hf);
}

static void TestCreateFontW()
{
    HFONT hf = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
    LOGFONTW lf = { 0 };
    GetObjectW(hf, sizeof(lf), &lf);
    WriteResult("CreateFontW", (BYTE)lf.lfCharSet, lf.lfFaceName);
    DeleteObject(hf);
}

static void TestCreateFontIndirectW()
{
    LOGFONTW lf = { 0 };
    lf.lfHeight = 16;
    lf.lfCharSet = ANSI_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Arial");
    HFONT hf = CreateFontIndirectW(&lf);
    LOGFONTW lfOut = { 0 };
    GetObjectW(hf, sizeof(lfOut), &lfOut);
    WriteResult("CreateFontIndirectW", (BYTE)lfOut.lfCharSet, lfOut.lfFaceName);
    DeleteObject(hf);
}

int main()
{
    // give the injected DLL's worker thread time to install the hooks
    Sleep(800);

    DeleteFileW(L"TestTarget_result.txt");
    TestCreateFontA();
    TestCreateFontIndirectA();
    TestCreateFontW();
    TestCreateFontIndirectW();

    return 0;
}
