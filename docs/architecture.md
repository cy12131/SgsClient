# SgsClient 客户端架构（M1-C）

## 总体结构

客户端采用 C++/QML 分层。C++ 提供进程入口、应用生命周期、SGS/1 编解码和异步 TCP 状态机；QML 只负责网络调试界面的布局、展示和操作转发。M1-C 不引入游戏规则或资源管理实现。

```text
main.cpp
  -> QGuiApplication
  -> Application
       -> GameClient
            -> QTcpSocket / QTimer
            -> MessageCodec
       -> QQmlApplicationEngine
            -> SgsClient/Main.qml
```

## C++ 与 QML 的职责边界

C++ 负责应用初始化、错误处理、Socket 生命周期、协议解析、请求匹配和连接状态。QML 负责窗口、控件、布局、日志表现和把用户操作转交给 `GameClient`，不直接解析 TCP 字节流。游戏规则仍不得放入 QML。

## Application 生命周期

`main.cpp` 首先创建 `QGuiApplication`，随后在其作用域内创建 `Application`。`Application` 借用 Qt 应用对象，拥有 `GameClient` 和 `QQmlApplicationEngine`，把客户端作为上下文属性暴露给 QML，再通过 `loadFromModule("SgsClient", "Main")` 加载模块。

成员声明顺序保证 QML 引擎先于 `GameClient` 析构。`GameClient` 显式停止定时器、切断 Socket 回调并终止连接，避免 Socket 析构信号重入已销毁成员。最后 `Application` 先于 `QGuiApplication` 析构。

## MessageCodec 与接收缓冲

`MessageCodec` 不持有 Socket。它把每次 `readyRead` 的字节追加到 `QByteArray`，只有在至少取得 4 字节头、合法长度和完整消息体后才消费一帧，并循环解析同批多帧。长度为 0/超限、BOM、无效 UTF-8、无效 JSON 或公共字段错误均作为致命协议错误返回，`GameClient` 随即断开。

## GameClient 状态与请求

`GameClient` 以异步信号槽维护 `Disconnected → Connecting → Handshaking → Ready`。TCP `connected` 后自动以请求 1 发送 Hello，并启动 10 秒单次定时器；只有匹配请求 1 且业务字段合法的 HelloAck 才进入 Ready。

Ping/Echo 从 1～2147483647 安全分配不重复编号，待处理表记录预期响应种类和 Echo 原文。未知 ID 或类型不匹配的响应不会完成请求。发送端以 FIFO 帧队列和帧内偏移处理 `QTcpSocket::write` 的部分接受和错误；断线清空编解码缓存、待处理请求和发送队列。

## 后续模块的预期职责（仅规划）

- `Model`：向 QML 暴露客户端可观察状态，不承载服务端权威规则。
- `Lobby`：大厅列表、匹配或房间入口的表现和交互协调。
- `Room`：房间成员、准备状态及房间内操作的客户端视图。
- `GameTable`：牌桌状态表现、动画和合法操作输入的采集。

以上未来名称只表示职责边界，并非已经落地的类型或跨端消息名称。M1-C 不提前创建这些模块。

## 服务端边界

Linux 服务端是游戏规则和对局状态的权威来源。M1-C 严格采用服务端提交 `dfc957a` 发布的 SGS/1，仅实现 Hello、Ping、Echo 和 Error；客户端不得单方面扩展公共协议。

## 资源扩展方向

未来图片、音频和动画资源应在协议与产品命名确定后建立统一索引、加载和缺失回退机制。M1-C 只使用 Qt 控件、颜色和文字，不建立资源同步或复杂绑定系统。
