# SGS/1 TCP 通信协议

> 同步来源：SgsServer `feature/m2-lobby-room-server` 分支，提交 `a95f424b82146c059d4d1d8ee7a3304a70d9c5f4`。以下协议正文与该提交一致；客户端不得单方面修改公共协议。

## 1. 传输与帧格式

SGS/1 使用 TCP。每条消息格式为：

```text
+---------------------------+------------------------------+
| 4-byte unsigned big-endian| UTF-8 JSON body              |
| body length               | exactly body length bytes    |
+---------------------------+------------------------------+
```

长度只计算 JSON 消息体，不含 4 字节头。消息体允许 1～65536 字节，不得含 UTF-8 BOM。接收方必须处理 TCP 半包、粘包和连续帧，不能把一次 `recv` 等同于一条消息。长度为 0、超过上限、无效 UTF-8 或无法解析的 JSON 会导致服务端关闭连接并记录日志。

## 2. 公共 JSON 结构

```json
{
  "protocol": 1,
  "type": "Ping",
  "request_id": 2,
  "data": {}
}
```

四个字段均必填：`protocol` 是整数且当前只能为 1；`type` 是区分大小写的字符串；`request_id` 是 1～2147483647 的整数；`data` 是对象。缺少字段或字段类型、范围非法属于非法消息。

## 3. 连接状态

新连接处于 **WaitingHello**。客户端必须在 TCP 建立后 10 秒内发送合法 Hello，否则服务端关闭连接。握手成功后进入 **Ready**，才可发送 Ping 和 Echo。

WaitingHello 中先发送其他格式正确的请求会收到 `HANDSHAKE_REQUIRED`，连接保持可用。Ready 中再次发送 Hello 会收到 `INVALID_MESSAGE`。不支持的协议版本在 request_id 合法时收到 `UNSUPPORTED_PROTOCOL`，随后连接关闭。

## 4. Hello / HelloAck

请求：

```json
{"protocol":1,"type":"Hello","request_id":1,"data":{"client_name":"SgsClient","client_version":"0.1.0-dev"}}
```

`client_name` 和 `client_version` 必须是字符串，当前不要求客户端与服务端软件版本相同。

响应：

```json
{"protocol":1,"type":"HelloAck","request_id":1,"data":{"server_name":"SgsServer","server_version":"0.1.0-dev"}}
```

## 5. Ping / Pong

请求的 `data` 必须为空对象：

```json
{"protocol":1,"type":"Ping","request_id":2,"data":{}}
```

响应：

```json
{"protocol":1,"type":"Pong","request_id":2,"data":{}}
```

## 6. Echo / EchoResult

请求：

```json
{"protocol":1,"type":"Echo","request_id":3,"data":{"text":"你好，SgsServer！"}}
```

`text` 必须是字符串。响应保留相同 request_id 与 Unicode 文本：

```json
{"protocol":1,"type":"EchoResult","request_id":3,"data":{"text":"你好，SgsServer！"}}
```

## 7. Error

```json
{
  "protocol": 1,
  "type": "Error",
  "request_id": 4,
  "data": {
    "code": "UNKNOWN_MESSAGE_TYPE",
    "message": "Unsupported message type"
  }
}
```

| code | 条件 | 是否关闭 |
| --- | --- | --- |
| `UNSUPPORTED_PROTOCOL` | 合法 request_id 的消息使用非 1 协议版本 | 写完 Error 后关闭 |
| `HANDSHAKE_REQUIRED` | Hello 前发送其他格式正确的请求 | 否 |
| `INVALID_MESSAGE` | 重复 Hello 或已识别消息的业务字段不合法 | 否 |
| `UNKNOWN_MESSAGE_TYPE` | Ready 状态收到格式正确但未知的 type | 否 |

无法取得合法 request_id 的公共字段错误、无效 JSON/UTF-8 和非法帧长度可以直接关闭，不保证 Error 响应。任何协议错误都只影响对应 Session，不应导致服务端进程退出。

## 8. M1-S 限制

SGS/1 M1-S 没有 TLS、压缩、认证、登录、房间、游戏消息、重连恢复或复杂版本协商。服务默认只监听 `127.0.0.1:18765`；跨设备联调需显式修改监听地址，并自行限制网络暴露范围。

## 9. M2 临时用户与大厅

握手完成后，客户端以临时昵称进入大厅：

```json
{"protocol":1,"type":"EnterLobby","request_id":4,"data":{"nickname":"Alice"}}
```

成功响应包含本次连接的临时用户标识：

```json
{"protocol":1,"type":"EnterLobbyResult","request_id":4,"data":{"user_id":1001,"nickname":"Alice"}}
```

昵称去除首尾 Unicode 空白后必须为 1～20 个 Unicode 字符且不能包含控制字符。昵称允许重复；身份判断只使用服务端分配的正整数 user_id。同一连接不可重复进入大厅，断线后身份立即失效。

### 获取房间列表

`ListRooms` 请求的 `data` 为空对象。`ListRoomsResult.data.rooms` 是房间摘要数组，每项包含 `room_id`、`name`、`owner_nickname`、`player_count`、`max_players` 和 `status`。客户端收到 `RoomListChanged` 通知后应重新请求列表，不把通知本身当作房间数据。

## 10. 创建、加入与退出房间

`CreateRoom` 携带去除首尾空白后 1～40 个 Unicode 字符且不含控制字符的 `name`，以及 2～8 的 `max_players`；成功返回含正整数 `room_id` 的 `CreateRoomResult`。创建者自动成为房主、占用最小可用座位且 ready 为 false。`JoinRoom` 携带正整数 `room_id`，成功返回 `JoinRoomResult`，并由服务端分配 1～max_players 中最小可用座位。`LeaveRoom` 使用空 `data`，成功返回 `LeaveRoomResult`。房主离开时最早加入的剩余成员成为房主且 ready 变为 false；最后一人离开会删除房间。

## 11. 准备状态

`SetReady.data.ready` 是布尔值，成功返回带同一 `ready` 值的 `SetReadyResult`。客户端不得仅凭结果乐观修改显示，最终状态以 `RoomSnapshot` 为准。只有非房主成员可设置，房主 ready 永远为 false；重复设置相同值成功但 revision 不变。

## 12. RoomSnapshot 通知

```json
{"protocol":1,"type":"RoomSnapshot","request_id":0,"data":{"room_id":1,"revision":3,"name":"测试房间","max_players":4,"status":"waiting","owner_id":1001,"players":[{"user_id":1001,"nickname":"Alice","seat":1,"ready":false,"is_owner":true},{"user_id":1002,"nickname":"Bob","seat":3,"ready":true,"is_owner":false}]}}
```

`seat` 是服务端分配的 1～max_players 稳定座位号；空位不会导致其他玩家重排。成员或实际准备状态变化时 revision 单调增加；M2 只使用完整快照且 status 固定为 `waiting`。客户端只接受 revision 更大的同房间快照，忽略重复或过期快照。

## 13. RoomListChanged 通知

房间列表变化后，服务端只向已进入大厅且当前不在房间的连接发送空 data 的 `RoomListChanged`。通知不携带列表，客户端收到后自行请求 `ListRooms`。成功业务操作的同一 Session FIFO 顺序固定为先 Result，再 RoomSnapshot 或 RoomListChanged。

客户端发出的请求始终使用 1～2147483647。服务端主动通知 `RoomSnapshot` 和 `RoomListChanged` 使用 0；客户端发送 0 属于公共字段错误并会被关闭连接。

## 14. M2 错误码

除 M1 错误外，M2 可能返回 `LOBBY_REQUIRED`、`ALREADY_IN_LOBBY`、`INVALID_NICKNAME`、`INVALID_ROOM_DATA`、`ROOM_NOT_FOUND`、`ROOM_FULL`、`ALREADY_IN_ROOM`、`NOT_IN_ROOM`、`PERMISSION_DENIED` 和 `RESOURCE_LIMIT`。Error 保留原请求的 request_id；业务错误不改变有效状态，原则上不关闭连接。

## 15. M2 限制

M2 临时用户不持久化，没有密码、恢复令牌或跨连接身份恢复；房间不能开始游戏。尚无 Lua、卡牌/技能、数据库、资源包同步、TLS、恶意用户防护体系或复杂订阅系统。游戏规则和房间状态由服务端权威管理；客户端只提交操作意图并展示通知状态。
