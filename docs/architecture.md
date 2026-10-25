# SgsClient 客户端架构（M3-A-C）

## 总体结构

客户端采用 C++/QML 分层。C++ 提供进程入口、应用生命周期、SGS/1、异步 TCP、资源包事务以及大厅/房间可观察模型；QML 只负责状态展示和操作转发。客户端不实现服务端权威规则，也不执行资源包中的 Lua/QML。

```text
main.cpp
  -> QGuiApplication
  -> Application
       -> GameClient
            -> QTcpSocket / QTimer
            -> MessageCodec
       -> PackageSyncManager -> GameClient
            -> QtConcurrent worker
                 -> PackageInstaller -> Git / Manifest / active.json
       -> RoomModel
       -> LobbyModel -> GameClient / PackageSyncManager / RoomModel
       -> QQmlApplicationEngine
            -> SgsClient/Shell.qml
                 -> Lobby.qml
                 -> WaitingRoom.qml
                 -> PackageSyncPanel.qml
```

## C++ 与 QML 的职责边界

C++ 负责应用初始化、错误处理、Socket 生命周期、协议解析、请求匹配、后台 Git 同步、Manifest/依赖验证及业务状态投影。QML 负责窗口、同步进度、错误/重试、固定座位布局和用户操作，不直接运行 Git、解析协议或推导权威状态。

## Application 生命周期

`main.cpp` 首先创建 `QGuiApplication`，随后创建 `Application`。后者依次拥有 `GameClient`、`PackageSyncManager`、`RoomModel`、`LobbyModel` 和 `QQmlApplicationEngine`，并把四个 C++ 对象暴露给 QML。逆序析构先释放界面和模型，再由同步管理器设置取消标记、终止后台 Git 子进程并等待 worker 收尾，最后销毁网络对象。

## 资源包同步与大厅门禁

每条新 TCP Session 都从 NotReady 开始。HelloAck 后 `PackageSyncManager` 请求清单，严格验证响应字段和 RFC 8785/JCS `manifest_id`；随后在 QtConcurrent 工作线程检查或安装全部包。只有原子切换成功并收到匹配的 `PackageReadyResult.ready=true` 后，状态才进入 Ready，`LobbyModel` 才发送 EnterLobby。断线立即取消当前任务、清除 Session Ready，重连必须重新获取清单；空清单同样写入空激活集合并显式 PackageReady。

同步状态独立于 `GameClient::Ready`：后者只表示 Hello 通信就绪。`LobbyModel` 不持有仓库、路径或 Manifest 逻辑，只监听包同步 Ready 信号。Ping/Echo 在 Hello 后仍可使用。

## 安装目录与事务

默认根目录为 `QStandardPaths::AppLocalDataLocation/packages`：

```text
packages/
├── installed/<package-id>/<full-commit>/
├── .staging/<uuid>/
├── .empty-hooks/
├── active.json
└── active.previous.json
```

生产同步只接受不含凭据的 HTTPS URL。Git 禁用系统/全局配置、终端提示、外部协议和 Hook；仓库不会执行安装脚本或初始化 submodule。下载先进入同一文件系统的 staging，校验 HEAD 等于完整锁定 commit、原始 Manifest SHA-256、严格字段、包身份/版本/API、依赖图及声明路径后，才重命名为不可变 commit 目录。最终用 `QSaveFile` 原子替换 `active.json`；任何包失败都不改变旧激活集合。已存在目录若有 tracked/untracked 修改会明确失败，绝不覆盖用户文件。

依赖集合在安装前整体验证：ID 唯一、目标存在、比较器范围满足且无环。所有包完成前不会使用任何包，因此依赖不会被部分激活。ADR-0009 规定服务端资源包重启生效；M3 不做目录监视、热加载、热卸载或游戏中版本切换。

成员声明顺序保证逆序析构时 QML 引擎先离开，随后大厅、房间模型和网络对象依次销毁。`GameClient` 显式停止定时器、切断 Socket 回调并终止连接，避免 Socket 析构信号重入已销毁成员。最后 `Application` 先于 `QGuiApplication` 析构。

## MessageCodec 与接收缓冲

`MessageCodec` 不持有 Socket。它把每次 `readyRead` 的字节追加到 `QByteArray`，只有在至少取得 4 字节头、合法长度和完整消息体后才消费一帧，并循环解析同批多帧。长度为 0/超限、BOM、无效 UTF-8、无效 JSON 或公共字段错误均作为致命协议错误返回，`GameClient` 随即断开。

## GameClient 状态与请求

`GameClient` 以异步信号槽维护 `Disconnected → Connecting → Handshaking → Ready`。TCP `connected` 后自动以请求 1 发送 Hello，并启动 10 秒单次定时器；只有匹配请求 1 且业务字段合法的 HelloAck 才进入 Ready。

所有客户端请求从 1～2147483647 安全分配不重复编号，待处理表记录预期响应种类。服务端的 `RoomSnapshot` 与 `RoomListChanged` 通知使用 0 并绕过请求匹配；其他 ID 0 消息被拒绝。未知 ID 或类型不匹配的响应不会完成请求。发送端以 FIFO 帧队列和帧内偏移处理 `QTcpSocket::write` 的部分接受和错误；断线清空编解码缓存、待处理请求和发送队列。

## 大厅与房间模型

`LobbyModel` 在 HelloAck 且资源包同步获得服务器 Ready 确认后发送 `EnterLobby`，保存临时用户身份并自动请求 `ListRooms`。`RoomListChanged` 只触发新列表请求；模型只采用最新列表请求的响应，避免乱序覆盖。创建/加入成功后等待 `RoomSnapshot` 再进入房间页。

`RoomModel` 验证快照的房间、房主、玩家、唯一座位和当前用户不变量，以 revision 严格递增更新。座位直接按服务端 `seat` 映射，离开形成空位时不重排。准备操作只发送意图，显示值等待新快照；离开成功和任何断线都会清理旧房间。

未来 `GameTable` 只负责牌桌状态表现、动画和合法操作输入采集；M3-A-C 不加载资源包入口，也不实现 Lua/GameViewContext。

## 服务端边界

Linux 服务端是清单、房间状态、游戏规则和对局状态的权威来源。M3-A-C 采用 SgsArchitecture `422016ed12a6c982bdd9e72f75590ec4ca9b5614` 和服务端 `ee022ef9979295a15482c52b17df7c9a4de4b093`；客户端不得单方面扩展公共协议。

## 资源扩展方向

开发时检查了 FreeKill 0.5.25 中的大厅/等待房间 QML、背景和创建房间图标及其 GPLv3 声明，但本阶段没有复制其 QML 或素材。当前界面仅使用自制颜色、形状、文字和 Unicode 图标，因此公开仓库不新增授权不明确的第三方媒体。未来资源路径继续与业务模型分离，并在协议与产品命名确定后建立索引、加载和缺失回退机制。
