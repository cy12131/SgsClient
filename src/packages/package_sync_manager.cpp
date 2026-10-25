#include "packages/package_sync_manager.h"

#include "network/game_client.h"

#include <QDir>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QtConcurrentRun>

PackageSyncManager::PackageSyncManager(GameClient &client, QString packagesPath, QObject *parent)
    : QObject(parent), m_client(client), m_packagesPath(std::move(packagesPath))
{
    connect(&client, &GameClient::stateChanged, this, &PackageSyncManager::handleClientState);
    connect(&client, &GameClient::responseReceived, this, &PackageSyncManager::handleResponse);
    connect(&client, &GameClient::requestFailed, this, &PackageSyncManager::handleRequestError);
    connect(&client, &GameClient::notificationReceived, this, &PackageSyncManager::handleNotification);
    connect(&m_watcher, &QFutureWatcher<Sgs::Packages::InstallResult>::finished,
            this, &PackageSyncManager::finishInstall);
}

PackageSyncManager::~PackageSyncManager()
{
    cancelWork();
    if (m_watcher.isRunning()) m_watcher.waitForFinished();
}

QString PackageSyncManager::packagesPath() const
{
    if (!m_packagesPath.isEmpty()) return QDir::cleanPath(m_packagesPath);
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("packages"));
}

QString PackageSyncManager::stateText() const
{
    switch (m_state) {
    case Disconnected: return QStringLiteral("未连接");
    case Handshaking: return QStringLiteral("协议握手");
    case FetchingManifest: return QStringLiteral("获取资源包清单");
    case CheckingPackages: return QStringLiteral("检查本地资源包");
    case Downloading: return QStringLiteral("下载资源包");
    case Verifying: return QStringLiteral("校验资源包");
    case Ready: return QStringLiteral("资源包已就绪");
    case Failed: return QStringLiteral("资源包同步失败");
    }
    return QStringLiteral("未知");
}

void PackageSyncManager::retry()
{
    if (m_state == Failed && m_client.isReady() && !m_watcher.isRunning()) fetchManifest();
}

void PackageSyncManager::handleClientState()
{
    switch (m_client.state()) {
    case GameClient::Disconnected:
    case GameClient::Connecting:
        cancelWork();
        m_installPending = false;
        m_manifestRequestId = m_readyRequestId = -1;
        m_currentPackage.clear();
        m_errorText.clear(); m_errorCode.clear(); m_retryable = false;
        emit errorChanged(); emit progressChanged();
        setState(Disconnected, QStringLiteral("连接服务器后将同步资源包"));
        break;
    case GameClient::Handshaking:
        setState(Handshaking, QStringLiteral("等待 HelloAck"));
        break;
    case GameClient::Ready:
        if (m_state == Handshaking || m_state == Disconnected) fetchManifest();
        break;
    }
}

void PackageSyncManager::fetchManifest()
{
    cancelWork();
    m_manifestRequestId = m_readyRequestId = -1;
    m_errorText.clear(); m_errorCode.clear(); m_retryable = false; m_currentPackage.clear();
    emit errorChanged(); emit progressChanged();
    const qint64 id = m_client.sendRequest(QStringLiteral("GetPackageManifest"),
                                           QStringLiteral("PackageManifestResult"));
    if (id < 1) { fail(QStringLiteral("PACKAGE_REQUEST_FAILED"), QStringLiteral("无法发送资源包清单请求。")); return; }
    m_manifestRequestId = id;
    setState(FetchingManifest, QStringLiteral("正在获取服务器锁定清单"));
}

void PackageSyncManager::handleResponse(const QString &type, const qint64 requestId,
                                        const QJsonObject &data)
{
    if (type == QStringLiteral("PackageManifestResult") && requestId == m_manifestRequestId) {
        m_manifestRequestId = -1;
        QString error;
        if (!Sgs::Packages::parsePackageSet(data, &m_packageSet, &error)) {
            fail(QStringLiteral("PACKAGE_MANIFEST_INVALID"), error, false); return;
        }
        beginInstall(m_packageSet); return;
    }
    if (type == QStringLiteral("PackageReadyResult") && requestId == m_readyRequestId) {
        m_readyRequestId = -1;
        if (data.size() == 1 && data.value(QStringLiteral("ready")).isBool()
            && data.value(QStringLiteral("ready")).toBool()) {
            setState(Ready, m_packageSet.packages.isEmpty()
                ? QStringLiteral("服务器资源包清单为空，已显式确认")
                : QStringLiteral("全部资源包已校验并经服务器确认"));
            return;
        }
        if (data.size() != 2 || !data.value(QStringLiteral("ready")).isBool()
            || data.value(QStringLiteral("ready")).toBool() || !data.value(QStringLiteral("error")).isObject()) {
            fail(QStringLiteral("PACKAGE_READY_RESULT_INVALID"),
                 QStringLiteral("PackageReadyResult 字段无效。"), false); return;
        }
        const QJsonObject error = data.value(QStringLiteral("error")).toObject();
        const QSet<QString> allowed{QStringLiteral("code"), QStringLiteral("message"),
                                    QStringLiteral("retryable"), QStringLiteral("details")};
        const QStringList errorKeys = error.keys();
        for (const QString &key : errorKeys) if (!allowed.contains(key)) {
            fail(QStringLiteral("PACKAGE_READY_RESULT_INVALID"),
                 QStringLiteral("PackageReadyResult.error 包含未知字段。"), false); return;
        }
        if (error.size() < 3 || !error.value(QStringLiteral("code")).isString()
            || !error.value(QStringLiteral("message")).isString()
            || !error.value(QStringLiteral("retryable")).isBool()
            || (error.contains(QStringLiteral("details"))
                && !error.value(QStringLiteral("details")).isObject())) {
            fail(QStringLiteral("PACKAGE_READY_RESULT_INVALID"),
                 QStringLiteral("PackageReadyResult.error 字段无效。"), false); return;
        }
        fail(error.value(QStringLiteral("code")).toString(QStringLiteral("PACKAGE_READY_REJECTED")),
             error.value(QStringLiteral("message")).toString(QStringLiteral("服务器拒绝资源包就绪声明。")),
             error.value(QStringLiteral("retryable")).toBool(false));
    }
}

void PackageSyncManager::beginInstall(const Sgs::Packages::PackageSet &set)
{
    m_packageSet = set;
    if (m_watcher.isRunning()) {
        m_installPending = true;
        setState(CheckingPackages, QStringLiteral("等待上一同步任务安全取消"));
        return;
    }
    m_installPending = false;
    setState(CheckingPackages, set.packages.isEmpty()
        ? QStringLiteral("空清单无需下载，仍将发送 PackageReady")
        : QStringLiteral("检查本地锁定提交"));
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    const auto token = ++m_generation;
    const auto cancelled = m_cancelled;
    const QString root = packagesPath();
    QPointer<PackageSyncManager> guard(this);
    auto callback = [guard, token](const Sgs::Packages::InstallProgress &progress) {
        if (!guard) return;
        QMetaObject::invokeMethod(guard, [guard, token, progress] {
            if (!guard || guard->m_generation != token) return;
            guard->m_currentPackage = progress.packageId;
            guard->setState(progress.phase == Sgs::Packages::InstallProgress::Downloading ? Downloading
                : progress.phase == Sgs::Packages::InstallProgress::Verifying ? Verifying : CheckingPackages,
                progress.text);
            emit guard->progressChanged();
        }, Qt::QueuedConnection);
    };
    m_watcher.setFuture(QtConcurrent::run([root, set, cancelled, callback] {
        return Sgs::Packages::PackageInstaller({root, false}).synchronize(set, cancelled, callback);
    }));
}

void PackageSyncManager::finishInstall()
{
    const auto result = m_watcher.result();
    if (m_installPending) {
        m_installPending = false;
        beginInstall(m_packageSet);
        return;
    }
    if (result.cancelled || !m_client.isReady()) return;
    if (!result.success) { fail(result.errorCode, result.error); return; }
    sendPackageReady();
}

void PackageSyncManager::sendPackageReady()
{
    m_currentPackage.clear(); emit progressChanged();
    const qint64 id = m_client.sendRequest(QStringLiteral("PackageReady"),
        QStringLiteral("PackageReadyResult"), {{QStringLiteral("manifest_id"), m_packageSet.manifestId}});
    if (id < 1) { fail(QStringLiteral("PACKAGE_READY_SEND_FAILED"), QStringLiteral("无法发送 PackageReady。")); return; }
    m_readyRequestId = id;
    setState(Verifying, QStringLiteral("等待服务器确认 PackageReady"));
}

void PackageSyncManager::handleRequestError(const qint64 requestId, const QString &code,
                                             const QString &message)
{
    if (requestId != m_manifestRequestId && requestId != m_readyRequestId) return;
    m_manifestRequestId = m_readyRequestId = -1;
    const bool retry = code != QStringLiteral("MANIFEST_ID_MISMATCH")
        && code != QStringLiteral("PACKAGE_MANIFEST_TOO_LARGE");
    fail(code, message, retry);
}

void PackageSyncManager::handleNotification(const QString &type, const QJsonObject &data)
{
    if (type == QStringLiteral("PackageManifestChanged") && m_client.isReady()) {
        // M3 生产策略为重启生效；保留通知处理使未来兼容时不会绕过门禁。
        if (data.size() != 1 || !data.value(QStringLiteral("manifest_id")).isString()
            || !QRegularExpression(QStringLiteral("^[0-9a-f]{64}$"))
                    .match(data.value(QStringLiteral("manifest_id")).toString()).hasMatch()) {
            fail(QStringLiteral("PACKAGE_MANIFEST_CHANGED_INVALID"),
                 QStringLiteral("PackageManifestChanged 字段无效。"), false);
            return;
        }
        fetchManifest();
    }
}

void PackageSyncManager::cancelWork()
{
    ++m_generation;
    if (m_cancelled) m_cancelled->store(true);
}

void PackageSyncManager::setState(const State state, const QString &status)
{
    bool changed = false;
    if (m_state != state) { m_state = state; changed = true; }
    if (!status.isNull() && m_statusText != status) { m_statusText = status; emit progressChanged(); }
    if (changed) emit stateChanged();
}

void PackageSyncManager::fail(const QString &code, const QString &message, const bool retryable)
{
    m_errorCode = code; m_errorText = QStringLiteral("%1：%2").arg(code, message);
    m_retryable = retryable; emit errorChanged();
    setState(Failed, QStringLiteral("同步未完成，禁止进入大厅"));
}
