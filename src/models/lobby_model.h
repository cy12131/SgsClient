#pragma once

#include <QObject>
#include <QVariantList>

class GameClient;
class QJsonObject;
class RoomModel;

// LobbyModel 管理一次 TCP 会话内的临时身份与大厅房间列表。
// 身份在断线时立即清除；房间列表只接受最新 ListRoomsResult。
class LobbyModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(QString phaseText READ phaseText NOTIFY phaseChanged)
    Q_PROPERTY(bool inLobby READ inLobby NOTIFY phaseChanged)
    Q_PROPERTY(qint64 userId READ userId NOTIFY identityChanged)
    Q_PROPERTY(QString nickname READ nickname NOTIFY identityChanged)
    Q_PROPERTY(QVariantList rooms READ rooms NOTIFY roomsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    enum Phase { Offline, Connecting, Handshaking, CommunicationReady, EnteringLobby, Lobby, InRoom };
    Q_ENUM(Phase)

    explicit LobbyModel(GameClient &client, RoomModel &roomModel, QObject *parent = nullptr);

    [[nodiscard]] Phase phase() const { return m_phase; }
    [[nodiscard]] QString phaseText() const;
    [[nodiscard]] bool inLobby() const { return m_phase == Lobby || m_phase == InRoom; }
    [[nodiscard]] qint64 userId() const { return m_userId; }
    [[nodiscard]] QString nickname() const { return m_nickname; }
    [[nodiscard]] QVariantList rooms() const { return m_rooms; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString errorText() const { return m_errorText; }

    Q_INVOKABLE void connectAndEnter(const QString &host, int port, const QString &nickname);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE void refreshRooms();
    Q_INVOKABLE void createRoom(const QString &name, int maxPlayers);
    Q_INVOKABLE void joinRoom(qint64 roomId);

signals:
    void phaseChanged();
    void identityChanged();
    void roomsChanged();
    void busyChanged();
    void errorTextChanged();

private:
    void handleClientState();
    void handleResponse(const QString &type, qint64 requestId, const QJsonObject &data);
    void handleNotification(const QString &type, const QJsonObject &data);
    void handleRequestError(qint64 requestId, const QString &code, const QString &message);
    void enterLobby();
    void resetSession();
    void updatePhase();
    void setBusy(bool busy);
    void setError(const QString &message);

    GameClient &m_client;
    RoomModel &m_roomModel;
    Phase m_phase = Offline;
    qint64 m_userId = 0;
    qint64 m_enterRequestId = -1;
    qint64 m_listRequestId = -1;
    qint64 m_createRequestId = -1;
    qint64 m_joinRequestId = -1;
    QString m_pendingNickname;
    QString m_nickname;
    QString m_errorText;
    QVariantList m_rooms;
    bool m_busy = false;
};
