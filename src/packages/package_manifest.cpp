#include "packages/package_manifest.h"

#include "packages/jcs_canonicalizer.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Sgs::Packages {
namespace {

const QRegularExpression kId(QStringLiteral("^[a-z][a-z0-9]*(?:\\.[a-z][a-z0-9_-]*)+$"));
const QRegularExpression kSemver(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$"));
const QRegularExpression kApi(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"));
const QRegularExpression kHash(QStringLiteral("^[0-9a-f]{64}$"));
const QRegularExpression kCommit(QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$"));
const QRegularExpression kRange(QStringLiteral(
    "^(?:\\*|(?:=|>=|<=|>|<)?(?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*)"
    "(?:-[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?(?: +(?:=|>=|<=|>|<)?(?:0|[1-9][0-9]*)"
    "\\.(?:0|[1-9][0-9]*)\\.(?:0|[1-9][0-9]*)(?:-[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?)*)$"));

bool exactKeys(const QJsonObject &object, const QSet<QString> &expected)
{
    const QStringList keys = object.keys();
    return QSet<QString>(keys.cbegin(), keys.cend()) == expected;
}

struct SemVersion { QString major; QString minor; QString patch; QStringList prerelease; };

bool parseSemver(const QString &value, SemVersion *version)
{
    const auto match = kSemver.match(value);
    if (!match.hasMatch()) return false;
    *version = {match.captured(1), match.captured(2), match.captured(3),
                match.captured(4).isEmpty() ? QStringList{} : match.captured(4).split('.')};
    return true;
}

int compareDecimal(QString a, QString b)
{
    while (a.size() > 1 && a.startsWith('0')) a.removeFirst();
    while (b.size() > 1 && b.startsWith('0')) b.removeFirst();
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    if (a != b) return a < b ? -1 : 1;
    return 0;
}

int compare(const SemVersion &a, const SemVersion &b)
{
    for (const auto &pair : {qMakePair(a.major, b.major), qMakePair(a.minor, b.minor),
                             qMakePair(a.patch, b.patch)}) {
        const int result = compareDecimal(pair.first, pair.second);
        if (result) return result;
    }
    if (a.prerelease.isEmpty() || b.prerelease.isEmpty()) {
        if (a.prerelease.isEmpty() == b.prerelease.isEmpty()) return 0;
        return a.prerelease.isEmpty() ? 1 : -1;
    }
    const qsizetype count = qMin(a.prerelease.size(), b.prerelease.size());
    const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    for (qsizetype i = 0; i < count; ++i) {
        const bool an = digits.match(a.prerelease.at(i)).hasMatch();
        const bool bn = digits.match(b.prerelease.at(i)).hasMatch();
        if (an && bn) { const int result = compareDecimal(a.prerelease.at(i), b.prerelease.at(i)); if (result) return result; }
        else if (an != bn) return an ? -1 : 1;
        else if (a.prerelease.at(i) != b.prerelease.at(i)) return a.prerelease.at(i) < b.prerelease.at(i) ? -1 : 1;
    }
    if (a.prerelease.size() == b.prerelease.size()) return 0;
    return a.prerelease.size() < b.prerelease.size() ? -1 : 1;
}

bool satisfies(const QString &version, const QString &range)
{
    SemVersion actual;
    if (!parseSemver(version, &actual)) return false;
    if (range == QStringLiteral("*")) return true;
    if (!kRange.match(range).hasMatch()) return false;
    for (const QString &token : range.split(' ', Qt::SkipEmptyParts)) {
        const auto match = QRegularExpression(QStringLiteral("^(>=|<=|>|<|=)?(.+)$")).match(token);
        SemVersion expected;
        if (!match.hasMatch() || !parseSemver(match.captured(2), &expected)) return false;
        const int relation = compare(actual, expected);
        const QString op = match.captured(1);
        if ((op.isEmpty() || op == QStringLiteral("=")) && relation != 0) return false;
        if (op == QStringLiteral(">=") && relation < 0) return false;
        if (op == QStringLiteral("<=") && relation > 0) return false;
        if (op == QStringLiteral(">") && relation <= 0) return false;
        if (op == QStringLiteral("<") && relation >= 0) return false;
    }
    return true;
}

QJsonObject normalized(const PackageSpec &spec)
{
    QStringList dependencies = spec.dependencies;
    std::sort(dependencies.begin(), dependencies.end(), [](const auto &a, const auto &b) {
        return a.toLatin1() < b.toLatin1();
    });
    QJsonArray deps;
    for (const auto &dep : dependencies) deps.append(dep);
    return {{QStringLiteral("id"), spec.id},
            {QStringLiteral("version"), spec.version},
            {QStringLiteral("api_version"), spec.apiVersion},
            {QStringLiteral("repository_url"), spec.repositoryUrl},
            {QStringLiteral("commit"), spec.commit},
            {QStringLiteral("manifest_sha256"), spec.manifestSha256},
            {QStringLiteral("dependencies"), deps}};
}

} // namespace

QString calculateManifestId(const QVector<PackageSpec> &input, QString *error)
{
    QVector<PackageSpec> packages = input;
    std::sort(packages.begin(), packages.end(), [](const auto &a, const auto &b) {
        return a.id.toLatin1() < b.id.toLatin1();
    });
    QJsonArray array;
    for (const auto &spec : packages) array.append(normalized(spec));
    const QByteArray canonical = canonicalizeJson(array, error);
    if (canonical.isEmpty() && !array.isEmpty()) return {};
    return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

bool versionSatisfiesRange(const QString &version, const QString &range)
{
    return satisfies(version, range);
}

bool validateDependencyGraph(const QVector<PackageSpec> &packages, QString *error)
{
    QHash<QString, PackageSpec> byId;
    for (const auto &package : packages) {
        if (byId.contains(package.id)) { *error = QStringLiteral("资源包 ID 重复：%1").arg(package.id); return false; }
        byId.insert(package.id, package);
    }
    enum Mark { None, Visiting, Done };
    QHash<QString, Mark> marks;
    std::function<bool(const QString &)> visit = [&](const QString &id) {
        if (marks.value(id) == Visiting) { *error = QStringLiteral("资源包依赖存在环：%1").arg(id); return false; }
        if (marks.value(id) == Done) return true;
        marks[id] = Visiting;
        for (const auto &dependency : byId.value(id).dependencies) {
            if (!byId.contains(dependency)) { *error = QStringLiteral("缺少依赖资源包：%1").arg(dependency); return false; }
            if (!visit(dependency)) return false;
        }
        marks[id] = Done;
        return true;
    };
    for (const auto &package : packages) if (!visit(package.id)) return false;
    return true;
}

bool parsePackageSet(const QJsonObject &data, PackageSet *result, QString *error)
{
    const QSet<QString> rootKeys{QStringLiteral("manifest_id"), QStringLiteral("packages"),
                                 QStringLiteral("sync_timeout_ms")};
    if (!exactKeys(data, rootKeys) || !data.value(QStringLiteral("manifest_id")).isString()
        || !kHash.match(data.value(QStringLiteral("manifest_id")).toString()).hasMatch()
        || !data.value(QStringLiteral("packages")).isArray()
        || !data.value(QStringLiteral("sync_timeout_ms")).isDouble()) {
        *error = QStringLiteral("PackageManifestResult 字段无效。"); return false;
    }
    const double timeoutNumber = data.value(QStringLiteral("sync_timeout_ms")).toDouble();
    if (!std::isfinite(timeoutNumber) || std::floor(timeoutNumber) != timeoutNumber
        || timeoutNumber < 1'000 || timeoutNumber > 1'800'000) {
        *error = QStringLiteral("sync_timeout_ms 必须是整数。"); return false;
    }
    const int timeout = static_cast<int>(timeoutNumber);
    const QJsonArray array = data.value(QStringLiteral("packages")).toArray();
    if (array.size() > 256) {
        *error = QStringLiteral("资源包数量或同步超时超出范围。"); return false;
    }
    QVector<PackageSpec> packages;
    const QSet<QString> packageKeys{QStringLiteral("id"), QStringLiteral("version"),
        QStringLiteral("api_version"), QStringLiteral("repository_url"), QStringLiteral("commit"),
        QStringLiteral("manifest_sha256"), QStringLiteral("dependencies")};
    for (const auto &value : array) {
        if (!value.isObject() || !exactKeys(value.toObject(), packageKeys)) {
            *error = QStringLiteral("资源包条目字段无效。"); return false;
        }
        const auto object = value.toObject();
        PackageSpec spec{object.value(QStringLiteral("id")).toString(),
                         object.value(QStringLiteral("version")).toString(),
                         object.value(QStringLiteral("api_version")).toString(),
                         object.value(QStringLiteral("repository_url")).toString(),
                         object.value(QStringLiteral("commit")).toString(),
                         object.value(QStringLiteral("manifest_sha256")).toString(), {}};
        const QUrl repository(spec.repositoryUrl);
        if (!kId.match(spec.id).hasMatch() || !kSemver.match(spec.version).hasMatch()
            || !kApi.match(spec.apiVersion).hasMatch() || spec.apiVersion != QStringLiteral("1.0")
            || !kCommit.match(spec.commit).hasMatch() || !kHash.match(spec.manifestSha256).hasMatch()
            || !object.value(QStringLiteral("repository_url")).isString()
            || !repository.isValid() || repository.host().isEmpty()
            || (repository.scheme() != QStringLiteral("https")
                && repository.scheme() != QStringLiteral("ssh"))
            || !object.value(QStringLiteral("dependencies")).isArray()) {
            *error = QStringLiteral("资源包 %1 的字段格式无效。").arg(spec.id); return false;
        }
        const auto deps = object.value(QStringLiteral("dependencies")).toArray();
        if (deps.size() > 128) { *error = QStringLiteral("资源包依赖过多。"); return false; }
        QSet<QString> seen;
        for (const auto &depValue : deps) {
            if (!depValue.isString()) { *error = QStringLiteral("资源包依赖必须是包 ID 字符串。"); return false; }
            const QString dependency = depValue.toString();
            if (!kId.match(dependency).hasMatch() || seen.contains(dependency)) {
                *error = QStringLiteral("资源包依赖格式无效或重复。"); return false;
            }
            seen.insert(dependency); spec.dependencies.append(dependency);
        }
        packages.append(spec);
    }
    if (!validateDependencyGraph(packages, error)) return false;
    const QString calculated = calculateManifestId(packages, error);
    if (calculated != data.value(QStringLiteral("manifest_id")).toString()) {
        *error = QStringLiteral("manifest_id 与规范化资源包清单不一致。"); return false;
    }
    *result = {calculated, packages, timeout};
    return true;
}

} // namespace Sgs::Packages
