#include "network/game_client.h"

#include <QAbstractSocket>
#include <QCoreApplication>

namespace {

constexpr int kHandshakeTimeoutMilliseconds = 10'000;
constexpr qsizetype kMaximumLogLines = 200;

QJsonObject makeMessage(const QString &type, const qint32 requestId, const QJsonObject &data)
{
    return {{QStringLiteral("protocol"), 1},
            {QStringLiteral("type"), type},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("data"), data}};
}

} // namespace

GameClient::GameClient(QObject *parent)
    : QObject(parent)
{
    m_handshakeTimer.setSingleShot(true);
    m_handshakeTimer.setInterval(kHandshakeTimeoutMilliseconds);

    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        appendLog(QStringLiteral("TCP 已连接，开始 SGS/1 握手。"));
        setState(Handshaking);
        sendHello();
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this] {
        const auto result = m_codec.append(m_socket.readAll());
        for (const auto &message : result.messages) {
            handleMessage(message);
        }
        if (result.isError()) {
            failProtocol(result.error);
        }
    });
    connect(&m_socket, &QTcpSocket::bytesWritten, this,
            [this](qint64) { flushWriteQueue(); });
    connect(&m_socket, &QTcpSocket::stateChanged, this,
            [this](const QAbstractSocket::SocketState socketState) {
                // 首次连接失败时 Qt 可能只进入 UnconnectedState，而不发出 disconnected。
                // 必须主动复位，才能让用户在不自动重连的前提下手动重试。
                if (socketState == QAbstractSocket::UnconnectedState && m_state == Connecting) {
                    resetSession();
                    setState(Disconnected);
                    appendLog(QStringLiteral("TCP 连接未建立，可以手动重试。"));
                }
            });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        const bool wasActive = m_state != Disconnected;
        resetSession();
        setState(Disconnected);
        if (wasActive) {
            appendLog(QStringLiteral("连接已断开。"));
        }
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this,
            [this](const QAbstractSocket::SocketError error) {
                if (error == QAbstractSocket::RemoteHostClosedError) {
                    setError(QStringLiteral("服务器关闭了连接。"));
                } else {
                    setError(QStringLiteral("网络错误：%1").arg(m_socket.errorString()));
                }
            });
    connect(&m_handshakeTimer, &QTimer::timeout, this, [this] {
        if (m_state == Handshaking) {
            failProtocol(QStringLiteral("等待 HelloAck 超时。"));
        }
    });
}

GameClient::~GameClient()
{
    // QTcpSocket 是值成员，销毁时可能发出断线相关信号。此时其他值成员也正按
    // 逆序析构，因此必须先切断回调，避免槽访问已经销毁的缓冲区或请求表。
    m_handshakeTimer.stop();
    m_socket.disconnect(this);
    m_socket.abort();
}

QString GameClient::stateText() const
{
    switch (m_state) {
    case Disconnected: return QStringLiteral("未连接");
    case Connecting: return QStringLiteral("连接中");
    case Handshaking: return QStringLiteral("握手中");
    case Ready: return QStringLiteral("已就绪");
    }
    return QStringLiteral("未知");
}

void GameClient::connectToServer(const QString &host, const int port)
{
    if (!canConnect()) {
        appendLog(QStringLiteral("当前状态不允许重复连接。"));
        return;
    }
    if (host.trimmed().isEmpty() || port < 1 || port > 65'535) {
        setError(QStringLiteral("请输入有效的服务器地址和端口。"));
        return;
    }
    resetSession();
    if (!m_lastError.isEmpty()) {
        m_lastError.clear();
        emit lastErrorChanged();
    }
    setState(Connecting);
    appendLog(QStringLiteral("正在连接 %1:%2 …").arg(host.trimmed()).arg(port));
    m_socket.connectToHost(host.trimmed(), static_cast<quint16>(port));
}

void GameClient::disconnectFromServer()
{
    if (m_state == Disconnected) {
        return;
    }
    appendLog(QStringLiteral("正在主动断开连接。"));
    m_socket.disconnectFromHost();
    if (m_socket.state() == QAbstractSocket::UnconnectedState) {
        resetSession();
        setState(Disconnected);
    }
}

qint64 GameClient::sendPing()
{
    if (!isReady()) {
        setError(QStringLiteral("握手完成前不能发送 Ping。"));
        return -1;
    }
    const qint32 requestId = nextRequestId();
    if (requestId < 0) {
        setError(QStringLiteral("没有可用的请求编号。"));
        return -1;
    }
    m_pending.insert(requestId, {PendingKind::Ping, {}});
    if (!enqueueMessage(makeMessage(QStringLiteral("Ping"), requestId, {}))) {
        m_pending.remove(requestId);
        return -1;
    }
    appendLog(QStringLiteral("已发送 Ping #%1。").arg(requestId));
    return requestId;
}

qint64 GameClient::sendEcho(const QString &text)
{
    if (!isReady()) {
        setError(QStringLiteral("握手完成前不能发送 Echo。"));
        return -1;
    }
    const qint32 requestId = nextRequestId();
    if (requestId < 0) {
        setError(QStringLiteral("没有可用的请求编号。"));
        return -1;
    }
    m_pending.insert(requestId, {PendingKind::Echo, text});
    if (!enqueueMessage(makeMessage(QStringLiteral("Echo"), requestId,
                                    {{QStringLiteral("text"), text}}))) {
        m_pending.remove(requestId);
        return -1;
    }
    appendLog(QStringLiteral("已发送 Echo #%1：%2").arg(requestId).arg(text));
    return requestId;
}

void GameClient::clearLog()
{
    if (!m_logLines.isEmpty()) {
        m_logLines.clear();
        emit logTextChanged();
    }
}

void GameClient::setHandshakeTimeoutForTesting(const int milliseconds)
{
    m_handshakeTimer.setInterval(qMax(1, milliseconds));
}

void GameClient::setState(const State state)
{
    if (m_state != state) {
        m_state = state;
        emit stateChanged();
    }
}

void GameClient::setError(const QString &message)
{
    m_lastError = message;
    emit lastErrorChanged();
    appendLog(QStringLiteral("错误：%1").arg(message));
}

void GameClient::appendLog(const QString &message)
{
    m_logLines.append(message);
    while (m_logLines.size() > kMaximumLogLines) {
        m_logLines.removeFirst();
    }
    emit logTextChanged();
}

qint32 GameClient::nextRequestId()
{
    qint32 candidate = m_nextRequestId;
    for (qsizetype checked = 0; checked <= m_pending.size(); ++checked) {
        if (!m_pending.contains(candidate)) {
            m_nextRequestId = candidate == Sgs::Protocol::kMaximumRequestId ? 1 : candidate + 1;
            return candidate;
        }
        candidate = candidate == Sgs::Protocol::kMaximumRequestId ? 1 : candidate + 1;
    }
    return -1;
}

bool GameClient::enqueueMessage(const QJsonObject &message)
{
    QString error;
    const QByteArray frame = Sgs::Protocol::MessageCodec::encode(message, &error);
    if (frame.isEmpty()) {
        setError(QStringLiteral("无法编码消息：%1").arg(error));
        return false;
    }
    m_outgoingFrames.enqueue(frame);
    flushWriteQueue();
    return true;
}

void GameClient::flushWriteQueue()
{
    if (m_socket.state() != QAbstractSocket::ConnectedState) {
        return;
    }
    while (!m_outgoingFrames.isEmpty()) {
        const QByteArray &frame = m_outgoingFrames.head();
        const qint64 remaining = frame.size() - m_writeOffset;
        const qint64 accepted = m_socket.write(frame.constData() + m_writeOffset, remaining);
        if (accepted < 0) {
            setError(QStringLiteral("写入 Socket 失败：%1").arg(m_socket.errorString()));
            m_socket.abort();
            return;
        }
        if (accepted == 0) {
            return;
        }
        m_writeOffset += accepted;
        if (m_writeOffset == frame.size()) {
            m_outgoingFrames.dequeue();
            m_writeOffset = 0;
        }
    }
}

void GameClient::sendHello()
{
    const qint32 requestId = nextRequestId();
    if (requestId != 1) {
        failProtocol(QStringLiteral("Hello 未能取得请求编号 1。"));
        return;
    }
    const QJsonObject data{{QStringLiteral("client_name"), QStringLiteral("SgsClient")},
                           {QStringLiteral("client_version"),
                            QCoreApplication::applicationVersion()}};
    m_pending.insert(requestId, {PendingKind::Hello, {}});
    if (!enqueueMessage(makeMessage(QStringLiteral("Hello"), requestId, data))) {
        m_pending.remove(requestId);
        return;
    }
    m_handshakeTimer.start();
    appendLog(QStringLiteral("已发送 Hello #1。"));
}

void GameClient::handleMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    const qint32 requestId = message.value(QStringLiteral("request_id")).toInt();
    const QJsonObject data = message.value(QStringLiteral("data")).toObject();
    if (type == QStringLiteral("Error")) {
        handleErrorMessage(requestId, data);
        return;
    }
    const auto pending = m_pending.constFind(requestId);
    if (pending == m_pending.cend()) {
        appendLog(QStringLiteral("忽略未知请求编号的响应 %1 #%2。").arg(type).arg(requestId));
        return;
    }
    if (pending->kind == PendingKind::Hello && type == QStringLiteral("HelloAck")) {
        if (m_state != Handshaking || !data.value(QStringLiteral("server_name")).isString()
            || !data.value(QStringLiteral("server_version")).isString()) {
            failProtocol(QStringLiteral("收到字段无效的 HelloAck。"));
            return;
        }
        m_pending.erase(pending);
        m_handshakeTimer.stop();
        setState(Ready);
        appendLog(QStringLiteral("SGS/1 握手成功：%1 %2。")
                      .arg(data.value(QStringLiteral("server_name")).toString(),
                           data.value(QStringLiteral("server_version")).toString()));
        return;
    }
    if (pending->kind == PendingKind::Ping && type == QStringLiteral("Pong")) {
        if (!data.isEmpty()) {
            setError(QStringLiteral("Pong #%1 的 data 必须为空对象。").arg(requestId));
            m_pending.erase(pending);
            return;
        }
        m_pending.erase(pending);
        appendLog(QStringLiteral("收到 Pong #%1。").arg(requestId));
        emit pongReceived(requestId);
        return;
    }
    if (pending->kind == PendingKind::Echo && type == QStringLiteral("EchoResult")) {
        if (!data.value(QStringLiteral("text")).isString()
            || data.value(QStringLiteral("text")).toString() != pending->echoText) {
            setError(QStringLiteral("EchoResult #%1 文本与请求不一致。").arg(requestId));
            m_pending.erase(pending);
            return;
        }
        const QString text = data.value(QStringLiteral("text")).toString();
        m_pending.erase(pending);
        appendLog(QStringLiteral("收到 EchoResult #%1：%2").arg(requestId).arg(text));
        emit echoReceived(requestId, text);
        return;
    }
    appendLog(QStringLiteral("忽略类型不匹配的响应 %1 #%2。").arg(type).arg(requestId));
}

void GameClient::handleErrorMessage(const qint32 requestId, const QJsonObject &data)
{
    if (!data.value(QStringLiteral("code")).isString()
        || !data.value(QStringLiteral("message")).isString()) {
        failProtocol(QStringLiteral("收到字段无效的 Error 消息。"));
        return;
    }
    const QString code = data.value(QStringLiteral("code")).toString();
    const QString message = data.value(QStringLiteral("message")).toString();
    m_pending.remove(requestId);
    setError(QStringLiteral("服务器错误 %1 (#%2)：%3").arg(code).arg(requestId).arg(message));
    if (code == QStringLiteral("UNSUPPORTED_PROTOCOL")) {
        m_socket.abort();
    }
}

void GameClient::resetSession()
{
    m_handshakeTimer.stop();
    m_codec.reset();
    m_pending.clear();
    m_outgoingFrames.clear();
    m_writeOffset = 0;
    m_nextRequestId = 1;
}

void GameClient::failProtocol(const QString &message)
{
    setError(QStringLiteral("协议错误：%1").arg(message));
    m_socket.abort();
    resetSession();
    setState(Disconnected);
}
