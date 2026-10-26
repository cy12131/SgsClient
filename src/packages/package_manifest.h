#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

namespace Sgs::Packages {

struct PackageSpec {
    QString id;
    QString version;
    QString apiVersion;
    QString repositoryUrl;
    QString commit;
    QString manifestSha256;
    QStringList dependencies;
};

struct PackageSet {
    QString manifestId;
    QVector<PackageSpec> packages;
    int syncTimeoutMs = 0;
};

[[nodiscard]] bool parsePackageSet(const QJsonObject &data, PackageSet *result,
                                   QString *error);
[[nodiscard]] QString calculateManifestId(const QVector<PackageSpec> &packages,
                                          QString *error = nullptr);
[[nodiscard]] bool validateDependencyGraph(const QVector<PackageSpec> &packages,
                                           QString *error);
[[nodiscard]] bool versionSatisfiesRange(const QString &version, const QString &range);

} // namespace Sgs::Packages
