# WinKeyGuard

轻量、绿色、无需安装的 Windows 游戏快捷键防误触工具。

> Lightweight, portable, no-install Windows hotkey guard for games.

当目标游戏处于前台并进入全屏 / 无边框全屏时，自动拦截左手区域容易误触的
Windows 系统快捷键组合；同时提供 Caps Lock 与输入法联动管理。

> Blocks Windows shortcuts (Win+D, Alt+Tab, ...) while a target game is in
> foreground fullscreen, and offers a Caps Lock ↔ IME linkage.

> 只拦截「修饰键 + 具体按键」，不简单禁用 Win / Ctrl / Alt / Shift 本身，
> 尽量不影响游戏自身的键盘操作。

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

界面语言跟随系统（简体中文 / English），也可在设置中手动选择。

## 它解决什么问题

玩全屏游戏时，左手区域（`Win`、`Alt+Tab`、`Win+D`、`Win+1~5`、`Ctrl+Esc`
等）很容易误触 Windows 系统快捷键，导致游戏被切出、打开开始菜单、切到桌面，
甚至弹出任务视图。WinKeyGuard 在游戏处于前台全屏时拦截这些组合，退出游戏
后自动恢复，无需手动开关。

## 主要功能

- **组合级拦截**：按「Modifier + Specific Key」精确匹配，不禁用修饰键本身。
  WASD、数字键、Ctrl+C/V、Alt+W 等游戏输入完全不受影响。
- **动态保护**：目标进程运行 + 前台 + 全屏 → 自动进入保护；切出 / 回桌面 / 后台
  自动关闭，重新回到游戏全屏自动恢复。
- **全屏检测**：结合前台窗口、窗口矩形、显示器矩形、窗口样式、DWM 信息与
  Per-Monitor V2 DPI 感知，支持独占全屏、无边框全屏、多显示器；另有
  「前台窗口模式」兼容特殊游戏。
- **Win + ` 特殊入口**：保护状态下按 `Win + ` ` ` 打开 **Windows 原生开始菜单**
  （按物理扫描码识别，不依赖键盘布局字符），返回游戏自动重新保护。
- **Caps Lock / 输入法联动**：Caps ON → 自动切换到英文输入法并禁止输入法切换
  快捷键；Caps OFF → 按用户选择处理并恢复正常切换。
- **紧急解除**：`Ctrl+Alt+F12` 一键暂停 / 恢复保护，最高优先级。
- **托盘常驻**：`config.json` 便携配置、用户级开机启动、可选以管理员身份运行。

## 系统要求

- Windows 10 / Windows 11（x64）
- 无需安装、无需管理员权限（拦截管理员权限运行的游戏时才建议以管理员运行）
- 不依赖驱动、不注入 DLL、不修改游戏文件

## 如何运行（便携版）

1. 下载 `WinKeyGuard-Portable.zip` 并解压。
2. 双击运行 `WinKeyGuard.exe`，程序常驻系统托盘。
3. 打开「目标程序」页，添加游戏 EXE（手动输入 / 从运行进程选择 / 浏览 EXE）。
4. 进入游戏全屏后自动保护；退出自动恢复。
5. 保护状态下按 `Win + ` ` ` 进入原生开始菜单。

配置保存在程序目录的 `config.json` 中，整个目录可复制到另一台电脑直接运行。

## 构建

依赖：CMake ≥ 3.21、Ninja、MSVC 2022、Qt 6（Widgets）。

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:\Qt\6.9.3\msvc2022_64
cmake --build build --config Release
```

便携打包（收集 Qt 运行库与 MSVC CRT）：

```powershell
windeployqt --release --no-translations --compiler-runtime build\WinKeyGuard.exe
```

## 项目结构

```
WinKeyGuard/
├── src/             # 源码
├── include/         # 头文件
├── resources/       # 图标、清单、版本资源
├── tests/           # 控制台自测
├── CMakeLists.txt
├── LICENSE
└── README.md
```

## 已知限制

- `Ctrl+Alt+Delete`（Windows 安全注意序列）普通用户态程序无法可靠屏蔽，仅提示。
- 以管理员权限运行的游戏中，普通权限的键盘 Hook 可能无法覆盖，需「以管理员身份运行」。
- 「Win 本身」为主开关：启用时吞掉 Win 键按下，天然阻断所有 Win+组合；
  单个 Win+组合的「允许」通过 `SendInput` 重放实现（仅纯 Win+单键规则）。

## 许可

[MIT](LICENSE)
