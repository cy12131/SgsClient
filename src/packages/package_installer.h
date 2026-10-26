#pragma once

#include "packages/package_manifest.h"

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

namespace Sgs::Packages {

struct InstallProgress {
    enum Phase { Checking, Downloading, Verifying } phase = Checking;
    QString packageId;
    QString text;
};

struct InstallResult {
    bool success = false;
    bool cancelled = false;
    QString errorCode;
    QString error;
    QStringList installed;
    QStringList reused;
};

enum class ActiveIndexCommit { Previous, Active };

struct InstallerOptions {
    InstallerOptions() = default;
    InstallerOptions(QString path, const bool allowFile)
        : rootPath(std::move(path)), allowFileRepositories(allowFile) {}

    QString rootPath;
    bool allowFileRepositories = false; // 仅供本地自动化测试夹具使用。
    // 生产环境保持为空；定向测试可在 QSaveFile::commit 前注入失败。
    std::function<bool(ActiveIndexCommit)> indexCommitGuard;
};

class PackageInstaller final
{
public:
    using ProgressCallback = std::function<void(const InstallProgress &)>;

    explicit PackageInstaller(InstallerOptions options);
    [[nodiscard]] InstallResult synchronize(const PackageSet &set,
        const std::shared_ptr<std::atomic_bool> &cancelled,
        const ProgressCallback &progress = {}) const;

private:
    InstallerOptions m_options;
};

} // namespace Sgs::Packages
