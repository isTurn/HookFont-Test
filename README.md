<p align="center">
  <img src="assets/cover.png" alt="HookFont" width="100%" />
</p>

# HookFont — 日系 Galgame 字体替换 / 汉化工具链

<div align="center">

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
![Platform](https://img.shields.io/badge/Platform-Windows-blue)
![Language](https://img.shields.io/badge/Language-C%2B%2B-00599C)
[![Release](https://img.shields.io/github/v/release/isTurn/HookFont-Test)](https://github.com/isTurn/HookFont-Test/releases)
[![build](https://github.com/isTurn/HookFont-Test/actions/workflows/build.yml/badge.svg)](https://github.com/isTurn/HookFont-Test/actions/workflows/build.yml)

一个用于**日系 Galgame（视觉小说）汉化与字体替换**的 Windows 工具链。

</div>

启动目标游戏进程，向其中注入 Hook DLL，强制把游戏创建的所有字体替换成指定字符集 + 字体，使日文游戏能正确显示中文。常用于配合机器翻译补丁 / 汉化补丁使用。

---

## ✨ 特性

- **强制字体替换**：Hook `CreateFontA/W`、`CreateFontIndirectA/W` 四个 GDI 字体创建 API，统一替换为配置的字符集（默认 `0x86` GB2312）+ 字体
- **DirectWrite 补全**：Hook `IDWriteFactory::CreateTextFormat`、`CreateTextLayout` / `CreateGdiCompatibleTextLayout` 及 `IDWriteTextLayout::SetFontFamilyName`，完整覆盖 WPF / Unity 等现代渲染引擎游戏（运行中改字体也生效）
- **GDI+ 支持**：Hook `GdipCreateFontFamilyFromName`，覆盖少数走 GDI+ 创建字体的老游戏 / 引擎
- **字体自动安装**：把字体文件放进 DLL 同目录 `fonts\`（`*.ttf/*.ttc/*.otf`），启动时自动 `AddFontResource` 注册，无需手动安装、免管理员权限
- **字体映射表**：`[FontMap]` 段支持"原字体名 → 新字体名"替换，优先于全局替换；支持 `*` / `?` 通配符模糊匹配（精确匹配优先）
- **候选字体回退**：`FontName` 支持逗号分隔的候选列表，自动选用第一个系统已安装的字体
- **Detours 注入**：基于 Microsoft Detours 挂起创建目标进程并注入 DLL，稳定可靠
- **注入已运行进程**：`HookFont.exe -pid <PID>` 直接向已运行的进程注入（Steam / 官方启动器拉起游戏、需进主菜单后再挂补丁等场景）
- **命令行启动**：`HookFont.exe <game.exe> [参数...]` 直接指定要启动的游戏并透传启动参数，无需改 ini
- **x86 / x64 双平台**：32 位与 64 位游戏均可使用，仓库附带 Detours x86 + x64 静态库
- **窗口标题替换**：Hook `CreateWindowExA/W` + `SetWindowTextA/W`，创建窗口与运行时改标题都会被替换（宽字符安全）
- **免配置环境**：配置按程序自身目录解析，不依赖当前工作目录；中文路径自动转 8.3 短路径
- **延迟 Hook**：从工作线程延迟执行 Hook，规避加载器锁死锁风险
- **日志排查**：运行日志落盘（`HookFont.log`），异常可追查，不弹窗卡游戏

## 🗂 组成

```
HookFont.sln
├── src
│   ├── HookFont            HookFont.dll（注入用 Hook 插件）
│   │   └── dllmain.cpp     入口：延迟到工作线程执行 Hook
│   └── RiaLoader           RiaLoader.exe（启动器，成品改名为 HookFont.exe）
│       └── RiaLoader.cpp   用 Microsoft Detours 创建游戏进程并注入 DLL
├── lib
│   └── Rxx                 个人工具库（静态库）
│       ├── Hook*           Detours 封装 + 字体/窗口 Hook 实现
│       ├── INI*            UTF-8 INI 解析（Rcf::INI）
│       ├── File*/Str*/Mem* 路径 / 字符串 / 内存工具
│       └── Console*        控制台工具
├── assets                  README 封面等静态资源
└── third
    └── detours             Microsoft Detours（x86 + x64 头文件与库）
```

## 🔄 工作原理

```mermaid
flowchart LR
    A[双击 HookFont.exe] --> B[读取同目录 HookFont.ini]
    B --> B2[自动注册 fonts\\ 目录字体]
    B2 --> C[Detours 挂起创建游戏进程<br/>并注入 HookFont.dll]
    C --> D[HookFont.dll 加载]
    D --> E[工作线程延迟执行 Hook]
    E --> F[Hook CreateFontA/W<br/>CreateFontIndirectA/W]
    E --> G[Hook DirectWrite<br/>CreateTextFormat / CreateTextLayout]
    E --> G2[Hook GDI+<br/>GdipCreateFontFamilyFromName]
    F --> H[按 FontMap 映射（含通配）/ 候选字体<br/>强制替换字符集 + 字体]
    G --> H
    G2 --> H
    H --> I[日文游戏正确显示中文]
    E --> J[可选：替换窗口标题<br/>CreateWindowEx + SetWindowText]
```

## 🚀 使用（部署给玩家）

把以下 3 个文件复制到游戏目录，编辑 `HookFont.ini` 后双击 `HookFont.exe`：

| 文件 | 说明 |
|---|---|
| `HookFont.dll` | 注入用 Hook 插件 |
| `HookFont.exe` | 启动器（RiaLoader 改名） |
| `HookFont.ini` | 配置文件（UTF-8 编码） |

`HookFont.ini` 示例：

```ini
[RiaLoader]
TargetEXE = ojyousama.exe
TargetDLLCount = 2
TargetDLLName_0 = HookFont.dll
TargetDLLName_1 = kDays.dll

[HookFont]
Charset = 0x86
FontName = 黑体, 微软雅黑, 宋体
HookCreateFontA = true
HookCreateFontIndirectA = true
HookCreateFontW = true
HookCreateFontIndirectW = true
HookDirectWrite = true
HookGdiplus = true
AutoInstallFonts = true
HookWindowTitle = false

[FontMap]
MS Gothic = 黑体
MS* = 黑体        ; 通配符：所有 MS 开头的字体都换成黑体
```

> 详细部署与排查说明见 [USAGE.txt](USAGE.txt)。

**命令行用法**（可选，默认双击启动器即可）：

```
HookFont.exe <游戏exe路径> [游戏参数...]   # 直接启动指定游戏并透传参数
HookFont.exe -pid <进程ID>                # 注入到已运行的进程（位数需匹配）
```

**约定与注意**

- 所有 INI 文件**编码必须是 UTF-8**；`xxx.ini` 必须与 `xxx.dll` / `xxx.exe` 同名
- 配置文件按**程序自身所在目录**解析，不依赖“当前工作目录”，从任意位置双击运行均可
- 启动器以**游戏所在目录**作为目标进程的工作目录，保证游戏相对路径读取正常
- 游戏目录含中文等非 ASCII 路径时，注入 DLL 会自动转换为短路径（8.3）以兼容 Detours 的 ANSI 接口
- 运行后生成 `HookFont.log`（DLL 侧）与 `HookFont.exe` 同名的 `.log`（启动器侧），异常时优先查看日志

## 🔧 构建

环境：**Visual Studio 2022**（v143 工具集，MSVC）。用 VS 打开 `HookFont.sln`，选 **Release | x86** 或 **Release | x64** 生成即可。产出分别在 `Release\`（x86）与 `x64\Release\`（x64）：

- `HookFont.dll`
- `RiaLoader.exe`（部署时改名为 `HookFont.exe`）
- `Rxx.lib`

命令行构建：

```
MSBuild.exe HookFont.sln /t:Rebuild /p:Configuration=Release /p:Platform=x86
MSBuild.exe HookFont.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

仓库已配置 [GitHub Actions 自动构建](.github/workflows/build.yml)（`windows-latest` + MSBuild），每次 push 到 `main` 会自动编译 **x86 / x64 双版本**并在 Artifacts 中产出。

### x86 / x64 说明

仓库附带 Microsoft Detours **x86 与 x64 双平台静态库**（`third\detours\lib.X86` / `lib.X64`），两种位数开箱即用。按游戏位数选择对应版本：32 位游戏用 x86，64 位游戏用 x64。

## 📦 与旧版（改进前）的差异

- 修复编译错误：废弃的 `std::locale::empty()`、`File.h` 误引用 `String.h`
- 修复逻辑 Bug：Detours 封装返回值反转；INI 无符号整数固定按 16 进制解析；内存搜索失败即 `ExitProcess` 杀进程；ANSI 路径函数对多字节字符的截断判断
- 配置 / 目标路径改为基于程序自身目录解析，不再受“当前工作目录”影响
- Hook 从 `DllMain` 内直接执行改为**工作线程延迟执行**，避免加载器锁死锁风险，并调用 `DisableThreadLibraryCalls`
- 新增 `CreateFontW` / `CreateFontIndirectW` 两个 Unicode 版 Hook；接通原本已实现但未接线的窗口标题替换 `HookTitleExA`
- 新增 **DirectWrite 引擎支持**（`IDWriteFactory::CreateTextFormat` vtable Hook，无 dwrite.lib 依赖），覆盖 WPF/Unity 等现代引擎游戏
- 新增 **DirectWrite 补全**：`CreateTextLayout` / `CreateGdiCompatibleTextLayout` 与 `IDWriteTextLayout::SetFontFamilyName`（运行时改字体也替换）
- 新增 **GDI+ 支持**：Hook `GdipCreateFontFamilyFromName`
- 新增 **字体自动安装**：DLL 同目录 `fonts\` 子目录字体自动 `AddFontResource` 注册
- 新增 **`[FontMap]` 字体映射表**：按"原字体名 → 新字体名"替换，优先于全局替换；支持 `*` / `?` 通配模糊匹配（保序、精确优先）
- 新增 **候选字体回退**：`FontName` 支持逗号分隔列表，自动选用第一个已安装字体
- 窗口标题替换升级为 **`CreateWindowExA/W` + `SetWindowTextA/W` 四 API 宽字符 Hook**，运行中改标题也生效
- 新增 **注入已运行进程**（`-pid`）与**命令行启动 / 参数透传**
- 新增 **x64 支持**：附带 Detours x64 静态库，`Release | x64` 开箱即用
- 注入 DLL 内的失败提示由弹窗改为**日志文件**，避免弹窗卡死游戏
- 启动器自动把游戏目录设为目标进程工作目录；中文路径自动转短路径注入
- 编译告警清零（`/W3` 下无 warning），x64 类型安全

## 📄 License

[MIT](LICENSE) © isTurn

---

*字体替换依赖 [Microsoft Detours](https://github.com/microsoft/Detours)（MIT），随仓库附带 x86 与 x64 版。*
