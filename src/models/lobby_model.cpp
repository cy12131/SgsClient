#include "models/lobby_model.h"

#include "models/room_model.h"
#include "network/game_client.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>

#include <climits>
#include <cmath>

namespace {

bool positiveInteger(const QJsonValue &value, qint64 *result)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number || number < 1
        || number > static_cast<double>(INT_MAX)) {
        return false;
    }
    *result = static_cast<qint64>(number);
    return true;
}

bool containsControlCharacter(const QString &text)
{
    for (const QChar character : text) {
        if (character.category() == QChar::Other_Control) {
            return true;
        }
    }
    return false;
}

QString friendlyError(const QString &code, const QString &fallback)
{
    static const QHash<QString, QString> messages{
        {QStringLiteral("LOBBY_REQUIRED"), QStringLiteral("请先进入大厅。")},
        {QStringLiteral("ALREADY_IN_LOBBY"), QStringLiteral("当前连接已经进入大厅。")},
        {QStringLiteral("INVALID_NICKNAME"), QStringLiteral("昵称需为 1～20 个字符且不能含控制字符。")},
        {QStringLiteral("INVALID_ROOM_DATA"), QStringLiteral("房间名称或人数设置无效。")},
        {QStringLiteral("ROOM_NOT_FOUND"), QStringLiteral("房间不存在或已解散。")},
        {QStringLiteral("ROOM_FULL"), QStringLiteral("房间人数已满。")},
        {QStringLiteral("ALREADY_IN_ROOM"), QStringLiteral("你已经在一个房间中。")},
        {QStringLiteral("NOT_IN_ROOM"), QStringLiteral("当前不在房间中。")},
        {QStringLiteral("PERMISSION_DENIED"), QStringLiteral("当前身份不能执行此操作。")},
        {QStringLiteral("RESOURCE_LIMIT"), QStringLiteral("服务器房间数量已达到上限。")}};
    return messages.value(code, QStringLiteral("%1：%2").arg(code, fallback));
}

} // namespace

LobbyModel::LobbyModel(GameClient &client, RoomModel &roomModel, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_roomModel(roomModel)
{
    connect(&client, &GameClient::stateChanged, this, &LobbyModel::handleClientState);
    connect(&client, &GameClient::responseReceived, this, &LobbyModel::handleResponse);
    connect(&client, &GameClient::notificationReceived, this, &LobbyModel::handleNotification);
    connect(&client, &GameClient::requestFailed, this, &LobbyModel::handleRequestError);
    connect(&roomModel, &RoomModel::activeChanged, this, &LobbyModel::updatePhase);
    connect(&roomModel, &RoomModel::leftRoom, this, [this] {
        updatePhase();
        refreshRooms();
    });
}

QString LobbyModel::phaseText() const
{
    switch (m_phase) {
    case Offline: return QStringLiteral("未连接");
    case Connecting: return QStringLiteral("连接中");
    case Handshaking: return QStringLiteral("握手中");
    case CommunicationReady: return QStringLiteral("通信就绪");
    case EnteringLobby: return QStringLiteral("正在进入大厅");
    case Lobby: return QStringLiteral("大厅");
    case InRoom: return QStringLiteral("等待房间");
    }
    return QStringLiteral("未知");
}

void LobbyModel::connectAndEnter(const QString &host, const int port, const QString &nickname)
{
    const QString trimmed = nickname.trimmed();
    if (trimmed.toUcs4().size() < 1 || trimmed.toUcs4().size() > 20
        || containsControlCharacter(trimmed)) {
        setError(QStringLiteral("昵称需为 1～20 个字符且不能含控制字符。"));
        return;
    }
    if (!m_client.canConnect()) {
        setError(QStringLiteral("当前连接状态不能重新连接。"));
        return;
    }
    m_pendingNickname = trimmed;
    setError({});
    setBusy(true);
    m_client.connectToServer(host, port);
    updatePhase();
}

void LobbyModel::disconnectFromServer()
{
    m_client.disconnectFromServer();
}

void LobbyModel::refreshRooms()
{
    if (m_userId <= 0 || m_roomModel.active() || !m_client.isReady()) {
        return;
    }
    const qint64 id = m_client.sendRequest(QStringLiteral("ListRooms"),
                                           QStringLiteral("ListRoomsResult"));
    if (id > 0) {
        m_listRequestId = id;
    }
}

void LobbyModel::createRoom(const QString &name, const int maxPlayers)
{
    const QString trimmed = name.trimmed();
    if (m_phase != Lobby || m_busy || trimmed.toUcs4().isEmpty()
        || trimmed.toUcs4().size() > 40 || containsControlCharacter(trimmed)
        || maxPlayers < 2 || maxPlayers > 8) {
        setError(QStringLiteral("房间名称需为 1～40 个字符，人数需为 2～8。"));
        return;
    }
    const qint64 id = m_client.sendRequest(
        QStringLiteral("CreateRoom"), QStringLiteral("CreateRoomResult"),
        {{QStringLiteral("name"), trimmed}, {QStringLiteral("max_players"), maxPlayers}});
    if (id > 0) {
        m_createRequestId = id;
        setBusy(true);
        setError({});
    }
}

void LobbyModel::joinRoom(const qint64 roomId)
{
    if (m_phase != Lobby || m_busy || roomId <= 0) {
        return;
    }
    const qint64 id = m_client.sendRequest(QStringLiteral("JoinRoom"),
                                           QStringLiteral("JoinRoomResult"),
                                           {{QStringLiteral("room_id"), roomId}});
    if (id > 0) {
        m_joinRequestId = id;
        setBusy(true);
        setError({});
    }
}

void LobbyModel::handleClientState()
{
    if (m_client.state() == GameClient::Disconnected) {
        resetSession();
    } else if (m_client.state() == GameClient::Ready && m_userId == 0
               && m_enterRequestId < 0 && !m_pendingNickname.isEmpty()) {
        enterLobby();
    }
    updatePhase();
}

void LobbyModel::handleResponse(const QString &type, const qint64 requestId,
                                const QJsonObject &data)
{
    if (type == QStringLiteral("EnterLobbyResult") && requestId == m_enterRequestId) {
        qint64 userId = 0;
        if (!positiveInteger(data.value(QStringLiteral("user_id")), &userId)
            || !data.value(QStringLiteral("nickname")).isString()) {
            setError(QStringLiteral("服务端返回了无效的大厅身份。"));
            m_enterRequestId = -1;
            setBusy(false);
            updatePhase();
            return;
        }
        m_userId = userId;
        m_nickname = data.value(QStringLiteral("nickname")).toString();
        m_enterRequestId = -1;
        m_roomModel.setCurrentUserId(userId);
        setBusy(false);
        emit identityChanged();
        updatePhase();
        refreshRooms();
        return;
    }
    if (type == QStringLiteral("ListRoomsResult") && requestId == m_listRequestId) {
        m_listRequestId = -1;
        if (!data.value(QStringLiteral("rooms")).isArray()) {
            setError(QStringLiteral("服务端返回了无效的房间列表。"));
            return;
        }
        QVariantList rooms;
        for (const QJsonValue &value : data.value(QStringLiteral("rooms")).toArray()) {
            if (!value.isObject()) {
                setError(QStringLiteral("房间列表包含无效项目。"));
                return;
            }
            const QJsonObject room = value.toObject();
            qint64 roomId = 0;
            qint64 playerCount = 0;
            qint64 maxPlayers = 0;
            if (!positiveInteger(room.value(QStringLiteral("room_id")), &roomId)
                || !room.value(QStringLiteral("name")).isString()
                || !room.value(QStringLiteral("owner_nickname")).isString()
                || !positiveInteger(room.value(QStringLiteral("player_count")), &playerCount)
                || !positiveInteger(room.value(QStringLiteral("max_players")), &maxPlayers)
                || maxPlayers < 2 || maxPlayers > 8 || playerCount > maxPlayers
                || room.value(QStringLiteral("status")).toString() != QStringLiteral("waiting")) {
                setError(QStringLiteral("房间列表包含字段无效的项目。"));
                return;
            }
            rooms.append(QVariantMap{{QStringLiteral("room_id"), roomId},
                                     {QStringLiteral("name"), room.value(QStringLiteral("name")).toString()},
                                     {QStringLiteral("owner_nickname"), room.value(QStringLiteral("owner_nickname")).toString()},
                                     {QStringLiteral("player_count"), static_cast<int>(playerCount)},
                                     {QStringLiteral("max_players"), static_cast<int>(maxPlayers)},
                                     {QStringLiteral("status"), QStringLiteral("waiting")}});
        }
        m_rooms = rooms;
        emit roomsChanged();
        return;
    }

    const bool createResult = type == QStringLiteral("CreateRoomResult")
        && requestId == m_createRequestId;
    const bool joinResult = type == QStringLiteral("JoinRoomResult")
        && requestId == m_joinRequestId;
    if (createResult || joinResult) {
        qint64 roomId = 0;
        if (!positiveInteger(data.value(QStringLiteral("room_id")), &roomId)) {
            setError(QStringLiteral("服务端返回了无效的房间编号。"));
        } else {
            m_roomModel.prepareForRoom(roomId);
        }
        if (createResult) {
            m_createRequestId = -1;
        }
        if (joinResult) {
            m_joinRequestId = -1;
        }
        setBusy(false);
    }
}

void LobbyModel::handleNotification(const QString &type, const QJsonObject &data)
{
    if (type == QStringLiteral("RoomListChanged") && data.isEmpty()
        && m_userId > 0 && !m_roomModel.active()) {
        refreshRooms();
    }
}

void LobbyModel::handleRequestError(const qint64 requestId, const QString &code,
                                    const QString &message)
{
    if (requestId != m_enterRequestId && requestId != m_listRequestId
        && requestId != m_createRequestId && requestId != m_joinRequestId) {
        return;
    }
    if (requestId == m_enterRequestId) m_enterRequestId = -1;
    if (requestId == m_listRequestId) m_listRequestId = -1;
    if (requestId == m_createRequestId) m_createRequestId = -1;
    if (requestId == m_joinRequestId) m_joinRequestId = -1;
    setBusy(false);
    setError(friendlyError(code, message));
    updatePhase();
}

void LobbyModel::enterLobby()
{
    const qint64 id = m_client.sendRequest(
        QStringLiteral("EnterLobby"), QStringLiteral("EnterLobbyResult"),
        {{QStringLiteral("nickname"), m_pendingNickname}});
    if (id > 0) {
        m_enterRequestId = id;
        setBusy(true);
        updatePhase();
    }
}

void LobbyModel::resetSession()
{
    m_userId = 0;
    m_enterRequestId = -1;
    m_listRequestId = -1;
    m_createRequestId = -1;
    m_joinRequestId = -1;
    m_nickname.clear();
    m_rooms.clear();
    m_roomModel.setCurrentUserId(0);
    m_roomModel.clear();
    setBusy(false);
    emit identityChanged();
    emit roomsChanged();
}

void LobbyModel::updatePhase()
{
    Phase next = Offline;
    switch (m_client.state()) {
    case GameClient::Disconnected: next = Offline; break;
    case GameClient::Connecting: next = Connecting; break;
    case GameClient::Handshaking: next = Handshaking; break;
    case GameClient::Ready:
        if (m_roomModel.active()) next = InRoom;
        else if (m_userId > 0) next = Lobby;
        else if (m_enterRequestId > 0) next = EnteringLobby;
        else next = CommunicationReady;
        break;
    }
    if (m_phase != next) {
        m_phase = next;
        emit phaseChanged();
    }
}

void LobbyModel::setBusy(const bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void LobbyModel::setError(const QString &message)
{
    if (m_errorText != message) {
        m_errorText = message;
        emit errorTextChanged();
    }
}
