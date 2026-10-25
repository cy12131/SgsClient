#include "models/room_model.h"

#include "network/game_client.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QHash>
#include <QSet>

#include <climits>
#include <cmath>

namespace {

bool integerInRange(const QJsonValue &value, const qint64 minimum, const qint64 maximum,
                    qint64 *result)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number
        || number < static_cast<double>(minimum) || number > static_cast<double>(maximum)) {
        return false;
    }
    *result = static_cast<qint64>(number);
    return true;
}

QString friendlyError(const QString &code, const QString &fallback)
{
    static const QHash<QString, QString> messages{
        {QStringLiteral("NOT_IN_ROOM"), QStringLiteral("当前不在房间中。")},
        {QStringLiteral("PERMISSION_DENIED"), QStringLiteral("当前身份不能执行此操作。")},
        {QStringLiteral("INVALID_ROOM_DATA"), QStringLiteral("房间操作参数无效。")}};
    return messages.value(code, QStringLiteral("%1：%2").arg(code, fallback));
}

} // namespace

RoomModel::RoomModel(GameClient &client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    connect(&client, &GameClient::responseReceived, this, &RoomModel::handleResponse);
    connect(&client, &GameClient::notificationReceived, this, &RoomModel::handleNotification);
    connect(&client, &GameClient::requestFailed, this, &RoomModel::handleRequestError);
    connect(&client, &GameClient::stateChanged, this, [this] {
        if (m_client.state() == GameClient::Disconnected) {
            clear();
        }
    });
}

void RoomModel::setCurrentUserId(const qint64 userId)
{
    if (m_currentUserId != userId) {
        m_currentUserId = userId;
        emit roomChanged();
    }
}

void RoomModel::prepareForRoom(const qint64 roomId)
{
    if (roomId <= 0) {
        return;
    }
    m_expectedRoomId = roomId;
    setError({});
}

void RoomModel::clear()
{
    const bool wasActive = m_active;
    m_expectedRoomId = 0;
    m_roomId = 0;
    m_revision = 0;
    m_ownerId = 0;
    m_leaveRequestId = -1;
    m_readyRequestId = -1;
    m_name.clear();
    m_status.clear();
    m_players.clear();
    m_maxPlayers = 0;
    m_active = false;
    m_currentUserOwner = false;
    m_currentUserReady = false;
    setBusy(false);
    if (wasActive) {
        emit activeChanged();
    }
    emit roomChanged();
    emit playersChanged();
}

QVariantMap RoomModel::playerAtSeat(const int seat) const
{
    for (const QVariant &player : m_players) {
        const QVariantMap map = player.toMap();
        if (map.value(QStringLiteral("seat")).toInt() == seat) {
            return map;
        }
    }
    return {};
}

void RoomModel::setReady(const bool ready)
{
    if (!m_active || m_currentUserOwner || m_busy) {
        return;
    }
    const qint64 id = m_client.sendRequest(QStringLiteral("SetReady"),
                                           QStringLiteral("SetReadyResult"),
                                           {{QStringLiteral("ready"), ready}});
    if (id > 0) {
        m_readyRequestId = id;
        setBusy(true);
        setError({});
    }
}

void RoomModel::leaveRoom()
{
    if (!m_active || m_busy) {
        return;
    }
    const qint64 id = m_client.sendRequest(QStringLiteral("LeaveRoom"),
                                           QStringLiteral("LeaveRoomResult"));
    if (id > 0) {
        m_leaveRequestId = id;
        setBusy(true);
        setError({});
    }
}

void RoomModel::handleResponse(const QString &type, const qint64 requestId,
                               const QJsonObject &data)
{
    if (type == QStringLiteral("SetReadyResult") && requestId == m_readyRequestId) {
        m_readyRequestId = -1;
        setBusy(false);
        if (!data.value(QStringLiteral("ready")).isBool()) {
            setError(QStringLiteral("服务端返回了无效的准备结果。"));
        }
        return;
    }
    if (type == QStringLiteral("LeaveRoomResult") && requestId == m_leaveRequestId) {
        m_leaveRequestId = -1;
        if (!data.isEmpty()) {
            setError(QStringLiteral("服务端返回了无效的退出房间结果。"));
            setBusy(false);
            return;
        }
        clear();
        emit leftRoom();
    }
}

void RoomModel::handleNotification(const QString &type, const QJsonObject &data)
{
    if (type == QStringLiteral("RoomSnapshot")) {
        applySnapshot(data);
    }
}

void RoomModel::handleRequestError(const qint64 requestId, const QString &code,
                                   const QString &message)
{
    if (requestId != m_leaveRequestId && requestId != m_readyRequestId) {
        return;
    }
    if (requestId == m_leaveRequestId) {
        m_leaveRequestId = -1;
    }
    if (requestId == m_readyRequestId) {
        m_readyRequestId = -1;
    }
    setBusy(false);
    setError(friendlyError(code, message));
}

void RoomModel::applySnapshot(const QJsonObject &data)
{
    qint64 roomId = 0;
    qint64 revision = 0;
    qint64 maxPlayers = 0;
    qint64 ownerId = 0;
    if (!integerInRange(data.value(QStringLiteral("room_id")), 1, INT_MAX, &roomId)
        || !integerInRange(data.value(QStringLiteral("revision")), 1, INT_MAX, &revision)
        || !integerInRange(data.value(QStringLiteral("max_players")), 2, 8, &maxPlayers)
        || !integerInRange(data.value(QStringLiteral("owner_id")), 1, INT_MAX, &ownerId)
        || !data.value(QStringLiteral("name")).isString()
        || data.value(QStringLiteral("status")).toString() != QStringLiteral("waiting")
        || !data.value(QStringLiteral("players")).isArray()) {
        setError(QStringLiteral("收到字段无效的房间快照。"));
        return;
    }
    if ((m_active && roomId != m_roomId)
        || (!m_active && (m_expectedRoomId <= 0 || roomId != m_expectedRoomId))) {
        return;
    }
    if (m_active && revision <= m_revision) {
        return;
    }

    const QJsonArray playerArray = data.value(QStringLiteral("players")).toArray();
    if (playerArray.isEmpty() || playerArray.size() > maxPlayers) {
        setError(QStringLiteral("房间快照的玩家数量无效。"));
        return;
    }
    QVariantList players;
    QSet<qint64> userIds;
    QSet<int> seats;
    int ownerCount = 0;
    bool currentOwner = false;
    bool currentReady = false;
    for (const QJsonValue &value : playerArray) {
        if (!value.isObject()) {
            setError(QStringLiteral("房间快照包含无效玩家。"));
            return;
        }
        const QJsonObject player = value.toObject();
        qint64 userId = 0;
        qint64 seat = 0;
        if (!integerInRange(player.value(QStringLiteral("user_id")), 1, INT_MAX, &userId)
            || !integerInRange(player.value(QStringLiteral("seat")), 1, maxPlayers, &seat)
            || !player.value(QStringLiteral("nickname")).isString()
            || !player.value(QStringLiteral("ready")).isBool()
            || !player.value(QStringLiteral("is_owner")).isBool()
            || userIds.contains(userId) || seats.contains(static_cast<int>(seat))) {
            setError(QStringLiteral("房间快照包含字段或座位无效的玩家。"));
            return;
        }
        userIds.insert(userId);
        seats.insert(static_cast<int>(seat));
        const bool isOwner = player.value(QStringLiteral("is_owner")).toBool();
        const bool ready = player.value(QStringLiteral("ready")).toBool();
        if (isOwner) {
            ++ownerCount;
            if (userId != ownerId || ready) {
                setError(QStringLiteral("房间快照的房主状态无效。"));
                return;
            }
        }
        if (userId == m_currentUserId) {
            currentOwner = isOwner;
            currentReady = ready;
        }
        players.append(QVariantMap{{QStringLiteral("user_id"), userId},
                                   {QStringLiteral("nickname"),
                                    player.value(QStringLiteral("nickname")).toString()},
                                   {QStringLiteral("seat"), static_cast<int>(seat)},
                                   {QStringLiteral("ready"), ready},
                                   {QStringLiteral("is_owner"), isOwner}});
    }
    if (ownerCount != 1 || !userIds.contains(ownerId) || !userIds.contains(m_currentUserId)) {
        setError(QStringLiteral("房间快照缺少房主或当前用户。"));
        return;
    }

    const bool wasActive = m_active;
    m_active = true;
    m_expectedRoomId = roomId;
    m_roomId = roomId;
    m_revision = revision;
    m_name = data.value(QStringLiteral("name")).toString();
    m_maxPlayers = static_cast<int>(maxPlayers);
    m_status = QStringLiteral("waiting");
    m_ownerId = ownerId;
    m_players = players;
    m_currentUserOwner = currentOwner;
    m_currentUserReady = currentReady;
    setError({});
    if (!wasActive) {
        emit activeChanged();
    }
    emit roomChanged();
    emit playersChanged();
}

void RoomModel::setBusy(const bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void RoomModel::setError(const QString &message)
{
    if (m_errorText != message) {
        m_errorText = message;
        emit errorTextChanged();
    }
}
