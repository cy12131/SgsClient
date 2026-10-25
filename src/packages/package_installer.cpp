#include "packages/package_installer.h"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUuid>
#include <QUrl>

#include <algorithm>

namespace Sgs::Packages {
namespace {

constexpr qint64 kMaximumManifestBytes = 1024 * 1024;

struct CommandResult { bool ok = false; bool cancelled = false; QByteArray output; };

CommandResult git(const QString &workingDirectory, const QStringList &arguments,
                  const QString &emptyHooks, QDeadlineTimer &deadline,
                  const std::shared_ptr<std::atomic_bool> &cancelled,
                  const bool allowFileRepositories)
{
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    environment.insert(QStringLiteral("GIT_ASKPASS"), QStringLiteral(""));
    environment.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
    environment.insert(QStringLiteral("GIT_CONFIG_GLOBAL"), QStringLiteral("NUL"));
    process.setProcessEnvironment(environment);
    QStringList safe{QStringLiteral("-c"), QStringLiteral("core.hooksPath=%1").arg(emptyHooks),
                     QStringLiteral("-c"), allowFileRepositories
                         ? QStringLiteral("protocol.file.allow=always")
                         : QStringLiteral("protocol.file.allow=never"),
                     QStringLiteral("-c"), QStringLiteral("protocol.ext.allow=never")};
    safe.append(arguments);
    if (cancelled->load() || deadline.hasExpired()) return {false, cancelled->load(), {}};
    process.start(QStringLiteral("git"), safe);
    const qint64 startWait = std::clamp(deadline.remainingTime(), qint64{1}, qint64{5'000});
    if (!process.waitForStarted(startWait)) return {};
    while (!process.waitForFinished(100)) {
        if (cancelled->load() || deadline.hasExpired()) {
            process.kill(); process.waitForFinished(2'000);
            return {false, cancelled->load(), {}};
        }
    }
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0,
            false, process.readAllStandardOutput().trimmed()};
}

bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.contains('\\') || path.startsWith('/')
        || path.size() > 240 || path.contains(QChar::Null) || QDir::isAbsolutePath(path)
        || QRegularExpression(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]*:")).match(path).hasMatch()) return false;
    const QString clean = QDir::cleanPath(path);
    return clean == path && clean != QStringLiteral("..") && !clean.startsWith(QStringLiteral("../"));
}

bool validateDeclaredPath(const QString &root, const QString &relative, const bool allowDirectory,
                          QString *error)
{
    if (!safeRelativePath(relative)) { *error = QStringLiteral("Manifest 路径不安全：%1").arg(relative); return false; }
    const QFileInfo info(QDir(root).filePath(relative));
    if (!info.exists() || (!allowDirectory && !info.isFile())) { *error = QStringLiteral("Manifest 声明路径不存在或类型无效：%1").arg(relative); return false; }
    const QString canonicalRoot = QDir::fromNativeSeparators(QFileInfo(root).canonicalFilePath()) + '/';
    const QString canonicalFile = QDir::fromNativeSeparators(info.canonicalFilePath());
    if (!canonicalFile.startsWith(canonicalRoot, Qt::CaseInsensitive)) {
        *error = QStringLiteral("Manifest 文件越过资源包根目录：%1").arg(relative); return false;
    }
    // Windows 默认大小写不敏感，逐段枚举确认声明的实际大小写。
    QString current = root;
    for (const QString &segment : relative.split('/')) {
        const QStringList entries = QDir(current).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        if (!entries.contains(segment, Qt::CaseSensitive)) {
            *error = QStringLiteral("Manifest 文件路径大小写不匹配：%1").arg(relative); return false;
        }
        current = QDir(current).filePath(segment);
    }
    return true;
}

bool validateManifest(const QString &root, const PackageSpec &spec,
                      const QHash<QString, QString> &versions, QString *error)
{
    QFile file(QDir(root).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > kMaximumManifestBytes) {
        *error = QStringLiteral("无法读取有效的 manifest.json。"); return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) { *error = QStringLiteral("manifest.json 不得包含 BOM。"); return false; }
    if (QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
        != spec.manifestSha256) { *error = QStringLiteral("manifest.json SHA-256 校验失败。"); return false; }
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder.decode(bytes);
    if (decoder.hasError()) { *error = QStringLiteral("manifest.json 不是有效 UTF-8。"); return false; }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("manifest.json 不是有效 JSON 对象。"); return false;
    }
    const QJsonObject object = document.object();
    const QSet<QString> allowed{QStringLiteral("schema_version"), QStringLiteral("id"),
        QStringLiteral("name"), QStringLiteral("version"), QStringLiteral("api_version"),
        QStringLiteral("dependencies"), QStringLiteral("server"), QStringLiteral("client"),
        QStringLiteral("resources")};
    const QSet<QString> required{QStringLiteral("schema_version"), QStringLiteral("id"),
        QStringLiteral("name"), QStringLiteral("version"), QStringLiteral("api_version"),
        QStringLiteral("dependencies")};
    const QStringList objectKeys = object.keys();
    const auto keys = QSet<QString>(objectKeys.cbegin(), objectKeys.cend());
    for (const auto &key : keys) if (!allowed.contains(key)) { *error = QStringLiteral("Manifest 包含未知字段：%1").arg(key); return false; }
    for (const auto &key : required) if (!object.contains(key)) { *error = QStringLiteral("Manifest 缺少字段：%1").arg(key); return false; }
    if (object.value(QStringLiteral("schema_version")).toInt(-1) != 1
        || object.value(QStringLiteral("id")).toString() != spec.id
        || object.value(QStringLiteral("version")).toString() != spec.version
        || object.value(QStringLiteral("api_version")).toString() != spec.apiVersion
        || !object.value(QStringLiteral("name")).isString()
        || object.value(QStringLiteral("name")).toString().toUcs4().isEmpty()
        || object.value(QStringLiteral("name")).toString().toUcs4().size() > 80
        || !object.value(QStringLiteral("dependencies")).isArray()
        || (!object.contains(QStringLiteral("server")) && !object.contains(QStringLiteral("client"))
            && object.value(QStringLiteral("resources")).toArray().isEmpty())) {
        *error = QStringLiteral("Manifest 身份、版本或入口字段无效。"); return false;
    }
    QHash<QString, QString> manifestDeps;
    for (const auto &value : object.value(QStringLiteral("dependencies")).toArray()) {
        if (!value.isObject()) { *error = QStringLiteral("Manifest 依赖格式无效。"); return false; }
        const auto dep = value.toObject();
        if (dep.size() != 2 || !dep.value(QStringLiteral("id")).isString()
            || !dep.value(QStringLiteral("version")).isString()) {
            *error = QStringLiteral("Manifest 依赖字段无效。"); return false;
        }
        const QString depId = dep.value(QStringLiteral("id")).toString();
        if (manifestDeps.contains(depId)) { *error = QStringLiteral("Manifest 依赖 ID 重复。"); return false; }
        manifestDeps.insert(depId, dep.value(QStringLiteral("version")).toString());
    }
    if (manifestDeps.size() != spec.dependencies.size()) { *error = QStringLiteral("Manifest 依赖与服务器锁定清单不一致。"); return false; }
    for (const auto &depId : spec.dependencies) {
        if (!manifestDeps.contains(depId) || !versions.contains(depId)) {
            *error = QStringLiteral("Manifest 依赖锁定不一致：%1").arg(depId); return false;
        }
        if (!versionSatisfiesRange(versions.value(depId), manifestDeps.value(depId))) {
            *error = QStringLiteral("Manifest 依赖版本不满足：%1 %2（实际 %3）")
                         .arg(depId, manifestDeps.value(depId), versions.value(depId)); return false;
        }
    }
    if (object.contains(QStringLiteral("server"))) {
        const auto server = object.value(QStringLiteral("server")).toObject();
        if (server.size() != 1 || !server.value(QStringLiteral("entry")).isString()
            || !server.value(QStringLiteral("entry")).toString().endsWith(QStringLiteral(".lua"))
            || !validateDeclaredPath(root, server.value(QStringLiteral("entry")).toString(), false, error)) return false;
    }
    if (object.contains(QStringLiteral("client"))) {
        const auto client = object.value(QStringLiteral("client")).toObject();
        if (client.size() != 1 || !client.value(QStringLiteral("qml_entry")).isString()
            || !client.value(QStringLiteral("qml_entry")).toString().endsWith(QStringLiteral(".qml"))
            || !validateDeclaredPath(root, client.value(QStringLiteral("qml_entry")).toString(), false, error)) return false;
    }
    if (object.contains(QStringLiteral("resources"))) {
        if (!object.value(QStringLiteral("resources")).isArray()) {
            *error = QStringLiteral("Manifest resources 必须是数组。"); return false;
        }
        const auto resources = object.value(QStringLiteral("resources")).toArray();
        if (resources.isEmpty() || resources.size() > 256) { *error = QStringLiteral("Manifest resources 必须包含 1～256 个路径。"); return false; }
        QSet<QString> seen;
        for (const auto &value : resources) {
            if (!value.isString() || seen.contains(value.toString())
                || !validateDeclaredPath(root, value.toString(), true, error)) return false;
            seen.insert(value.toString());
        }
    }
    return true;
}

bool writeActive(const QString &root, const PackageSet &set, QString *error)
{
    QJsonArray packages;
    for (const auto &spec : set.packages)
        packages.append(QJsonObject{{QStringLiteral("id"), spec.id}, {QStringLiteral("commit"), spec.commit},
                                    {QStringLiteral("path"), QStringLiteral("installed/%1/%2").arg(spec.id, spec.commit)}});
    const QString activePath = QDir(root).filePath(QStringLiteral("active.json"));
    if (QFile::exists(activePath)) {
        QFile::remove(QDir(root).filePath(QStringLiteral("active.previous.json")));
        if (!QFile::copy(activePath, QDir(root).filePath(QStringLiteral("active.previous.json")))) {
            *error = QStringLiteral("无法保存上一版资源包激活索引。"); return false;
        }
    }
    QSaveFile file(activePath);
    if (!file.open(QIODevice::WriteOnly)) { *error = QStringLiteral("无法写入资源包激活索引。"); return false; }
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("manifest_id"), set.manifestId},
                                         {QStringLiteral("packages"), packages}}).toJson(QJsonDocument::Indented));
    if (!file.commit()) { *error = QStringLiteral("无法原子发布资源包激活索引。"); return false; }
    return true;
}

} // namespace

PackageInstaller::PackageInstaller(InstallerOptions options) : m_options(std::move(options)) {}

InstallResult PackageInstaller::synchronize(const PackageSet &set,
    const std::shared_ptr<std::atomic_bool> &cancelled, const ProgressCallback &progress) const
{
    InstallResult result;
    QDir root(m_options.rootPath);
    if (!root.mkpath(QStringLiteral("installed")) || !root.mkpath(QStringLiteral(".staging"))
        || !root.mkpath(QStringLiteral(".empty-hooks"))) {
        result.errorCode = QStringLiteral("PACKAGE_STORAGE_ERROR");
        result.error = QStringLiteral("无法创建客户端资源包目录。"); return result;
    }
    QDeadlineTimer deadline(set.syncTimeoutMs);
    QHash<QString, QString> versions;
    for (const auto &package : set.packages) versions.insert(package.id, package.version);
    for (const auto &spec : set.packages) {
        if (cancelled->load()) { result.cancelled = true; return result; }
        if (progress) progress({InstallProgress::Checking, spec.id, QStringLiteral("检查本地资源包")});
        const QString destination = root.filePath(QStringLiteral("installed/%1/%2").arg(spec.id, spec.commit));
        if (QFileInfo::exists(destination)) {
            const auto head = git(destination, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
                                  root.filePath(QStringLiteral(".empty-hooks")), deadline, cancelled,
                                  m_options.allowFileRepositories);
            const auto clean = git(destination, {QStringLiteral("status"), QStringLiteral("--porcelain"),
                                                 QStringLiteral("--untracked-files=all")},
                                   root.filePath(QStringLiteral(".empty-hooks")), deadline, cancelled,
                                   m_options.allowFileRepositories);
            QString error;
            if (!head.ok || QString::fromLatin1(head.output) != spec.commit || !clean.ok
                || !clean.output.isEmpty() || !validateManifest(destination, spec, versions, &error)) {
                result.errorCode = QStringLiteral("LOCAL_PACKAGE_MODIFIED");
                result.error = QStringLiteral("本地资源包 %1 已被修改或校验失败；为保护用户文件不会覆盖。%2")
                                   .arg(spec.id, error.isEmpty() ? QString() : QStringLiteral(" ") + error);
                return result;
            }
            result.reused.append(spec.id); continue;
        }
        const QUrl url(spec.repositoryUrl);
        const bool https = url.isValid() && url.scheme() == QStringLiteral("https")
            && !url.host().isEmpty() && url.userInfo().isEmpty();
        const bool testFile = m_options.allowFileRepositories && url.isLocalFile();
        if (!https && !testFile) {
            result.errorCode = QStringLiteral("UNSUPPORTED_REPOSITORY_URL");
            result.error = QStringLiteral("资源包 %1 只允许无凭据的 HTTPS 仓库地址。").arg(spec.id); return result;
        }
        const QString stage = root.filePath(QStringLiteral(".staging/%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        if (progress) progress({InstallProgress::Downloading, spec.id, QStringLiteral("从 Git 仓库下载锁定提交")});
        const auto clone = git(root.absolutePath(), {QStringLiteral("clone"), QStringLiteral("--no-checkout"),
            QStringLiteral("--filter=blob:none"), QStringLiteral("--"), spec.repositoryUrl, stage},
            root.filePath(QStringLiteral(".empty-hooks")), deadline, cancelled,
            m_options.allowFileRepositories);
        if (!clone.ok) { QDir(stage).removeRecursively(); result.cancelled = clone.cancelled;
            result.errorCode = clone.cancelled ? QStringLiteral("PACKAGE_SYNC_CANCELLED") : QStringLiteral("PACKAGE_DOWNLOAD_FAILED");
            result.error = clone.cancelled ? QStringLiteral("资源包同步已取消。") : QStringLiteral("资源包 %1 下载失败。").arg(spec.id); return result; }
        const auto checkout = git(stage, {QStringLiteral("checkout"), QStringLiteral("--detach"), spec.commit},
                                  root.filePath(QStringLiteral(".empty-hooks")), deadline, cancelled,
                                  m_options.allowFileRepositories);
        const auto head = git(stage, {QStringLiteral("rev-parse"), QStringLiteral("HEAD")},
                              root.filePath(QStringLiteral(".empty-hooks")), deadline, cancelled,
                              m_options.allowFileRepositories);
        if (!checkout.ok || !head.ok || QString::fromLatin1(head.output) != spec.commit) {
            QDir(stage).removeRecursively(); result.errorCode = QStringLiteral("PACKAGE_COMMIT_NOT_FOUND");
            result.error = QStringLiteral("资源包 %1 的锁定提交不存在。").arg(spec.id); return result;
        }
        if (progress) progress({InstallProgress::Verifying, spec.id, QStringLiteral("校验 Manifest 与资源路径")});
        QString validationError;
        if (!validateManifest(stage, spec, versions, &validationError)) {
            QDir(stage).removeRecursively(); result.errorCode = QStringLiteral("PACKAGE_MANIFEST_INVALID");
            result.error = QStringLiteral("资源包 %1 校验失败：%2").arg(spec.id, validationError); return result;
        }
        const QString parent = QFileInfo(destination).absolutePath();
        QDir().mkpath(parent);
        if (!QDir().rename(stage, destination)) {
            QDir(stage).removeRecursively(); result.errorCode = QStringLiteral("PACKAGE_PUBLISH_FAILED");
            result.error = QStringLiteral("资源包 %1 无法原子发布。").arg(spec.id); return result;
        }
        result.installed.append(spec.id);
    }
    QString publishError;
    if (!writeActive(root.absolutePath(), set, &publishError)) {
        result.errorCode = QStringLiteral("PACKAGE_PUBLISH_FAILED"); result.error = publishError; return result;
    }
    result.success = true;
    return result;
}

} // namespace Sgs::Packages
