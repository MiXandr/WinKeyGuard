# WinKeyGuard

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> **本软件由 AI 制作。**

本工具由 AI 生成，主要为个人自用场景设计。因开发方式特殊，可能存在未知问题。
欢迎提交 Bug，我会在能力范围内尽力修复，但无法承诺即时处理。感谢你的理解与支持。

---

## 这是什么

WinKeyGuard 是一款轻量、绿色、无需安装的 Windows 游戏快捷键防误触工具。

当游戏处于前台全屏时，它会自动拦截左手区域容易误触的 Windows 系统快捷键
（如 `Win`、`Win + D`、`Alt + Tab` 等），避免游戏被切出、弹出开始菜单或切到桌面；
同时提供 Caps Lock 与输入法联动，防止游戏中误触中文输入法。

它只拦截「修饰键 + 具体按键」的组合，不会禁用 Win / Ctrl / Alt / Shift 本身，
因此游戏里的 WASD、数字键、Ctrl+C/V 等操作不受影响。

## 主要功能

- **组合级拦截**：只拦截配置里指定的 Windows 快捷键组合，其余按键正常放行。
- **动态保护**：目标游戏前台全屏时自动开启，切出或回桌面后自动关闭，回到游戏自动恢复。
- **全屏检测**：支持独占全屏、无边框全屏、多显示器和 DPI 缩放；特殊游戏可用「前台窗口模式」兼容。
- **Win + ` 特殊入口**：保护状态下按 `Win` + \` 打开 Windows 原生开始菜单，回到游戏后保护自动恢复。
- **Caps Lock / 输入法联动**：Caps ON 自动切英文并禁止切换输入法快捷键；Caps OFF 按设置处理并恢复。
- **紧急解除**：`Ctrl+Alt+F12` 一键暂停 / 恢复保护。
- **托盘常驻**：`config.json` 便携配置，支持开机启动。

## 系统要求

- Windows 10 / Windows 11（x64）
- 无需安装、无需管理员权限（拦截管理员权限运行的游戏时建议以管理员运行）
- 不依赖驱动、不注入 DLL、不修改游戏文件

## 使用说明

1. 下载并解压 `WinKeyGuard-Portable.zip`。
2. 运行 `WinKeyGuard.exe`，常驻系统托盘。
3. 在「目标程序」页添加游戏 EXE。
4. 进入游戏全屏后自动保护，退出自动恢复。
5. 保护状态下按 `Win` + \` 打开原生开始菜单。

配置保存在程序目录的 `config.json` 中，整个目录可复制到另一台电脑使用。

## 构建

依赖：CMake ≥ 3.21、Ninja、MSVC 2022、Qt 6（Widgets）。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:\Qt\6.9.3\msvc2022_64
cmake --build build --config Release
```

便携打包：

```powershell
windeployqt --release --no-translations --compiler-runtime build\WinKeyGuard.exe
```

## 项目结构

```
WinKeyGuard/
├── src/             # 源码
├── include/         # 头文件
├── resources/       # 图标、清单、版本资源
├── translations/    # 翻译源
├── tests/           # 控制台自测
├── CMakeLists.txt
├── LICENSE
└── README.md
```

## 已知限制

- `Ctrl+Alt+Delete` 是 Windows 安全序列，普通用户态程序无法屏蔽。
- 拦截管理员权限运行的游戏时，需以管理员身份运行 WinKeyGuard。

## 许可

[MIT](LICENSE)
