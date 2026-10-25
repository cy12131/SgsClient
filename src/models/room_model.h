#pragma once

#include <QObject>
#include <QVariantList>

class GameClient;
class QJsonObject;

// RoomModel 只保存服务端 RoomSnapshot 的权威副本。
// 按钮操作仅发送请求；在收到新 revision 快照前绝不预改 ready、房主或座位。
class RoomModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(qint64 roomId READ roomId NOTIFY roomChanged)
    Q_PROPERTY(qint64 revision READ revision NOTIFY roomChanged)
    Q_PROPERTY(QString name READ name NOTIFY roomChanged)
    Q_PROPERTY(int maxPlayers READ maxPlayers NOTIFY roomChanged)
    Q_PROPERTY(QString status READ status NOTIFY roomChanged)
    Q_PROPERTY(qint64 ownerId READ ownerId NOTIFY roomChanged)
    Q_PROPERTY(QVariantList players READ players NOTIFY playersChanged)
    Q_PROPERTY(bool currentUserOwner READ currentUserOwner NOTIFY roomChanged)
    Q_PROPERTY(bool currentUserReady READ currentUserReady NOTIFY roomChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    explicit RoomModel(GameClient &client, QObject *parent = nullptr);

    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] qint64 roomId() const { return m_roomId; }
    [[nodiscard]] qint64 revision() const { return m_revision; }
    [[nodiscard]] QString name() const { return m_name; }
    [[nodiscard]] int maxPlayers() const { return m_maxPlayers; }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] qint64 ownerId() const { return m_ownerId; }
    [[nodiscard]] QVariantList players() const { return m_players; }
    [[nodiscard]] bool currentUserOwner() const { return m_currentUserOwner; }
    [[nodiscard]] bool currentUserReady() const { return m_currentUserReady; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString errorText() const { return m_errorText; }

    void setCurrentUserId(qint64 userId);
    void prepareForRoom(qint64 roomId);
    void clear();

    Q_INVOKABLE QVariantMap playerAtSeat(int seat) const;
    Q_INVOKABLE void setReady(bool ready);
    Q_INVOKABLE void leaveRoom();

signals:
    void activeChanged();
    void roomChanged();
    void playersChanged();
    void busyChanged();
    void errorTextChanged();
    void leftRoom();

private:
    void handleResponse(const QString &type, qint64 requestId, const QJsonObject &data);
    void handleNotification(const QString &type, const QJsonObject &data);
    void handleRequestError(qint64 requestId, const QString &code, const QString &message);
    void applySnapshot(const QJsonObject &data);
    void setBusy(bool busy);
    void setError(const QString &message);

    GameClient &m_client;
    qint64 m_currentUserId = 0;
    qint64 m_expectedRoomId = 0;
    qint64 m_roomId = 0;
    qint64 m_revision = 0;
    qint64 m_ownerId = 0;
    qint64 m_leaveRequestId = -1;
    qint64 m_readyRequestId = -1;
    QString m_name;
    QString m_status;
    QString m_errorText;
    QVariantList m_players;
    int m_maxPlayers = 0;
    bool m_active = false;
    bool m_currentUserOwner = false;
    bool m_currentUserReady = false;
    bool m_busy = false;
};
