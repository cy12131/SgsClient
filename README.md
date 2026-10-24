# SgsClient

SgsClient 是自主开发的多人在线回合制卡牌游戏 Windows 客户端。本仓库当前处于 M0：提供独立、可构建、可测试的 Qt Quick/QML 应用外壳，不包含联网与游戏逻辑，也不依赖 FreeKill 运行。

## 技术栈

- C++20
- Qt 6（Quick、QML、Quick Controls 2、Qt Test）
- CMake + Ninja
- Windows 10/11；MinGW 64 位或与所装 Qt 匹配的 MSVC 64 位工具链

编译器必须与 Qt 二进制套件匹配：MinGW Qt 使用兼容的 MinGW，MSVC Qt 使用对应的 MSVC。不要混用两类套件。

## 依赖环境

- Qt 6.5 或更高版本，包含 Qt Quick、Qt QML、Qt Quick Controls 2
- CMake 3.21 或更高版本
- 支持 C++20 的编译器
- Ninja（推荐）或其他受 CMake 支持的生成器

## 配置与构建

以下示例使用本机 Qt 6.11.2 MinGW 套件。`CMAKE_PREFIX_PATH` 由命令行或 IDE Kit 提供，项目本身不写死 Qt 安装位置。

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64 `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
cmake --build build
```

若 `cmake`/`ninja` 未加入 `PATH`，可用 Qt 安装目录下工具的完整路径执行同样命令。

## 运行

构建后运行：

```powershell
.\build\bin\sgs-client.exe
```

开发环境中需确保匹配套件的 Qt `bin` 和编译器运行库可被找到；发布时可用 `windeployqt` 部署依赖，本里程碑不制作安装包。

## 测试

```powershell
ctest --test-dir build --output-on-failure
```

自动测试在 Qt `offscreen` 平台上真正创建 QML 主窗口后退出，用于发现缺失模块和 QML 加载错误。它不能替代人工可见窗口检查；M0 验收还需启动正常程序，确认窗口显示、标题正确且能够关闭。

## 当前功能

- 独立的 `Application` 启动外壳与 QML 引擎生命周期管理
- “SGS Client”桌面主窗口、版本显示和基础界面
- “进入游戏”占位入口，点击后明确提示功能开发中
- CTest/Qt Test 冒烟测试

## 尚未实现

网络通信、协议、账号、大厅、房间、牌桌、卡牌、武将、技能、Lua、皮肤、音效、动画和正式资源系统均不在 M0 范围内。

更多设计边界见 [docs/architecture.md](docs/architecture.md)，实测状态见 [docs/milestone_status.md](docs/milestone_status.md)。
