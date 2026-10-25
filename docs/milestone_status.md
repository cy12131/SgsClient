# SgsClient 里程碑状态

## M3-A-C：客户端资源包自动同步

完成日期：2026-10-26（Asia/Shanghai）

### 项目、基线与分支

- 正式目录：`D:\Game\NewKill`
- M2 基线：`v0.3.0-m2` / `27abb310e3af01110d57e22db7f74758d1504a2a`
- 开发分支：`feature/m3-a-client-packages`
- 契约基线：SgsArchitecture `422016ed12a6c982bdd9e72f75590ec4ca9b5614`
- 服务端参考：SgsServer `ee022ef9979295a15482c52b17df7c9a4de4b093`

### 新增模块

- `PackageSyncManager`：每 Session 的清单请求、后台任务、PackageReady 门禁、取消/重试和 QML 状态。
- `PackageInstaller`：安全 Git 子进程、staging、commit/Manifest/路径校验、不可变缓存和原子 active 索引。
- `package_manifest`：同步清单严格解析、API/依赖图与 SemVer 比较器范围验证、manifest_id。
- `jcs_canonicalizer`：RFC 8785 确定性 JSON 编码。
- `PackageSyncPanel.qml`：当前包、阶段、错误、安装路径和重试操作。
- `package_sync_test.cpp`：本地 Git、回环协议门禁、事务失败及主线程响应性测试。

### 状态机与执行链

`Disconnected → Handshaking → FetchingManifest → CheckingPackages → Downloading → Verifying → Ready/Failed`。HelloAck 只使 SGS/1 通信可用；所有包成功原子激活并收到 `PackageReadyResult.ready=true` 后，LobbyModel 才发送 EnterLobby。空清单也写入空集合并发送 PackageReady。断线取消 worker、删除未完成 staging、清除 Session 状态；重连完整重走清单和 Ready。

默认安装根为 `QStandardPaths::AppLocalDataLocation/packages`。版本目录是 `installed/<id>/<commit>`；`active.json` 是当前集合的唯一提交点，更新前保留 `active.previous.json`。失败或取消不会改动旧 active；已修改的本地 commit 目录会以 `LOCAL_PACKAGE_MODIFIED` 失败，不被覆盖。

### 实际测试结果

- M2 开发前基线：Debug、Release 原有 3/3 CTest 均通过。
- Debug：配置、完整编译成功；新增后最终全量 4/4 CTest 通过。
- Release：独立配置、完整编译成功；新增后最终全量 4/4 CTest 通过。
- 本地资源包测试覆盖空清单、首装、缓存命中、缺包、commit 变化、无效 URL、目标 commit 不存在、Manifest 哈希失败、依赖缺失/版本不符/环、失败保留旧 active、错误 manifest_id、PackageReady、断线重同步及 Qt 事件循环响应。
- 真实公开 CNB：从 `https://cnb.cool/sgs-project/sgs-test-mode.git` 下载提交 `9d38a4a970448ca17bfbf7c61f20ab77f6f12451`，原始 Manifest SHA-256 `d267e42891b2ac4729a21e90ac44a8f6aa1a285d4f14ef8ed68f351ec29b9941`；首次安装与第二次缓存复用均实际 PASS。
- 真实 Linux M3 服务端联调：未执行。本任务没有提供独立 M3 测试服务器地址，且按限制未触碰既有 M2 服务；不得把本地 `QTcpServer` 或公开 Git 测试记为跨设备联调。
- Release GUI：在真实 Windows 桌面启动并实际显示；UI Automation 与截图确认资源包同步面板、未连接状态和路径 `C:/Users/cy/AppData/Local/SgsClient/SGS Client/packages` 均可见，随后通过窗口关闭按钮正常关闭。未连接独立 M3 服务端，因此未把 GUI 内的远程 PackageReady 流程记为通过。

### 安全与范围

生产只接受无内嵌凭据的 HTTPS 仓库；禁用 Git 交互、系统/全局配置、Hook、file/ext 协议，不输出仓库 URL 或 Git stderr，未执行安装脚本或 submodule。Git 与校验全部在 QtConcurrent worker 中运行。M3-A 未实现 Lua、GameProcessManager、GameViewContext、动态 QML 宿主或热加载，也未修改 SgsServer/SgsArchitecture/CNB 仓库。

最终 feature 分支提交、远程推送哈希与工作区状态以本阶段 Git 交付报告为准。

## M2-C：大厅与等待房间客户端

完成日期：2026-10-25（Asia/Shanghai）

### 1. 项目与 Git

- 绝对路径：`D:\Game\NewKill`
- 开发分支：`feature/m2-lobby-room-client`
- 基线：`7be707e merge: complete SGS client M1`
- 本阶段只在功能分支开发；不合并 `main`，不推送远程。

### 2. 实现内容与文件

- `src/models/lobby_model.h/.cpp`：临时身份、应用业务阶段、房间列表、创建/加入和列表刷新。
- `src/models/room_model.h/.cpp`：完整房间快照、revision、固定座位、房主/准备、退出及清理。
- `src/network/game_client.*`：在 M1 单连接基础上增加通用业务请求、Result/Error 匹配和通知分发。
- `src/protocol/message_codec.*`：接收端允许 `RoomSnapshot`/`RoomListChanged` 使用 request_id 0；发送端仍禁止 0。
- `qml/Shell.qml`、`qml/Lobby.qml`、`qml/WaitingRoom.qml`：连接页、大厅页和 2～8 人等待房间。
- `tests/lobby_room_test.cpp`：M2 协议和模型的本地 `QTcpServer` 自动化测试。
- 更新 CMake、README、架构、协议和冒烟测试。

### 3. 对象关系与生命周期

`Application` 依次拥有 `GameClient`、`RoomModel`、`LobbyModel`、`QQmlApplicationEngine`。QML 引擎最后构造、最先析构，随后大厅模型、房间模型和网络对象逆序销毁，避免 QML 或模型引用已经析构的底层对象。全程复用 M1 的单一 `QTcpSocket` 与逐 Socket `NoProxy`，没有创建大厅专用连接。

`GameClient` 只负责传输、握手、请求编号/匹配和通知分流；`LobbyModel`/`RoomModel` 负责业务状态投影；QML 只负责表现和提交用户操作，不伪造服务端权威状态。

### 4. 业务执行链

- EnterLobby：用户提交地址、端口、昵称 → TCP/Hello 成功 → `LobbyModel` 发送 EnterLobby → 校验 EnterLobbyResult → 保存正整数 user_id/服务端昵称 → 请求 ListRooms → 进入 Lobby。
- CreateRoom/JoinRoom：发送请求 → 校验匹配 Result 和 room_id → 等待随后 `RoomSnapshot` → 由快照激活 `RoomModel` 并进入 InRoom；Result 与快照同次 `readyRead` 也可处理。
- SetReady：发送意图 → 收到 SetReadyResult 只结束请求 → 等待 revision 更新的 RoomSnapshot 才改变显示，不做乐观更新。
- LeaveRoom：LeaveRoomResult 后清空房间并返回大厅，再请求最新列表。

### 5. 通知、revision 与座位

`RoomSnapshot` 和 `RoomListChanged` 只有 request_id 0 才作为通知接收。快照校验房间编号、名称、容量、waiting 状态、房主、玩家唯一性、座位范围及当前用户存在；同房间只接受严格更大的 revision。QML 按服务端 1～max_players 的 seat 映射固定槽位，空缺不会重排。

`RoomListChanged` 不携带列表，收到后产生新的 ListRooms 请求；模型记录最新列表请求 ID，旧响应不能覆盖新列表。

断线会清除 user_id、昵称、房间列表、当前房间、待处理请求和协议缓冲并返回连接页。重新连接必须重新 Hello 和 EnterLobby，不恢复旧会话。

### 6. 构建与自动化测试（已实际测试）

| 构建类型 | 配置/编译 | CTest |
| --- | --- | --- |
| Debug | Qt 6.11.2 + MinGW 13.1，成功 | 3/3 通过，0 失败，最终复核 6.56 秒 |
| Release | Qt 6.11.2 + MinGW 13.1，成功 | 3/3 通过，0 失败，最终复核 6.08 秒 |

CTest 包含 QML 冒烟、M1 网络回归和 M2 大厅/房间测试。M2 测试实际覆盖 EnterLobby/ListRooms、创建与加入、Result+Snapshot 粘连、通知 ID 规则、列表刷新、成员增减、座位空缺、准备/取消准备、房主变更、相同/旧 revision、业务错误、退出、断线清理和重连。测试使用本机回环 `QTcpServer`，不是跨设备联调。

### 7. Release GUI 与真实网络（已实际测试）

Release GUI 在正常 Windows 桌面平台实际显示，连接页标题、版本、服务器/端口/中文昵称字段、状态和日志控件均正常，最后正常关闭。

Windows `10.0.233.61` 经“以太网 3”访问 `10.0.172.236:18765`，`Test-NetConnection` 成功。Release GUI 实际完成 TCP 连接、Hello #1 和 HelloAck，服务端自报 `SgsServer 0.1.0-dev`；随后 EnterLobby #2 收到 `UNKNOWN_MESSAGE_TYPE: Unsupported message type`。客户端没有错误进入大厅，清晰显示原始错误码，主动断开后回到未连接并正常关闭。

上述结果证明真实跨设备的 M1 传输/握手和 M2 失败保持机制工作，但目标地址仍运行不支持大厅消息的 M1 服务，因此不能记为真实 M2-S 大厅/房间联调通过。需在 Linux 侧切换至参考提交 `a95f424b82146c059d4d1d8ee7a3304a70d9c5f4` 后补验真实大厅、创建/加入、通知、准备和退出流程。

### 8. 素材与界面

开发时检查了用户提供的 FreeKill 0.5.25 中大厅/等待房间 QML、`image/background.jpg`、`image/button/tileicon/create_room.png` 及 GPLv3 许可。为避免把未单独核实授权边界的媒体提交到公开仓库，本阶段未复制其 QML 或图片；实际界面全部使用自制布局、颜色、形状、文字和 Unicode 图标。未引入武将、皮肤、语音、动画或资源同步系统。

### 9. 已知问题与待联调事项

- 真实 Linux 地址当前运行 M1 服务，M2 跨设备大厅/房间验收待服务端切换后进行。
- M2 没有开局、持久账号、聊天、会话恢复或自动重连，符合范围。
- 旧的 `qml/Main.qml` 仍作为未加载的 M1 界面源文件保留；当前根组件为 `Shell.qml`，运行时不会实例化旧页面。
- Qt 的可选 Vulkan headers 提示不影响本项目构建和运行。

## M1-C：Qt TCP 客户端与 SGS/1

完成日期：2026-10-24（Asia/Shanghai）

### 1. 项目与 Git

- 绝对路径：`D:\Game\NewKill`
- 开发分支：`feature/m1-network-client`
- 基线：`3e589b0 chore: initialize SGS client M0`
- 本阶段按要求不合并 `main`、不自动推送远程。

### 2. 新增与修改

- 新增 `src/protocol/message_codec.h/.cpp`：SGS/1 JSON、长度帧和增量接收缓冲。
- 新增 `src/network/game_client.h/.cpp`：异步 Socket、握手、请求匹配、发送队列和错误处理。
- 新增 `tests/network_test.cpp`：协议单元测试及本地 `QTcpServer` 集成替身。
- 新增 `docs/protocol.md`：同步自 SgsServer `feature/m1-network-server` 提交 `dfc957a`。
- 修改 `Application`：拥有 `GameClient` 并在 QML 引擎加载前暴露上下文属性。
- 修改 `Main.qml`：网络地址、端口、连接状态、Ping、中文 Echo 和有限日志界面。
- 修改 CMake、README 和架构文档，加入 Qt Network 与 M1-C 说明。

### 3. 生命周期和执行链

`Application` 按“GameClient 先构造、QML 引擎后构造”的顺序持有二者，逆序销毁时 QML 不会引用已销毁网络对象。`GameClient` 以值成员拥有 `QTcpSocket` 和单次 `QTimer`，不创建额外线程。

启动链：QML 调用 `connectToServer` → `Connecting` → TCP `connected` → `Handshaking` → 自动发送 Hello #1 并启动 10 秒定时器 → 收到合法且 ID 匹配的 HelloAck → `Ready`。连接失败、主动断开、远端关闭或致命协议错误都会清理接收缓存、待处理表和发送队列并回到 `Disconnected`。

显式析构先停止定时器、断开 Socket 信号并 `abort()`，防止 `QTcpSocket` 在其他成员析构期间发出回调。该问题由连续网络测试实际发现并修复。

### 4. 协议和缓冲

`MessageCodec` 用 `QByteArray` 累积任意分段输入：少于 4 字节时等待；取得大端长度后先验证 1～65536，再等待完整 body；完整后严格验证无 BOM、有效 UTF-8、JSON 对象及四个公共字段，并继续循环解析同批粘连帧。任何错误都在分配大块内存前被拒绝。

请求 ID 在 1～2147483647 内循环分配，并跳过待处理 ID，不发生有符号溢出。待处理表记录 Hello/Ping/Echo 的预期响应和 Echo 原文；未知或不匹配 ID 不完成请求。`UNSUPPORTED_PROTOCOL` 会立即终止会话，其他合法 Error 显示到日志/UI，并依据服务端后续连接状态更新。

发送端使用 FIFO 完整帧队列和帧内偏移；`QTcpSocket::write` 部分接受时保留剩余字节，`bytesWritten` 后继续提交，负返回值会报告错误并断开。

### 5. 环境与构建（已实际测试）

| 项目 | 结果 |
| --- | --- |
| Qt | 6.11.2：Core、Network、Qml、Quick、Quick Controls 2、Test |
| 编译器 | MinGW-w64 G++ 13.1.0，C++20 |
| CMake / Ninja | 3.30.5 / 1.12.1 |
| Debug | 配置与完整编译成功 |
| Release | 独立目录配置与完整编译成功 |

Qt 的可选 Vulkan headers 未安装，但本项目不使用 Vulkan，不影响编译和运行。

### 6. CTest（已实际测试）

| 构建 | 结果 | 最终用时 |
| --- | --- | --- |
| Debug | 2/2 通过，0 失败 | 5.08 秒（最终回归） |
| Release | 2/2 通过，0 失败 | 5.27 秒（最终回归） |

`sgs-client-smoke` 保持 M0 的离屏 QML 加载检查。`sgs-client-network` 使用本机 `127.0.0.1` 临时端口和 `QTcpServer` 测试替身，覆盖大端编码、严格公共字段、中文 UTF-8、1～3 字节头/分段 body、连续多帧、非法长度、BOM、无效 UTF-8/JSON、超长编码、HelloAck Ready、错误响应 ID、Ping/Pong、EchoResult 原文、异常断线、连接失败后重试、握手超时、普通 Error 展示和 `UNSUPPORTED_PROTOCOL` 终止会话。测试均有 30 秒 CTest 总超时，内部等待不超过 5 秒。

沙箱内首次运行 loopback 被系统策略以 `Permission denied` 拦截；在受控沙箱外仅连接 `127.0.0.1` 重跑后全部通过。这一环境限制不计为产品失败。

### 7. GUI（已实际测试）

Release 程序经仅用于验证的 `windeployqt` 部署到已忽略的 `build-release/bin` 后，以真实 Windows 桌面窗口启动。窗口标题为 `SGS Client`，截图和 UI Automation 树同时确认：

- 状态为“未连接”；地址/端口为 `127.0.0.1:18765`。
- “连接”可用；“断开”和“Ping”禁用。
- Echo 输入与“发送 Echo”禁用，中文默认文本显示正确。
- 版本 `0.1.0-dev` 与通信日志区域显示正常。
- 使用窗口关闭按钮后目标窗口消失，无残留窗口。

### 8. 测试边界与已知限制

- 本阶段没有对真实 Linux SgsServer 做跨设备联调；所有网络自动化结果均来自本地 `QTcpServer` 替身，不能伪装成真实服务端测试。
- 服务端默认监听 `127.0.0.1:18765`；跨设备联调前需由服务端显式配置监听地址并限制暴露范围。
- 没有 TLS、认证、登录、大厅、房间、游戏状态、资源同步、Lua、自动重连或额外线程池。
- `windeployqt` 产物只在忽略的构建目录用于 GUI 验证，不提交仓库，也不代表已制作安装包。

## M1-I-F1：Qt TCP 代理修复与真实联调

完成日期：2026-10-24（Asia/Shanghai）

### 修复

首次 Windows → Linux 联调时，系统代理配置导致 `QTcpSocket` 报告“对于这个操作代理类型是无效的”。`GameClient::connectToServer()` 现在会在每次 `connectToHost()` 前，仅对游戏服务器 Socket 设置 `QNetworkProxy::NoProxy`。该设置不修改应用级或 Windows 系统代理，也不会限制未来资源下载模块独立选择代理。

`sgs-client-network` 新增代理回归用例：先把应用代理指向不可用的本机 HTTP 代理端口，再确认游戏 Socket 仍能直连本地测试服务器并完成 Hello/HelloAck。Debug、Release 均重新编译成功，两套 CTest 均为 2/2 通过。

### 真实 Windows → Linux 联调（已实际测试）

- Windows：`10.0.233.61`，接口“以太网 3”；Linux 服务端：`10.0.172.236:18765`。
- `Test-NetConnection` 返回 `TcpTestSucceeded: True`。
- Release GUI 实际完成 `TCP 已连接 → Hello #1 → HelloAck → Ready`，服务端报告 `SgsServer 0.1.0-dev`。
- `Ping #2 → Pong #2`；指定中文文本经 `Echo #3 → EchoResult #3` 原文返回。
- 连续执行 `Ping #4 → Pong #4`、混合中英文/空格/标点/特殊字符的 `Echo #5 → EchoResult #5`，ID 和原文均匹配，无重复响应。
- 主动断开后状态回到“未连接”，Ping/Echo 控件禁用；再次连接重新发送 Hello #1，并再次进入 Ready。

本次没有修改 SGS/1 协议或 Linux 服务端，没有测试需要服务端配合的异常主动断开，也没有进入 M2。

---

## M0 历史记录

完成日期：2026-10-24（Asia/Shanghai）

## 1. 实际项目路径

`D:\Game\NewKill`

参考文件 `FreeKill-0.5.25.zip` 仅用于了解 Qt/QML 分层，已加入 `.gitignore`；本客户端没有复制其复杂实现，也没有构建或运行依赖。

## 2. 开发环境（已实际检查）

| 项目 | 实测结果 |
| --- | --- |
| Windows | 注册表报告 Windows 10 Home 26H2，OS Build `26300.9711`，64 位 |
| Qt | Qt `6.11.2`，使用 `C:\Qt\6.11.2\mingw_64` |
| Qt 模块 | Quick、QML、Quick Controls 2、Test 的 CMake 包均已找到并参与构建 |
| 编译器 | Qt 自带 MinGW-w64 GCC/G++ `13.1.0`，`C:\Qt\Tools\mingw1310_64` |
| CMake | `3.30.5`，`C:\Qt\Tools\CMake_64` |
| Ninja | `1.12.1`，`C:\Qt\Tools\Ninja` |
| Git | `2.53.0.windows.3` |

工具链匹配结论：本次使用 MinGW 版 Qt 与 Qt 自带 MinGW 13.1，不与已安装的 MSVC 套件混用。机器同时存在 `C:\Qt\6.11.2\msvc2022_64`，但本次未使用。

环境注意：系统默认 `PATH` 中的 `qmake` 来自 Anaconda Qt 5.15.2，且默认找不到 `cmake`/`g++`。因此实际构建显式选择了上述 Qt 6 MinGW Kit。项目的 `CMakeLists.txt` 未写死 Qt 安装路径。

## 3. 最终目录结构

```text
NewKill/
├── .gitignore
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── architecture.md
│   └── milestone_status.md
├── qml/
│   └── Main.qml
├── src/
│   ├── application.cpp
│   ├── application.h
│   └── main.cpp
└── tests/
    └── smoke_test.cpp
```

`build/` 和 `build-release/` 是本地验证产物并已忽略，不属于交付源码。参考 ZIP 同样已忽略。

## 4. 新增或修改文件

- `CMakeLists.txt`：C++20、Qt 模块、QML Module、警告级别、测试和 Windows 产物配置。
- `src/main.cpp`：创建 `QGuiApplication`、初始化 `Application`、进入事件循环。
- `src/application.h/.cpp`：应用元数据、QML 引擎生命周期、主界面加载和失败处理。
- `qml/Main.qml`：最小桌面主窗口、版本显示及“功能开发中”入口。
- `tests/smoke_test.cpp`：使用子进程和离屏平台加载真实客户端/QML 后正常退出。
- `README.md`：依赖、构建、运行、测试和范围说明。
- `docs/architecture.md`：当前分层和未来模块职责边界。
- `.gitignore`：Windows、Qt、CMake、IDE 和参考归档忽略项。

## 5. Application 启动执行链

`main()` 创建 `QGuiApplication` → 创建借用它的 `Application` → `Application::initialize()` 设置名称/版本并加载 QML → 初始化成功后进入 `QGuiApplication::exec()`。

局部变量的逆序析构保证 `Application`（含 `QQmlApplicationEngine`）先销毁，`QGuiApplication` 后销毁。QML 根对象创建失败时，初始化返回 `false`；异步 `objectCreationFailed` 也会请求以失败码退出。

## 6. QML 窗口加载机制

`qt_add_qml_module` 将 `qml/Main.qml` 作为 URI `SgsClient` 的 QML 模块纳入构建和资源缓存。C++ 使用 `QQmlApplicationEngine::loadFromModule("SgsClient", "Main")` 加载，无需依赖源目录下的松散 QML 文件。版本文本读取 `Qt.application.version`，其值由 C++ 设置为 `0.1.0-dev`。

## 7. CMake 配置及编译结果（已实际测试）

- Debug：干净目录配置成功；C++20 编译、QML 缓存生成和 `build\bin\sgs-client.exe` 链接成功。
- Release：独立的 `build-release` 目录配置成功；完整编译和链接成功。
- 编译启用了 GCC 的 `-Wall -Wextra -Wpedantic`（MSVC 分支配置为 `/W4 /permissive-`）。
- Qt 报告未找到可选的 Vulkan headers；本项目未使用 Vulkan，该提示不影响配置、编译或运行。

## 8. CTest 结果（已实际测试）

| 构建类型 | 结果 | 用时 |
| --- | --- | --- |
| Debug | 1/1 通过，0 失败 | 干净构建后 0.86 秒；最终增量复核 0.21 秒 |
| Release | 1/1 通过，0 失败 | 干净构建后 0.47 秒；最终增量复核 0.17 秒 |

冒烟测试以 `QT_QPA_PLATFORM=offscreen` 启动最终客户端程序，在实际创建 QML 根窗口后由专用 `--smoke-test` 参数退出，并检查正常退出码及关键 QML 模块加载错误。该结果只证明自动化加载，不冒充可见窗口验收。

## 9. 实际 GUI 运行结果（已实际测试）

Debug 客户端以正常桌面平台启动。进程保持运行，检测到非零顶层窗口句柄 `25298420`，窗口标题为 `SGS Client`。随后通过正常的窗口关闭请求退出，`CloseMainWindow()` 返回成功，进程在 5 秒内结束且退出码为 `0`。

因此“窗口可以实际显示并正常关闭”已与离屏 CTest 分开验证。主界面内容、版本文本和按钮占位行为同时由已编译的 `Main.qml` 定义；本阶段没有任何网络连接假象。

## 10. Git 状态

当前目录原先不是 Git 仓库。M0 完成时已初始化本地仓库并使用 `main` 分支；没有配置或推送远程。源码文件处于待首次提交状态，构建目录、二进制、Qt Creator 用户配置和参考 ZIP 均被忽略。

## 11. 已知问题与限制

- Qt/CMake/MinGW 未统一加入系统 `PATH`，命令行构建需使用完整路径、临时调整 `PATH`，或在 Qt Creator 中选择匹配 Kit。
- 尚未运行 `windeployqt`，所以可执行文件依赖开发机上的 Qt/MinGW 运行库；M0 不要求安装包。
- 自动化测试验证 QML 创建和退出，不做像素级界面断言；已另行完成可见窗口验收。
- 网络、协议、账号、大厅、房间、牌桌、规则、Lua 和正式资源系统均未实现，符合 M0 范围。

## 12. M1 建议（仅建议，未实现）

等待架构负责人统一跨端协议后，再确定 Network 的连接状态机、消息边界、错误模型和可测试接口。建议优先建立不暴露传输细节给 QML 的 C++ 网络抽象与协议契约测试，再由只读 Model 向 QML 提供连接状态；不要在协议确定前创建大厅、房间或游戏对象。
