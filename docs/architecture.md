# SgsClient 客户端架构（M2-C）

## 总体结构

客户端采用 C++/QML 分层。C++ 提供进程入口、应用生命周期、SGS/1 编解码、异步 TCP 状态机以及大厅/房间可观察模型；QML 只负责连接页、大厅和等待房间的布局、展示和操作转发。客户端不实现服务端权威规则。

```text
main.cpp
  -> QGuiApplication
  -> Application
       -> GameClient
            -> QTcpSocket / QTimer
            -> MessageCodec
       -> RoomModel
       -> LobbyModel -> GameClient / RoomModel
       -> QQmlApplicationEngine
            -> SgsClient/Shell.qml
                 -> Lobby.qml
                 -> WaitingRoom.qml
```

## C++ 与 QML 的职责边界

C++ 负责应用初始化、错误处理、Socket 生命周期、协议解析、请求匹配，以及大厅和房间状态投影。QML 负责窗口、控件、固定座位布局和把用户操作转交给 `LobbyModel`/`RoomModel`，不直接解析 TCP 字节流或推导权威状态。

## Application 生命周期

`main.cpp` 首先创建 `QGuiApplication`，随后在其作用域内创建 `Application`。`Application` 借用 Qt 应用对象，依次拥有 `GameClient`、`RoomModel`、`LobbyModel` 和 `QQmlApplicationEngine`，把三个 C++ 对象作为上下文属性暴露给 QML，再通过 `loadFromModule("SgsClient", "Shell")` 加载模块。

成员声明顺序保证逆序析构时 QML 引擎先离开，随后大厅、房间模型和网络对象依次销毁。`GameClient` 显式停止定时器、切断 Socket 回调并终止连接，避免 Socket 析构信号重入已销毁成员。最后 `Application` 先于 `QGuiApplication` 析构。

## MessageCodec 与接收缓冲

`MessageCodec` 不持有 Socket。它把每次 `readyRead` 的字节追加到 `QByteArray`，只有在至少取得 4 字节头、合法长度和完整消息体后才消费一帧，并循环解析同批多帧。长度为 0/超限、BOM、无效 UTF-8、无效 JSON 或公共字段错误均作为致命协议错误返回，`GameClient` 随即断开。

## GameClient 状态与请求

`GameClient` 以异步信号槽维护 `Disconnected → Connecting → Handshaking → Ready`。TCP `connected` 后自动以请求 1 发送 Hello，并启动 10 秒单次定时器；只有匹配请求 1 且业务字段合法的 HelloAck 才进入 Ready。

所有客户端请求从 1～2147483647 安全分配不重复编号，待处理表记录预期响应种类。服务端的 `RoomSnapshot` 与 `RoomListChanged` 通知使用 0 并绕过请求匹配；其他 ID 0 消息被拒绝。未知 ID 或类型不匹配的响应不会完成请求。发送端以 FIFO 帧队列和帧内偏移处理 `QTcpSocket::write` 的部分接受和错误；断线清空编解码缓存、待处理请求和发送队列。

## 大厅与房间模型

`LobbyModel` 在 HelloAck 后发送 `EnterLobby`，保存临时用户身份并自动请求 `ListRooms`。`RoomListChanged` 只触发新列表请求；模型只采用最新列表请求的响应，避免乱序覆盖。创建/加入成功后等待 `RoomSnapshot` 再进入房间页。

`RoomModel` 验证快照的房间、房主、玩家、唯一座位和当前用户不变量，以 revision 严格递增更新。座位直接按服务端 `seat` 映射，离开形成空位时不重排。准备操作只发送意图，显示值等待新快照；离开成功和任何断线都会清理旧房间。

未来 `GameTable` 只负责牌桌状态表现、动画和合法操作输入采集；M2-C 不实现该模块。

## 服务端边界

Linux 服务端是房间状态、游戏规则和对局状态的权威来源。M2-C 严格采用服务端提交 `a95f424b82146c059d4d1d8ee7a3304a70d9c5f4` 发布的 SGS/1；客户端不得单方面扩展公共协议。

## 资源扩展方向

开发时检查了 FreeKill 0.5.25 中的大厅/等待房间 QML、背景和创建房间图标及其 GPLv3 声明，但本阶段没有复制其 QML 或素材。当前界面仅使用自制颜色、形状、文字和 Unicode 图标，因此公开仓库不新增授权不明确的第三方媒体。未来资源路径继续与业务模型分离，并在协议与产品命名确定后建立索引、加载和缺失回退机制。
