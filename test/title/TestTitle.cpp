// TestTitle.cpp — 验证窗口标题替换 Hook（HookWindowTitle）
// 被注入后创建一个标题为 TEST_RAW 的窗口，检查实际标题是否被替换为 TEST_NEW
#include <windows.h>
#include <cstdio>

int main()
{
    Sleep(800); // 等待注入 DLL 的工作线程完成 Hook

    HWND hwnd = CreateWindowExA(0, "STATIC", "TEST_RAW", WS_OVERLAPPEDWINDOW,
                                0, 0, 200, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);

    char title[128] = { 0 };
    if (hwnd) GetWindowTextA(hwnd, title, 127);

    FILE* f = nullptr;
    fopen_s(&f, "TestTitle_result.txt", "w");
    if (f)
    {
        fprintf(f, "title=%s\r\n", title);
        fclose(f);
    }

    if (hwnd) DestroyWindow(hwnd);
    return 0;
}
