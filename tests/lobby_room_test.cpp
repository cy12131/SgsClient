#include "models/lobby_model.h"
#include "models/room_model.h"
#include "network/game_client.h"
#include "packages/package_sync_manager.h"
#include "protocol/message_codec.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QQueue>
#include <QScopedPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
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

QByteArray incomingFrame(const QJsonObject &object)
{
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray frame(4, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    frame.append(body);
    return frame;
}

bool waitUntil(const std::function<bool()> &predicate, const int timeout = 2'000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QTest::qWait(5);
    }
    return predicate();
}

class PeerReader final
{
public:
    QJsonObject next(QTcpSocket *peer, bool *ok)
    {
        const bool available = waitUntil([&] {
            if (!m_messages.isEmpty()) {
                return true;
            }
            if (peer->bytesAvailable() == 0) {
                return false;
            }
            const auto result = m_codec.append(peer->readAll());
            if (result.isError()) {
                m_error = result.error;
                return true;
            }
            for (const QJsonObject &item : result.messages) {
                m_messages.enqueue(item);
            }
            return !m_messages.isEmpty();
        });
        *ok = available && m_error.isEmpty() && !m_messages.isEmpty();
        return *ok ? m_messages.dequeue() : QJsonObject{};
    }

private:
    MessageCodec m_codec;
    QQueue<QJsonObject> m_messages;
    QString m_error;
};

struct Fixture
{
    QTemporaryDir packages;
    GameClient client;
    PackageSyncManager packageSync{client, packages.path()};
    RoomModel room{client};
    LobbyModel lobby{client, packageSync, room};
    QTcpServer server;
    QScopedPointer<QTcpSocket> peer;
    PeerReader reader;

    bool acceptConnection()
    {
        if (!waitUntil([this] { return server.hasPendingConnections(); })) {
            return false;
        }
        peer.reset(server.nextPendingConnection());
        return peer != nullptr;
    }

    bool startLobby(const QString &nickname = QStringLiteral("测试玩家"),
                    const qint64 userId = 1002)
    {
        if (!server.isListening() && !server.listen(QHostAddress::LocalHost, 0)) {
            return false;
        }
        lobby.connectAndEnter(QStringLiteral("127.0.0.1"), server.serverPort(), nickname);
        if (!acceptConnection()) {
            return false;
        }
        bool ok = false;
        const QJsonObject hello = reader.next(peer.data(), &ok);
        if (!ok || hello.value(QStringLiteral("type")).toString() != QStringLiteral("Hello")
            || hello.value(QStringLiteral("request_id")).toInt() != 1) {
            return false;
        }
        peer->write(incomingFrame(message(
            QStringLiteral("HelloAck"), 1,
            {{QStringLiteral("server_name"), QStringLiteral("SgsServer")},
             {QStringLiteral("server_version"), QStringLiteral("0.2.0-m2")}})));
        peer->flush();
        const QJsonObject manifest = reader.next(peer.data(), &ok);
        if (!ok || manifest.value(QStringLiteral("type")).toString() != QStringLiteral("GetPackageManifest")) {
            return false;
        }
        peer->write(incomingFrame(message(QStringLiteral("PackageManifestResult"),
            manifest.value(QStringLiteral("request_id")).toInt(),
            {{QStringLiteral("manifest_id"), QStringLiteral("4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945")},
             {QStringLiteral("packages"), QJsonArray{}}, {QStringLiteral("sync_timeout_ms"), 5000}})));
        peer->flush();
        const QJsonObject ready = reader.next(peer.data(), &ok);
        if (!ok || ready.value(QStringLiteral("type")).toString() != QStringLiteral("PackageReady")) return false;
        peer->write(incomingFrame(message(QStringLiteral("PackageReadyResult"),
            ready.value(QStringLiteral("request_id")).toInt(), {{QStringLiteral("ready"), true}})));
        peer->flush();
        const QJsonObject enter = reader.next(peer.data(), &ok);
        if (!ok || enter.value(QStringLiteral("type")).toString() != QStringLiteral("EnterLobby")
            || enter.value(QStringLiteral("data")).toObject()
                   .value(QStringLiteral("nickname")).toString() != nickname) {
            return false;
        }
        peer->write(incomingFrame(message(
            QStringLiteral("EnterLobbyResult"), enter.value(QStringLiteral("request_id")).toInt(),
            {{QStringLiteral("user_id"), userId}, {QStringLiteral("nickname"), nickname}})));
        peer->flush();
        const QJsonObject list = reader.next(peer.data(), &ok);
        if (!ok || list.value(QStringLiteral("type")).toString() != QStringLiteral("ListRooms")) {
            return false;
        }
        peer->write(incomingFrame(message(
            QStringLiteral("ListRoomsResult"), list.value(QStringLiteral("request_id")).toInt(),
            {{QStringLiteral("rooms"), QJsonArray{}}})));
        peer->flush();
        return waitUntil([this] { return lobby.phase() == LobbyModel::Lobby; });
    }

    void send(const QJsonObject &object)
    {
        peer->write(incomingFrame(object));
        peer->flush();
    }

    void sendTogether(const QList<QJsonObject> &objects)
    {
        QByteArray frames;
        for (const QJsonObject &object : objects) {
            frames.append(incomingFrame(object));
        }
        peer->write(frames);
        peer->flush();
    }
};

QJsonObject player(const int userId, const QString &nickname, const int seat,
                   const bool ready, const bool owner)
{
    return {{QStringLiteral("user_id"), userId},
            {QStringLiteral("nickname"), nickname},
            {QStringLiteral("seat"), seat},
            {QStringLiteral("ready"), ready},
            {QStringLiteral("is_owner"), owner}};
}

QJsonObject snapshot(const int revision, const int ownerId, const QJsonArray &players,
                     const int roomId = 7, const int maxPlayers = 4)
{
    return message(QStringLiteral("RoomSnapshot"), 0,
                   {{QStringLiteral("room_id"), roomId},
                    {QStringLiteral("revision"), revision},
                    {QStringLiteral("name"), QStringLiteral("联调房间")},
                    {QStringLiteral("max_players"), maxPlayers},
                    {QStringLiteral("status"), QStringLiteral("waiting")},
                    {QStringLiteral("owner_id"), ownerId},
                    {QStringLiteral("players"), players}});
}

bool joinRoom(Fixture &fixture, const QJsonArray &players, const int revision = 1)
{
    fixture.lobby.joinRoom(7);
    bool ok = false;
    const QJsonObject request = fixture.reader.next(fixture.peer.data(), &ok);
    if (!ok || request.value(QStringLiteral("type")).toString() != QStringLiteral("JoinRoom")) {
        return false;
    }
    const int id = request.value(QStringLiteral("request_id")).toInt();
    fixture.sendTogether({message(QStringLiteral("JoinRoomResult"), id,
                                  {{QStringLiteral("room_id"), 7}}),
                          snapshot(revision, 1001, players)});
    return waitUntil([&fixture] { return fixture.room.active(); });
}

} // namespace

class LobbyRoomTest final : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void notificationRequestIdRules();
    void enterLobbyAndRefreshOnNotification();
    void createResultAndSnapshotCanBeCoalesced();
    void seatsRevisionsReadyAndOwnerAreAuthoritative();
    void businessErrorAndLeavePreserveState();
    void disconnectClearsSessionAndReconnects();
};

void LobbyRoomTest::initTestCase()
{
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0-dev"));
}

void LobbyRoomTest::notificationRequestIdRules()
{
    MessageCodec codec;
    const auto valid = codec.append(incomingFrame(message(
        QStringLiteral("RoomListChanged"), 0)));
    QVERIFY2(!valid.isError(), qPrintable(valid.error));
    QCOMPARE(valid.messages.size(), 1);

    QString error;
    QVERIFY(MessageCodec::encode(message(QStringLiteral("RoomListChanged"), 0), &error).isEmpty());
    QVERIFY(codec.append(incomingFrame(message(QStringLiteral("Pong"), 0))).isError());
    MessageCodec wrongNotification;
    QVERIFY(wrongNotification.append(incomingFrame(message(
        QStringLiteral("RoomSnapshot"), 8))).isError());
}

void LobbyRoomTest::enterLobbyAndRefreshOnNotification()
{
    Fixture fixture;
    QVERIFY(fixture.startLobby(QStringLiteral("玩家甲"), 1001));
    QCOMPARE(fixture.lobby.userId(), 1001);
    QCOMPARE(fixture.lobby.nickname(), QStringLiteral("玩家甲"));

    fixture.send(message(QStringLiteral("RoomListChanged"), 0));
    bool ok = false;
    const QJsonObject list = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(list.value(QStringLiteral("type")).toString(), QStringLiteral("ListRooms"));
    const QJsonArray rooms{QJsonObject{{QStringLiteral("room_id"), 7},
                                      {QStringLiteral("name"), QStringLiteral("测试房间")},
                                      {QStringLiteral("owner_nickname"), QStringLiteral("房主")},
                                      {QStringLiteral("player_count"), 1},
                                      {QStringLiteral("max_players"), 4},
                                      {QStringLiteral("status"), QStringLiteral("waiting")}}};
    fixture.send(message(QStringLiteral("ListRoomsResult"),
                         list.value(QStringLiteral("request_id")).toInt(),
                         {{QStringLiteral("rooms"), rooms}}));
    QVERIFY(waitUntil([&fixture] { return fixture.lobby.rooms().size() == 1; }));
    QCOMPARE(fixture.lobby.rooms().first().toMap().value(QStringLiteral("room_id")).toLongLong(), 7);
}

void LobbyRoomTest::createResultAndSnapshotCanBeCoalesced()
{
    Fixture fixture;
    QVERIFY(fixture.startLobby(QStringLiteral("房主"), 1001));
    fixture.lobby.createRoom(QStringLiteral("联调房间"), 4);
    bool ok = false;
    const QJsonObject create = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(create.value(QStringLiteral("type")).toString(), QStringLiteral("CreateRoom"));
    const int id = create.value(QStringLiteral("request_id")).toInt();
    fixture.sendTogether({message(QStringLiteral("CreateRoomResult"), id,
                                  {{QStringLiteral("room_id"), 7}}),
                          snapshot(1, 1001,
                                   QJsonArray{player(1001, QStringLiteral("房主"), 1, false, true)})});
    QVERIFY(waitUntil([&fixture] { return fixture.room.active(); }));
    QCOMPARE(fixture.lobby.phase(), LobbyModel::InRoom);
    QCOMPARE(fixture.room.revision(), 1);
    QVERIFY(fixture.room.currentUserOwner());
}

void LobbyRoomTest::seatsRevisionsReadyAndOwnerAreAuthoritative()
{
    Fixture fixture;
    QVERIFY(fixture.startLobby());
    const QJsonArray initial{player(1001, QStringLiteral("房主"), 1, false, true),
                             player(1002, QStringLiteral("测试玩家"), 3, false, false)};
    QVERIFY(joinRoom(fixture, initial, 5));
    QVERIFY(fixture.room.playerAtSeat(2).isEmpty());
    QCOMPARE(fixture.room.playerAtSeat(3).value(QStringLiteral("user_id")).toLongLong(), 1002);

    fixture.send(snapshot(4, 1001,
                          QJsonArray{player(1001, QStringLiteral("房主"), 1, false, true),
                                     player(1002, QStringLiteral("测试玩家"), 2, true, false)}));
    QTest::qWait(50);
    QCOMPARE(fixture.room.revision(), 5);
    QVERIFY(fixture.room.playerAtSeat(2).isEmpty());

    fixture.room.setReady(true);
    bool ok = false;
    const QJsonObject ready = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(ready.value(QStringLiteral("type")).toString(), QStringLiteral("SetReady"));
    const int readyId = ready.value(QStringLiteral("request_id")).toInt();
    fixture.sendTogether({message(QStringLiteral("SetReadyResult"), readyId,
                                  {{QStringLiteral("ready"), true}}),
                          snapshot(6, 1001,
                                   QJsonArray{player(1001, QStringLiteral("房主"), 1, false, true),
                                              player(1002, QStringLiteral("测试玩家"), 3, true, false)})});
    QVERIFY(waitUntil([&fixture] { return fixture.room.currentUserReady(); }));
    QCOMPARE(fixture.room.playerAtSeat(3).value(QStringLiteral("ready")).toBool(), true);

    fixture.send(snapshot(6, 1001,
                          QJsonArray{player(1001, QStringLiteral("房主"), 1, false, true),
                                     player(1002, QStringLiteral("测试玩家"), 2, false, false)}));
    QTest::qWait(50);
    QCOMPARE(fixture.room.playerAtSeat(3).value(QStringLiteral("ready")).toBool(), true);

    fixture.room.setReady(false);
    const QJsonObject cancelReady = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(cancelReady.value(QStringLiteral("type")).toString(), QStringLiteral("SetReady"));
    fixture.sendTogether({message(QStringLiteral("SetReadyResult"),
                                  cancelReady.value(QStringLiteral("request_id")).toInt(),
                                  {{QStringLiteral("ready"), false}}),
                          snapshot(7, 1001,
                                   QJsonArray{player(1001, QStringLiteral("房主"), 1, false, true),
                                              player(1002, QStringLiteral("测试玩家"), 3, false, false)})});
    QVERIFY(waitUntil([&fixture] { return !fixture.room.currentUserReady(); }));

    fixture.send(snapshot(8, 1002,
                          QJsonArray{player(1002, QStringLiteral("测试玩家"), 3, false, true)}));
    QVERIFY(waitUntil([&fixture] { return fixture.room.currentUserOwner(); }));
    QCOMPARE(fixture.room.playerAtSeat(3).value(QStringLiteral("seat")).toInt(), 3);
    QVERIFY(!fixture.room.currentUserReady());
}

void LobbyRoomTest::businessErrorAndLeavePreserveState()
{
    Fixture fixture;
    QVERIFY(fixture.startLobby());
    fixture.lobby.joinRoom(99);
    bool ok = false;
    const QJsonObject failedJoin = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    fixture.send(message(QStringLiteral("Error"),
                         failedJoin.value(QStringLiteral("request_id")).toInt(),
                         {{QStringLiteral("code"), QStringLiteral("ROOM_FULL")},
                          {QStringLiteral("message"), QStringLiteral("Room is full")}}));
    QVERIFY(waitUntil([&fixture] { return fixture.lobby.errorText().contains(QStringLiteral("已满")); }));
    QVERIFY(!fixture.room.active());
    QCOMPARE(fixture.lobby.phase(), LobbyModel::Lobby);

    QVERIFY(joinRoom(fixture,
                     QJsonArray{player(1001, QStringLiteral("房主"), 1, false, true),
                                player(1002, QStringLiteral("测试玩家"), 2, false, false)}));
    fixture.room.leaveRoom();
    const QJsonObject leave = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(leave.value(QStringLiteral("type")).toString(), QStringLiteral("LeaveRoom"));
    fixture.send(message(QStringLiteral("LeaveRoomResult"),
                         leave.value(QStringLiteral("request_id")).toInt()));
    QVERIFY(waitUntil([&fixture] { return !fixture.room.active(); }));
    QCOMPARE(fixture.lobby.phase(), LobbyModel::Lobby);
    const QJsonObject refresh = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(refresh.value(QStringLiteral("type")).toString(), QStringLiteral("ListRooms"));
}

void LobbyRoomTest::disconnectClearsSessionAndReconnects()
{
    Fixture fixture;
    QVERIFY(fixture.startLobby());
    QVERIFY(joinRoom(fixture,
                     QJsonArray{player(1001, QStringLiteral("房主"), 1, false, true),
                                player(1002, QStringLiteral("测试玩家"), 2, false, false)}));
    fixture.peer->abort();
    QVERIFY(waitUntil([&fixture] { return fixture.client.state() == GameClient::Disconnected; }));
    QCOMPARE(fixture.lobby.userId(), 0);
    QCOMPARE(fixture.lobby.rooms().size(), 0);
    QVERIFY(!fixture.room.active());

    fixture.peer.reset();
    fixture.reader = PeerReader{};
    fixture.lobby.connectAndEnter(QStringLiteral("127.0.0.1"), fixture.server.serverPort(),
                                  QStringLiteral("重连玩家"));
    QVERIFY(fixture.acceptConnection());
    bool ok = false;
    const QJsonObject hello = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(hello.value(QStringLiteral("request_id")).toInt(), 1);
    fixture.send(message(QStringLiteral("HelloAck"), 1,
                         {{QStringLiteral("server_name"), QStringLiteral("SgsServer")},
                          {QStringLiteral("server_version"), QStringLiteral("0.2.0-m2")}}));
    const QJsonObject manifest = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(manifest.value(QStringLiteral("type")).toString(), QStringLiteral("GetPackageManifest"));
    fixture.send(message(QStringLiteral("PackageManifestResult"),
        manifest.value(QStringLiteral("request_id")).toInt(),
        {{QStringLiteral("manifest_id"), QStringLiteral("4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945")},
         {QStringLiteral("packages"), QJsonArray{}}, {QStringLiteral("sync_timeout_ms"), 5000}}));
    const QJsonObject ready = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(ready.value(QStringLiteral("type")).toString(), QStringLiteral("PackageReady"));
    fixture.send(message(QStringLiteral("PackageReadyResult"),
                         ready.value(QStringLiteral("request_id")).toInt(),
                         {{QStringLiteral("ready"), true}}));
    const QJsonObject enter = fixture.reader.next(fixture.peer.data(), &ok);
    QVERIFY(ok);
    QCOMPARE(enter.value(QStringLiteral("type")).toString(), QStringLiteral("EnterLobby"));
    QCOMPARE(enter.value(QStringLiteral("request_id")).toInt(), 4);
}

QTEST_GUILESS_MAIN(LobbyRoomTest)

#include "lobby_room_test.moc"
