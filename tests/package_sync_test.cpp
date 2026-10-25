#include "network/game_client.h"
#include "packages/jcs_canonicalizer.h"
#include "packages/package_installer.h"
#include "packages/package_manifest.h"
#include "packages/package_sync_manager.h"
#include "protocol/message_codec.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QProcess>
#include <QQueue>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QtEndian>
#include <QtConcurrentRun>
#include <QtTest>

#include <atomic>
#include <functional>
#include <memory>

using namespace Sgs::Packages;
using Sgs::Protocol::MessageCodec;

namespace {

constexpr auto kEmptyManifestId = "4f53cda18c2baa0c0354bb5f9a3ecbe5ed12ab4d8e11ba873c2f11161202b945";

QJsonObject message(const QString &type, int requestId, const QJsonObject &data = {})
{
    return {{QStringLiteral("protocol"), 1}, {QStringLiteral("type"), type},
            {QStringLiteral("request_id"), requestId}, {QStringLiteral("data"), data}};
}

bool waitUntil(const std::function<bool()> &predicate, int timeout = 5000)
{
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < timeout) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20); QTest::qWait(5);
    }
    return predicate();
}

class PeerReader final
{
public:
    QJsonObject next(QTcpSocket *peer, bool *ok)
    {
        const bool available = waitUntil([&] {
            if (!m_messages.isEmpty()) return true;
            if (peer->bytesAvailable() == 0) return false;
            const auto result = m_codec.append(peer->readAll());
            if (result.isError()) { m_error = result.error; return true; }
            for (const auto &item : result.messages) m_messages.enqueue(item);
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

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

bool runGit(const QString &directory, const QStringList &args, QByteArray *output = nullptr)
{
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start(QStringLiteral("git"), args);
    if (!process.waitForFinished(10'000) || process.exitCode() != 0) return false;
    if (output) *output = process.readAllStandardOutput().trimmed();
    return true;
}

struct RepoFixture {
    QTemporaryDir directory;
    QByteArray manifest;
    QString hash;
    QString commit;
    QString packageId = QStringLiteral("org.sgs.test");
    QString packageVersion = QStringLiteral("0.1.0");

    bool create(const QString &version = QStringLiteral("0.1.0"),
                const QJsonArray &dependencies = {},
                const QString &id = QStringLiteral("org.sgs.test"))
    {
        if (!directory.isValid()) return false;
        packageId = id; packageVersion = version;
        const QJsonObject object{{QStringLiteral("schema_version"), 1},
            {QStringLiteral("id"), packageId},
            {QStringLiteral("name"), QStringLiteral("Test Package")},
            {QStringLiteral("version"), version}, {QStringLiteral("api_version"), QStringLiteral("1.0")},
            {QStringLiteral("dependencies"), dependencies},
            {QStringLiteral("resources"), QJsonArray{QStringLiteral("asset.txt")}}};
        manifest = QJsonDocument(object).toJson(QJsonDocument::Indented);
        hash = QString::fromLatin1(QCryptographicHash::hash(manifest, QCryptographicHash::Sha256).toHex());
        if (!writeFile(directory.filePath(QStringLiteral("manifest.json")), manifest)
            || !writeFile(directory.filePath(QStringLiteral("asset.txt")), "first")) return false;
        if (!runGit(directory.path(), {QStringLiteral("init"), QStringLiteral("-q")})
            || !runGit(directory.path(), {QStringLiteral("add"), QStringLiteral(".")})
            || !runGit(directory.path(), {QStringLiteral("-c"), QStringLiteral("user.name=SGS Test"),
                QStringLiteral("-c"), QStringLiteral("user.email=test@example.invalid"),
                QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("v1")})) return false;
        QByteArray head;
        if (!runGit(directory.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &head)) return false;
        commit = QString::fromLatin1(head); return true;
    }

    QString secondCommit()
    {
        if (!writeFile(directory.filePath(QStringLiteral("asset.txt")), "second")
            || !runGit(directory.path(), {QStringLiteral("add"), QStringLiteral("asset.txt")})
            || !runGit(directory.path(), {QStringLiteral("-c"), QStringLiteral("user.name=SGS Test"),
                QStringLiteral("-c"), QStringLiteral("user.email=test@example.invalid"),
                QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("v2")})) return {};
        QByteArray head; runGit(directory.path(), {QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &head);
        return QString::fromLatin1(head);
    }

    PackageSpec spec(const QString &lockedCommit = {}) const
    {
        return {packageId, packageVersion, QStringLiteral("1.0"),
            QUrl::fromLocalFile(directory.path()).toString(), lockedCommit.isEmpty() ? commit : lockedCommit,
            hash, {}};
    }
};

PackageSet setFor(const QVector<PackageSpec> &packages)
{
    return {calculateManifestId(packages), packages, 30'000};
}

} // namespace

class PackageSyncTest final : public QObject
{
    Q_OBJECT
private slots:
    void jcsAndManifestIdVectors();
    void strictManifestAndDependencyValidation();
    void firstInstallCacheCommitChangeAndRollback();
    void invalidRepositoryCommitAndManifestFailSafely();
    void emptyManifestProtocolGateAndReconnect();
    void wrongManifestIdFailsGate();
    void backgroundInstallKeepsEventLoopResponsive();
    void publicCnbFirstDownloadAndCache();
};

void PackageSyncTest::jcsAndManifestIdVectors()
{
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(
        R"({"literals":[null,true,false],"string":"€$\u000f\nA'B\"\\\"/","numbers":[333333333.33333329,1E30,4.50,2e-3,0.000000000000000000000000001]})", &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QString canonicalError;
    const QByteArray canonical = canonicalizeJson(doc.object(), &canonicalError);
    QVERIFY2(canonicalError.isEmpty(), qPrintable(canonicalError));
    QVERIFY2(canonical.startsWith("{\"literals\":[null,true,false],\"numbers\":[333333333.3333333,1e+30,4.5,0.002,1e-27],\"string\":"), canonical.constData());
    QCOMPARE(calculateManifestId({}), QString::fromLatin1(kEmptyManifestId));
    PackageSpec base{QStringLiteral("org.sgs.base"), QStringLiteral("1.0.0"), QStringLiteral("1.0"),
        QStringLiteral("https://github.com/cy12131/sgs-base-package.git"), QString(40, QLatin1Char('1')),
        QString(64, QLatin1Char('a')), {}};
    PackageSpec standard{QStringLiteral("org.sgs.standard"), QStringLiteral("1.0.0"), QStringLiteral("1.0"),
        QStringLiteral("https://github.com/cy12131/sgs-standard-package.git"), QString(40, QLatin1Char('2')),
        QString(64, QLatin1Char('b')), {QStringLiteral("org.sgs.base")}};
    QCOMPARE(calculateManifestId({standard, base}),
             QStringLiteral("1b773ff47e081a15bc050331b8beabe6e1e6ee001301ccc11c7249bfe70cf4d8"));
}

void PackageSyncTest::strictManifestAndDependencyValidation()
{
    PackageSet parsed;
    QString error;
    QVERIFY(parsePackageSet({{QStringLiteral("manifest_id"), QString::fromLatin1(kEmptyManifestId)},
        {QStringLiteral("packages"), QJsonArray{}}, {QStringLiteral("sync_timeout_ms"), 1000}}, &parsed, &error));
    QCOMPARE(parsed.packages.size(), 0);
    const QJsonObject base{{QStringLiteral("id"), QStringLiteral("org.sgs.base")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")}, {QStringLiteral("api_version"), QStringLiteral("1.0")},
        {QStringLiteral("repository_url"), QStringLiteral("https://example.invalid/base.git")},
        {QStringLiteral("commit"), QString(40, QLatin1Char('1'))},
        {QStringLiteral("manifest_sha256"), QString(64, QLatin1Char('a'))},
        {QStringLiteral("dependencies"), QJsonArray{}}};
    const QJsonObject standard{{QStringLiteral("id"), QStringLiteral("org.sgs.standard")},
        {QStringLiteral("version"), QStringLiteral("1.0.0")}, {QStringLiteral("api_version"), QStringLiteral("1.0")},
        {QStringLiteral("repository_url"), QStringLiteral("https://example.invalid/standard.git")},
        {QStringLiteral("commit"), QString(40, QLatin1Char('2'))},
        {QStringLiteral("manifest_sha256"), QString(64, QLatin1Char('b'))},
        {QStringLiteral("dependencies"), QJsonArray{QStringLiteral("org.sgs.base")}}};
    const QVector<PackageSpec> vector{
        {QStringLiteral("org.sgs.base"), QStringLiteral("1.0.0"), QStringLiteral("1.0"),
         QStringLiteral("https://example.invalid/base.git"), QString(40, QLatin1Char('1')),
         QString(64, QLatin1Char('a')), {}},
        {QStringLiteral("org.sgs.standard"), QStringLiteral("1.0.0"), QStringLiteral("1.0"),
         QStringLiteral("https://example.invalid/standard.git"), QString(40, QLatin1Char('2')),
         QString(64, QLatin1Char('b')), {QStringLiteral("org.sgs.base")}}};
    QVERIFY(parsePackageSet({{QStringLiteral("manifest_id"), calculateManifestId(vector)},
        {QStringLiteral("packages"), QJsonArray{base, standard}},
        {QStringLiteral("sync_timeout_ms"), 120000}}, &parsed, &error));
    QCOMPARE(parsed.packages.at(1).dependencies, QStringList{QStringLiteral("org.sgs.base")});
    QVERIFY(!parsePackageSet({{QStringLiteral("manifest_id"), QString(64, QLatin1Char('0'))},
        {QStringLiteral("packages"), QJsonArray{}}, {QStringLiteral("sync_timeout_ms"), 1000}}, &parsed, &error));

    PackageSpec a{QStringLiteral("org.sgs.a"), QStringLiteral("1.0.0"), QStringLiteral("1.0"),
        QStringLiteral("https://example.invalid/a.git"), QString(40, QLatin1Char('a')), QString(64, QLatin1Char('a')),
        {QStringLiteral("org.sgs.missing")}};
    QVERIFY(!validateDependencyGraph({a}, &error));
    PackageSpec b{QStringLiteral("org.sgs.missing"), QStringLiteral("2.0.0"), QStringLiteral("1.0"),
        QStringLiteral("https://example.invalid/b.git"), QString(40, QLatin1Char('b')), QString(64, QLatin1Char('b')), {}};
    QVERIFY(validateDependencyGraph({a, b}, &error));
    QVERIFY(!versionSatisfiesRange(QStringLiteral("2.0.0"), QStringLiteral(">=1.0.0 <2.0.0")));
    QVERIFY(versionSatisfiesRange(QStringLiteral("2.0.0"), QStringLiteral(">=2.0.0 <3.0.0")));
    a.dependencies = {b.id}; b.dependencies = {a.id};
    QVERIFY(!validateDependencyGraph({a, b}, &error));
}

void PackageSyncTest::firstInstallCacheCommitChangeAndRollback()
{
    RepoFixture repo; QVERIFY(repo.create());
    QTemporaryDir root; QVERIFY(root.isValid());
    PackageInstaller installer({root.path(), true});
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto first = installer.synchronize(setFor({repo.spec()}), cancelled);
    QVERIFY2(first.success, qPrintable(first.error)); QCOMPARE(first.installed, QStringList{repo.spec().id});
    const QString firstPath = QDir(root.path()).filePath(QStringLiteral("installed/%1/%2").arg(repo.spec().id, repo.commit));
    QVERIFY(QFileInfo::exists(firstPath));
    auto cached = installer.synchronize(setFor({repo.spec()}), cancelled);
    QVERIFY2(cached.success, qPrintable(cached.error)); QCOMPARE(cached.reused, QStringList{repo.spec().id});

    const QString second = repo.secondCommit(); QVERIFY(!second.isEmpty());
    auto upgraded = installer.synchronize(setFor({repo.spec(second)}), cancelled);
    QVERIFY2(upgraded.success, qPrintable(upgraded.error));
    QVERIFY(QFileInfo::exists(firstPath));
    QFile activeSnapshot(root.filePath(QStringLiteral("active.json")));
    QVERIFY(activeSnapshot.open(QIODevice::ReadOnly));
    const QByteArray activeBefore = activeSnapshot.readAll();
    activeSnapshot.close();
    QVERIFY(writeFile(QDir(root.path()).filePath(QStringLiteral("installed/%1/%2/asset.txt").arg(repo.spec().id, second)), "manual edit"));
    auto protectedResult = installer.synchronize(setFor({repo.spec(second)}), cancelled);
    QVERIFY(!protectedResult.success); QCOMPARE(protectedResult.errorCode, QStringLiteral("LOCAL_PACKAGE_MODIFIED"));
    QFile active(root.filePath(QStringLiteral("active.json"))); QVERIFY(active.open(QIODevice::ReadOnly));
    QCOMPARE(active.readAll(), activeBefore);
}

void PackageSyncTest::invalidRepositoryCommitAndManifestFailSafely()
{
    RepoFixture repo; QVERIFY(repo.create()); QTemporaryDir root;
    PackageInstaller installer({root.path(), true}); auto cancelled = std::make_shared<std::atomic_bool>(false);
    auto invalidUrl = repo.spec(); invalidUrl.repositoryUrl = QStringLiteral("ssh://example.invalid/private.git");
    QCOMPARE(installer.synchronize(setFor({invalidUrl}), cancelled).errorCode, QStringLiteral("UNSUPPORTED_REPOSITORY_URL"));
    auto missing = repo.spec(QString(40, QLatin1Char('f')));
    QCOMPARE(installer.synchronize(setFor({missing}), cancelled).errorCode, QStringLiteral("PACKAGE_COMMIT_NOT_FOUND"));
    auto badHash = repo.spec(); badHash.manifestSha256 = QString(64, QLatin1Char('0'));
    QCOMPARE(installer.synchronize(setFor({badHash}), cancelled).errorCode, QStringLiteral("PACKAGE_MANIFEST_INVALID"));
    QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("active.json"))));

    RepoFixture base; QVERIFY(base.create(QStringLiteral("2.0.0"), {}, QStringLiteral("org.sgs.base")));
    const QJsonArray dependencies{QJsonObject{{QStringLiteral("id"), QStringLiteral("org.sgs.base")},
                                               {QStringLiteral("version"), QStringLiteral(">=1.0.0 <2.0.0")}}};
    RepoFixture dependent; QVERIFY(dependent.create(QStringLiteral("1.0.0"), dependencies,
                                                     QStringLiteral("org.sgs.dependent")));
    auto dependentSpec = dependent.spec(); dependentSpec.dependencies = {base.packageId};
    const auto rangeFailure = installer.synchronize(setFor({dependentSpec, base.spec()}), cancelled);
    QCOMPARE(rangeFailure.errorCode, QStringLiteral("PACKAGE_MANIFEST_INVALID"));
}

void PackageSyncTest::emptyManifestProtocolGateAndReconnect()
{
    QTemporaryDir root; GameClient client; PackageSyncManager manager(client, root.path());
    QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    auto oneSession = [&] {
        PeerReader reader;
        client.connectToServer(QStringLiteral("127.0.0.1"), server.serverPort());
        QVERIFY(waitUntil([&] { return server.hasPendingConnections(); }));
        QScopedPointer<QTcpSocket> peer(server.nextPendingConnection()); bool ok = false;
        auto request = reader.next(peer.data(), &ok);
        QVERIFY2(ok, qPrintable(QStringLiteral("client=%1 error=%2 log=%3 bytes=%4")
            .arg(client.stateText(), client.lastError(), client.logText()).arg(peer->bytesAvailable())));
        QCOMPARE(request.value(QStringLiteral("type")).toString(), QStringLiteral("Hello"));
        peer->write(MessageCodec::encode(message(QStringLiteral("HelloAck"), 1,
            {{QStringLiteral("server_name"), QStringLiteral("SgsServer")}, {QStringLiteral("server_version"), QStringLiteral("0.3.0-m3")}}))); peer->flush();
        request = reader.next(peer.data(), &ok); QVERIFY(ok); QCOMPARE(request.value(QStringLiteral("type")).toString(), QStringLiteral("GetPackageManifest"));
        peer->write(MessageCodec::encode(message(QStringLiteral("PackageManifestResult"), request.value(QStringLiteral("request_id")).toInt(),
            {{QStringLiteral("manifest_id"), QString::fromLatin1(kEmptyManifestId)}, {QStringLiteral("packages"), QJsonArray{}},
             {QStringLiteral("sync_timeout_ms"), 5000}}))); peer->flush();
        request = reader.next(peer.data(), &ok); QVERIFY(ok); QCOMPARE(request.value(QStringLiteral("type")).toString(), QStringLiteral("PackageReady"));
        QCOMPARE(request.value(QStringLiteral("data")).toObject().value(QStringLiteral("manifest_id")).toString(), QString::fromLatin1(kEmptyManifestId));
        peer->write(MessageCodec::encode(message(QStringLiteral("PackageReadyResult"), request.value(QStringLiteral("request_id")).toInt(),
            {{QStringLiteral("ready"), true}}))); peer->flush();
        QVERIFY(waitUntil([&] { return manager.state() == PackageSyncManager::Ready; }));
        peer->abort(); QVERIFY(waitUntil([&] { return manager.state() == PackageSyncManager::Disconnected; }));
    };
    oneSession(); oneSession();
}

void PackageSyncTest::wrongManifestIdFailsGate()
{
    PackageSet parsed; QString error;
    QVERIFY(!parsePackageSet({{QStringLiteral("manifest_id"), QString(64, QLatin1Char('0'))},
        {QStringLiteral("packages"), QJsonArray{}}, {QStringLiteral("sync_timeout_ms"), 5000}}, &parsed, &error));
    QVERIFY(error.contains(QStringLiteral("manifest_id")));
}

void PackageSyncTest::backgroundInstallKeepsEventLoopResponsive()
{
    RepoFixture repo; QVERIFY(repo.create()); QTemporaryDir root;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    QTimer timer; int ticks = 0;
    connect(&timer, &QTimer::timeout, this, [&] { ++ticks; }); timer.start(1);
    auto future = QtConcurrent::run([&] {
        return PackageInstaller({root.path(), true}).synchronize(setFor({repo.spec()}), cancelled,
            [](const InstallProgress &) { QThread::msleep(30); });
    });
    QVERIFY(waitUntil([&] { return future.isFinished(); }, 30'000));
    QVERIFY2(future.result().success, qPrintable(future.result().error));
    QVERIFY2(ticks > 3, "Git synchronization blocked the GUI event loop");
}

void PackageSyncTest::publicCnbFirstDownloadAndCache()
{
    if (qEnvironmentVariable("SGS_RUN_PUBLIC_PACKAGE_TEST") != QStringLiteral("1"))
        QSKIP("Set SGS_RUN_PUBLIC_PACKAGE_TEST=1 for the real public CNB download test.");
    QTemporaryDir root; QVERIFY(root.isValid());
    PackageSpec spec{QStringLiteral("org.sgs.testmode"), QStringLiteral("0.1.0"),
        QStringLiteral("1.0"), QStringLiteral("https://cnb.cool/sgs-project/sgs-test-mode.git"),
        QStringLiteral("9d38a4a970448ca17bfbf7c61f20ab77f6f12451"),
        QStringLiteral("d267e42891b2ac4729a21e90ac44a8f6aa1a285d4f14ef8ed68f351ec29b9941"), {}};
    PackageInstaller installer({root.path(), false});
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    const auto first = installer.synchronize(setFor({spec}), cancelled);
    QVERIFY2(first.success, qPrintable(first.error)); QCOMPARE(first.installed, QStringList{spec.id});
    const auto second = installer.synchronize(setFor({spec}), cancelled);
    QVERIFY2(second.success, qPrintable(second.error)); QCOMPARE(second.reused, QStringList{spec.id});
}

QTEST_GUILESS_MAIN(PackageSyncTest)
#include "package_sync_test.moc"
