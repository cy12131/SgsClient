#pragma once

#include "protocol/message_codec.h"

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

class GameClient final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(bool ready READ isReady NOTIFY stateChanged)
    Q_PROPERTY(bool canConnect READ canConnect NOTIFY stateChanged)
    Q_PROPERTY(bool canDisconnect READ canDisconnect NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logTextChanged)

public:
    enum State { Disconnected, Connecting, Handshaking, Ready };
    Q_ENUM(State)

    explicit GameClient(QObject *parent = nullptr);
    ~GameClient() override;

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] bool isReady() const { return m_state == Ready; }
    [[nodiscard]] bool canConnect() const { return m_state == Disconnected; }
    [[nodiscard]] bool canDisconnect() const { return m_state != Disconnected; }
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] QString logText() const { return m_logLines.join(QLatin1Char('\n')); }

    Q_INVOKABLE void connectToServer(const QString &host, int port);
    Q_INVOKABLE void disconnectFromServer();
    Q_INVOKABLE qint64 sendPing();
    Q_INVOKABLE qint64 sendEcho(const QString &text);
    Q_INVOKABLE void clearLog();

    void setHandshakeTimeoutForTesting(int milliseconds);

signals:
    void stateChanged();
    void lastErrorChanged();
    void logTextChanged();
    void pongReceived(qint64 requestId);
    void echoReceived(qint64 requestId, const QString &text);

private:
    enum class PendingKind { Hello, Ping, Echo };
    struct PendingRequest {
        PendingKind kind;
        QString echoText;
    };

    void setState(State state);
    void setError(const QString &message);
    void appendLog(const QString &message);
    [[nodiscard]] qint32 nextRequestId();
    [[nodiscard]] bool enqueueMessage(const QJsonObject &message);
    void flushWriteQueue();
    void sendHello();
    void handleMessage(const QJsonObject &message);
    void handleErrorMessage(qint32 requestId, const QJsonObject &data);
    void resetSession();
    void failProtocol(const QString &message);

    QTcpSocket m_socket;
    QTimer m_handshakeTimer;
    Sgs::Protocol::MessageCodec m_codec;
    State m_state = Disconnected;
    qint32 m_nextRequestId = 1;
    QHash<qint32, PendingRequest> m_pending;
    QQueue<QByteArray> m_outgoingFrames;
    qsizetype m_writeOffset = 0;
    QString m_lastError;
    QStringList m_logLines;
};
