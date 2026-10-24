# SgsClient

SgsClient 是自主开发的多人在线回合制卡牌游戏 Windows 客户端。M1-C 提供遵循 SGS/1 的 Qt TCP 调试客户端；项目不依赖 FreeKill 运行。

## 技术栈

- C++20
- Qt 6（Core、Network、Quick、QML、Quick Controls 2、Qt Test）
- CMake + Ninja
- Windows 10/11；MinGW 64 位或与所装 Qt 匹配的 MSVC 64 位工具链

编译器必须与 Qt 二进制套件匹配：MinGW Qt 使用兼容的 MinGW，MSVC Qt 使用对应的 MSVC。不要混用两类套件。

## 依赖环境

- Qt 6.5 或更高版本，包含 Qt Network、Qt Quick、Qt QML、Qt Quick Controls 2
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

测试包括离屏 QML 冒烟测试，以及使用本机 `QTcpServer` 替身的协议/连接测试。本地 Socket 测试不等同于 Linux 服务端跨设备联调。自动化测试不能替代可见窗口检查。

## 当前功能

- 独立的 `Application` 启动外壳与 QML 引擎生命周期管理
- SGS/1 大端长度帧、严格 UTF-8/JSON 编解码与增量收包
- 异步 TCP 连接、Hello/HelloAck、Ping/Pong、Echo/EchoResult 和 Error 展示
- `Disconnected / Connecting / Handshaking / Ready` 状态与 10 秒握手超时
- 地址、端口、连接、断开、Ping、中文 Echo 和有限日志调试界面
- CTest/Qt Test 冒烟及本地模拟服务器测试

## 尚未实现

真实 Linux 服务端跨设备联调、TLS、认证、登录、大厅、房间、牌桌、卡牌、武将、技能、Lua、资源同步、自动重连和正式 UI 均未实现。

协议见 [docs/protocol.md](docs/protocol.md)，设计边界见 [docs/architecture.md](docs/architecture.md)，实测状态见 [docs/milestone_status.md](docs/milestone_status.md)。
