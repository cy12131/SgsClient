# SgsClient

SgsClient 是自主开发的多人在线回合制卡牌游戏 Windows 客户端。M3-A-C 在 SGS/1 TCP、大厅和等待房间基础上增加服务端锁定资源包的自动同步；项目不依赖 FreeKill 运行。

## 技术栈

- C++20
- Qt 6（Core、Concurrent、Network、Quick、QML、Quick Controls 2、Qt Test）
- CMake + Ninja
- Windows 10/11；MinGW 64 位或与所装 Qt 匹配的 MSVC 64 位工具链

编译器必须与 Qt 二进制套件匹配：MinGW Qt 使用兼容的 MinGW，MSVC Qt 使用对应的 MSVC。不要混用两类套件。

## 依赖环境

- Qt 6.5 或更高版本，包含 Qt Network、Qt Quick、Qt QML、Qt Quick Controls 2
- CMake 3.21 或更高版本
- 支持 C++20 的编译器
- Ninja（推荐）或其他受 CMake 支持的生成器
- Git 命令行客户端（资源包通过公开 HTTPS Git 仓库同步）

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

测试包括离屏 QML 冒烟测试、本机 `QTcpServer` 协议/大厅替身，以及使用临时 Git 仓库的资源包事务测试。公开 CNB 实测可设置 `SGS_RUN_PUBLIC_PACKAGE_TEST=1` 后单独运行 `sgs-client-package-sync-test publicCnbFirstDownloadAndCache`；普通 CTest 不依赖公网。

## 当前功能

- 独立的 `Application` 启动外壳与 QML 引擎生命周期管理
- SGS/1 大端长度帧、严格 UTF-8/JSON 编解码与增量收包
- 异步 TCP 连接、Hello/HelloAck、Ping/Pong、Echo/EchoResult 和 Error 展示
- `Disconnected / Connecting / Handshaking / Ready` 状态与 10 秒握手超时
- 临时昵称进入大厅、房间列表刷新、创建与加入房间
- 服务端权威的固定座位、房主与准备状态，以及离开房间
- `RoomListChanged` 自动刷新、`RoomSnapshot` revision 去重和断线清理
- `GetPackageManifest → PackageReadyResult` 大厅门禁及空清单确认
- 后台 HTTPS Git 下载、完整 commit/Manifest SHA-256/依赖/API/路径校验
- staging 安装、不可变 commit 目录和 `active.json` 原子激活；失败保留上一可用集合
- 独立同步状态、进度、错误原因与重试界面
- 连接页、大厅页、等待房间页，以及 CTest/Qt Test 本地模拟服务器测试

## 尚未实现

M3-A 客户端尚未与真实 Linux M3 服务端联调（本任务未提供独立 M3 测试地址）。持久账号、匹配、开局、牌桌、Lua 执行、动态 QML 游戏宿主、资源包热加载、私有仓库凭据和自动重连均未实现。

协议见 [docs/protocol.md](docs/protocol.md)，设计边界见 [docs/architecture.md](docs/architecture.md)，实测状态见 [docs/milestone_status.md](docs/milestone_status.md)。
