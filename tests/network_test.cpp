#include "network/game_client.h"
#include "protocol/message_codec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include <QtTest>

#include <functional>

using Sgs::Protocol::MessageCodec;

namespace {

QJsonObject message(const QString &type, const int requestId, const QJsonObject &data = {})
{
    return {{QStringLiteral("protocol"), 1},
            {QStringLiteral("type"), type},
            {QStringLiteral("request_id"), requestId},
            {QStringLiteral("data"), data}};
}

QByteArray rawFrame(const QByteArray &body)
{
    QByteArray frame(4, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    frame.append(body);
    return frame;
}

bool waitUntil(const std::function<bool()> &predicate, const int timeout = 2'000)
{
    QElapsedTimer timer;
    timer.start();
    bool satisfied = predicate();
    while (!satisfied && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
        satisfied = predicate();
    }
    return satisfied;
}

QTcpSocket *connectPeer(GameClient &client, QTcpServer &server)
{
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        qWarning() << "Local test server failed to listen:" << server.errorString();
        return nullptr;
    }
    client.connectToServer(QStringLiteral("127.0.0.1"), server.serverPort());
    if (!waitUntil([&server] { return server.hasPendingConnections(); })) {
        qWarning() << "Local connection timed out; client state/error:"
                   << client.stateText() << client.lastError()
                   << "server:" << server.serverAddress() << server.serverPort();
        return nullptr;
    }
    return server.nextPendingConnection();
}

QJsonObject readMessage(QTcpSocket *peer, bool *ok)
{
    MessageCodec codec;
    QJsonObject received;
    QString error;
    const bool completed = waitUntil([&] {
        if (peer->bytesAvailable() == 0) {
            return false;
        }
        const auto result = codec.append(peer->readAll());
        if (result.isError()) {
            error = result.error;
            return true;
        }
        if (!result.messages.isEmpty()) {
            received = result.messages.constFirst();
            return true;
        }
        return false;
    });
    *ok = completed && error.isEmpty() && !received.isEmpty();
    if (!*ok) {
        qWarning() << "Failed to read SGS/1 message; completed/error/bytes:"
                   << completed << error << peer->bytesAvailable();
    }
    return received;
}

bool completeHandshake(GameClient &client, QTcpSocket *peer)
{
    bool ok = false;
    const QJsonObject hello = readMessage(peer, &ok);
    if (!ok || hello.value(QStringLiteral("type")).toString() != QStringLiteral("Hello")
        || hello.value(QStringLiteral("request_id")).toInt() != 1) {
        qWarning() << "Invalid/missing Hello:" << ok << hello
                   << "client:" << client.stateText() << client.lastError() << client.logText();
        return false;
    }
    const auto ack = message(QStringLiteral("HelloAck"), 1,
                             {{QStringLiteral("server_name"), QStringLiteral("SgsServer")},
                              {QStringLiteral("server_version"), QStringLiteral("0.1.0-dev")}});
    peer->write(MessageCodec::encode(ack));
    peer->flush();
    return waitUntil([&client] { return client.state() == GameClient::Ready; });
}

class ApplicationProxyGuard final
{
public:
    ApplicationProxyGuard()
        : m_previous(QNetworkProxy::applicationProxy())
    {
    }

    ~ApplicationProxyGuard() { QNetworkProxy::setApplicationProxy(m_previous); }

private:
    QNetworkProxy m_previous;
};

} // namespace

class NetworkTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void encodingUsesBigEndianAndUtf8();
    void commonFieldsAreStrictlyValidated();
    void fragmentedAndCoalescedFramesAreDecoded();
    void invalidFramesAreRejected();
    void gameSocketBypassesApplicationProxy();
    void helloAckEntersReady();
    void pingRequiresMatchingResponseId();
    void echoPreservesChineseText();
    void abnormalDisconnectUpdatesState();
    void connectionFailureAllowsRetry();
    void handshakeTimesOut();
    void serverErrorIsReported();
    void unsupportedProtocolStopsSession();
};

void NetworkTest::initTestCase()
{
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0-dev"));
}

void NetworkTest::encodingUsesBigEndianAndUtf8()
{
    const QString text = QStringLiteral("你好，SgsServer！");
    const QByteArray frame = MessageCodec::encode(
        message(QStringLiteral("Echo"), 3, {{QStringLiteral("text"), text}}));
    QVERIFY(!frame.isEmpty());
    const quint32 length = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(frame.constData()));
    QCOMPARE(length, static_cast<quint32>(frame.size() - 4));
    QVERIFY(frame.mid(4).contains(text.toUtf8()));

    MessageCodec codec;
    const auto result = codec.append(frame);
    QVERIFY2(!result.isError(), qPrintable(result.error));
    QCOMPARE(result.messages.size(), 1);
    QCOMPARE(result.messages.first().value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("text")).toString(), text);
}

void NetworkTest::commonFieldsAreStrictlyValidated()
{
    QString error;
    QVERIFY(MessageCodec::validateCommonFields(message(QStringLiteral("Ping"), 1), &error));

    auto invalid = message(QStringLiteral("Ping"), 1);
    invalid.insert(QStringLiteral("protocol"), 1.5);
    QVERIFY(!MessageCodec::validateCommonFields(invalid, &error));
    invalid = message(QStringLiteral("Ping"), 1);
    invalid.insert(QStringLiteral("request_id"), 0);
    QVERIFY(!MessageCodec::validateCommonFields(invalid, &error));
    invalid = message(QStringLiteral("Ping"), 1);
    invalid.insert(QStringLiteral("data"), QStringLiteral("not-an-object"));
    QVERIFY(!MessageCodec::validateCommonFields(invalid, &error));
}

void NetworkTest::fragmentedAndCoalescedFramesAreDecoded()
{
    const QByteArray ping = MessageCodec::encode(message(QStringLiteral("Ping"), 2));
    const QByteArray echo = MessageCodec::encode(message(
        QStringLiteral("Echo"), 3, {{QStringLiteral("text"), QStringLiteral("中文")}}));
    MessageCodec codec;
    QCOMPARE(codec.append(ping.left(1)).messages.size(), 0);
    QCOMPARE(codec.append(ping.mid(1, 2)).messages.size(), 0);
    QCOMPARE(codec.append(ping.mid(3, 5)).messages.size(), 0);
    const auto completed = codec.append(ping.mid(8) + echo);
    QVERIFY2(!completed.isError(), qPrintable(completed.error));
    QCOMPARE(completed.messages.size(), 2);
    QCOMPARE(completed.messages.at(0).value(QStringLiteral("type")).toString(),
             QStringLiteral("Ping"));
    QCOMPARE(completed.messages.at(1).value(QStringLiteral("type")).toString(),
             QStringLiteral("Echo"));
}

void NetworkTest::invalidFramesAreRejected()
{
    MessageCodec codec;
    QByteArray invalidLength(4, '\0');
    QVERIFY(codec.append(invalidLength).isError());

    invalidLength.resize(4);
    qToBigEndian<quint32>(Sgs::Protocol::kMaximumBodySize + 1, invalidLength.data());
    QVERIFY(codec.append(invalidLength).isError());
    QVERIFY(codec.append(rawFrame(QByteArray::fromHex("ff"))).isError());
    QVERIFY(codec.append(rawFrame("not-json")).isError());
    QVERIFY(codec.append(rawFrame(QByteArray("\xEF\xBB\xBF{}", 5))).isError());

    const QByteArray oversizedText(Sgs::Protocol::kMaximumBodySize, 'x');
    QString error;
    QVERIFY(MessageCodec::encode(message(
        QStringLiteral("Echo"), 3, {{QStringLiteral("text"), QString::fromLatin1(oversizedText)}}),
        &error).isEmpty());
}

void NetworkTest::gameSocketBypassesApplicationProxy()
{
    ApplicationProxyGuard proxyGuard;
    Q_UNUSED(proxyGuard);
    QNetworkProxy::setApplicationProxy(QNetworkProxy(
        QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 1));

    QTcpServer server;
    GameClient client;
    QScopedPointer<QTcpSocket> peer(connectPeer(client, server));
    QVERIFY2(peer, "The SGS socket did not bypass the application proxy");
    QVERIFY(completeHandshake(client, peer.data()));
    QCOMPARE(client.state(), GameClient::Ready);
}

void NetworkTest::helloAckEntersReady()
{
    QTcpServer server;
    GameClient client;
    QScopedPointer<QTcpSocket> peer(connectPeer(client, server));
    QVERIFY(peer);
    QVERIFY(completeHandshake(client, peer.data()));
    QCOMPARE(client.state(), GameClient::Ready);
}

void NetworkTest::pingRequiresMatchingResponseId()
{
    QTcpServer server;
    GameClient client;
    QScopedPointer<QTcpSocket> peer(connectPeer(client, server));
    QVERIFY(peer);
    QVERIFY(completeHandshake(client, peer.data()));
    QSignalSpy pongSpy(&client, &GameClient::pongReceived);
    const qint64 requestId = client.sendPing();
    QCOMPARE(requestId, 2);
    bool ok = false;
    const QJsonObject ping = readMessage(peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(ping.value(QStringLiteral("type")).toString(), QStringLiteral("Ping"));

    peer->write(MessageCodec::encode(message(QStringLiteral("Pong"), 99)));
    peer->flush();
    QTest::qWait(50);
    QCOMPARE(pongSpy.count(), 0);
    peer->write(MessageCodec::encode(message(QStringLiteral("Pong"), requestId)));
    peer->flush();
    QVERIFY(waitUntil([&pongSpy] { return pongSpy.count() == 1; }));
}

void NetworkTest::echoPreservesChineseText()
{
    QTcpServer server;
    GameClient client;
    QScopedPointer<QTcpSocket> peer(connectPeer(client, server));
    QVERIFY(peer);
    QVERIFY(completeHandshake(client, peer.data()));
    QSignalSpy echoSpy(&client, &GameClient::echoReceived);
    const QString text = QStringLiteral("你好，SgsServer！雪山🏔");
    const qint64 requestId = client.sendEcho(text);
    bool ok = false;
    const QJsonObject echo = readMessage(peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(echo.value(QStringLiteral("data")).toObject()
                 .value(QStringLiteral("text")).toString(), text);
    peer->write(MessageCodec::encode(message(
        QStringLiteral("EchoResult"), requestId, {{QStringLiteral("text"), text}})));
    peer->flush();
    QVERIFY(waitUntil([&echoSpy] { return echoSpy.count() == 1; }));
    QCOMPARE(echoSpy.first().at(1).toString(), text);
}

void NetworkTest::abnormalDisconnectUpdatesState()
{
    QTcpServer server;
    GameClient client;
    QScopedPointer<QTcpSocket> peer(connectPeer(client, server));
    QVERIFY(peer);
    QVERIFY(completeHandshake(client, peer.data()));
    peer->abort();
    QVERIFY(waitUntil([&client] { return client.state() == GameClient::Disconnected; }));
    QVERIFY(!client.lastError().isEmpty());
}

void NetworkTest::connectionFailureAllowsRetry()
{
    QTcpServer portReservation;
    QVERIFY(portReservation.listen(QHostAddress::LocalHost, 0));
    const quint16 closedPort = portReservation.serverPort();
    portReservation.close();

    GameClient client;
    client.connectToServer(QStringLiteral("127.0.0.1"), closedPort);
    const bool recovered = waitUntil([&client] {
        return client.state() == GameClient::Disconnected && !client.lastError().isEmpty();
    }, 5'000);
    if (!recovered) {
        qWarning() << "Connection failure did not recover:" << client.stateText()
                   << client.lastError() << client.logText();
    }
    QVERIFY(recovered);
    QVERIFY(client.canConnect());
}

void NetworkTest::handshakeTimesOut()
{
    QTcpServer server;
    GameClient client;
    client.setHandshakeTimeoutForTesting(100);
    QScopedPointer<QTcpSocket> peer(connectPeer(client, server));
    QVERIFY(peer);
    bool ok = false;
    const QJsonObject hello = readMessage(peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(hello.value(QStringLiteral("type")).toString(), QStringLiteral("Hello"));
    QVERIFY(waitUntil([&client] { return client.state() == GameClient::Disconnected; }, 1'000));
    QVERIFY(client.lastError().contains(QStringLiteral("超时")));
}

void NetworkTest::serverErrorIsReported()
{
    QTcpServer server;
    GameClient client;
    QScopedPointer<QTcpSocket> peer(connectPeer(client, server));
    QVERIFY(peer);
    QVERIFY(completeHandshake(client, peer.data()));
    peer->write(MessageCodec::encode(message(
        QStringLiteral("Error"), 42,
        {{QStringLiteral("code"), QStringLiteral("UNKNOWN_MESSAGE_TYPE")},
         {QStringLiteral("message"), QStringLiteral("Unsupported message type")}})));
    peer->flush();
    QVERIFY(waitUntil([&client] {
        return client.lastError().contains(QStringLiteral("UNKNOWN_MESSAGE_TYPE"));
    }));
    QCOMPARE(client.state(), GameClient::Ready);
}

void NetworkTest::unsupportedProtocolStopsSession()
{
    QTcpServer server;
    GameClient client;
    QScopedPointer<QTcpSocket> peer(connectPeer(client, server));
    QVERIFY(peer);
    QVERIFY(completeHandshake(client, peer.data()));
    peer->write(MessageCodec::encode(message(
        QStringLiteral("Error"), 77,
        {{QStringLiteral("code"), QStringLiteral("UNSUPPORTED_PROTOCOL")},
         {QStringLiteral("message"), QStringLiteral("Unsupported protocol version")}})));
    peer->flush();
    QVERIFY(waitUntil([&client] { return client.state() == GameClient::Disconnected; }));
    QVERIFY(client.lastError().contains(QStringLiteral("UNSUPPORTED_PROTOCOL")));
    QCOMPARE(client.sendPing(), -1);
}

QTEST_GUILESS_MAIN(NetworkTest)

#include "network_test.moc"
