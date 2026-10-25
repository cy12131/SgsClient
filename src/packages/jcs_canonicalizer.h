#pragma once

#include <QByteArray>
#include <QJsonValue>
#include <QString>

namespace Sgs::Packages {

// RFC 8785 JSON Canonicalization Scheme. Package manifest IDs currently contain
// strings, arrays and objects; number support is retained for conformance tests.
[[nodiscard]] QByteArray canonicalizeJson(const QJsonValue &value, QString *error = nullptr);

} // namespace Sgs::Packages
