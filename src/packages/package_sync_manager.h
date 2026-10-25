#pragma once

#include "packages/package_installer.h"

#include <QFutureWatcher>
#include <QObject>
#include <atomic>
#include <memory>

class GameClient;
class QJsonObject;

// PackageSyncManager 独占资源包同步会话，不把 Git、Manifest 或门禁逻辑放进大厅模型。
// 后台任务通过取消标记和 generation 隔离；对象析构时等待工作线程结束，避免悬空回调。
class PackageSyncManager final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString currentPackage READ currentPackage NOTIFY progressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY progressChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    Q_PROPERTY(bool retryable READ retryable NOTIFY errorChanged)
    Q_PROPERTY(QString packagesPath READ packagesPath CONSTANT)

public:
    enum State { Disconnected, Handshaking, FetchingManifest, CheckingPackages,
                 Downloading, Verifying, Ready, Failed };
    Q_ENUM(State)

    explicit PackageSyncManager(GameClient &client, QString packagesPath = {},
                                QObject *parent = nullptr);
    ~PackageSyncManager() override;

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString currentPackage() const { return m_currentPackage; }
    [[nodiscard]] QString statusText() const { return m_statusText; }
    [[nodiscard]] QString errorText() const { return m_errorText; }
    [[nodiscard]] bool retryable() const { return m_retryable; }
    [[nodiscard]] QString packagesPath() const;

    Q_INVOKABLE void retry();

signals:
    void stateChanged();
    void progressChanged();
    void errorChanged();

private:
    void handleClientState();
    void handleResponse(const QString &type, qint64 requestId, const QJsonObject &data);
    void handleRequestError(qint64 requestId, const QString &code, const QString &message);
    void handleNotification(const QString &type, const QJsonObject &data);
    void fetchManifest();
    void beginInstall(const Sgs::Packages::PackageSet &set);
    void finishInstall();
    void sendPackageReady();
    void cancelWork();
    void setState(State state, const QString &status = {});
    void fail(const QString &code, const QString &message, bool retryable = true);

    GameClient &m_client;
    QString m_packagesPath;
    State m_state = Disconnected;
    QString m_currentPackage;
    QString m_statusText;
    QString m_errorText;
    QString m_errorCode;
    bool m_retryable = false;
    qint64 m_manifestRequestId = -1;
    qint64 m_readyRequestId = -1;
    Sgs::Packages::PackageSet m_packageSet;
    QFutureWatcher<Sgs::Packages::InstallResult> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    quint64 m_generation = 0;
    bool m_installPending = false;
};
