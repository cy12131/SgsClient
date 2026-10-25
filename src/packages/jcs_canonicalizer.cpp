#include "packages/jcs_canonicalizer.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <charconv>
#include <cmath>

namespace Sgs::Packages {
namespace {

void fail(QString *error, const QString &text)
{
    if (error) *error = text;
}

QByteArray quote(const QString &text)
{
    QByteArray out{"\""};
    const QByteArray utf8 = text.toUtf8();
    for (const unsigned char ch : utf8) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(ch >> 4) & 0xf];
                out += hex[ch & 0xf];
            } else {
                out += static_cast<char>(ch);
            }
        }
    }
    out += '"';
    return out;
}

bool validUnicode(const QString &text)
{
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch.isHighSurrogate()) {
            if (i + 1 >= text.size() || !text.at(i + 1).isLowSurrogate()) return false;
            ++i;
        } else if (ch.isLowSurrogate()) {
            return false;
        }
    }
    return true;
}

bool append(const QJsonValue &value, QByteArray *out, QString *error)
{
    if (value.isNull()) { *out += "null"; return true; }
    if (value.isBool()) { *out += value.toBool() ? "true" : "false"; return true; }
    if (value.isString()) {
        if (!validUnicode(value.toString())) { fail(error, QStringLiteral("JCS 不允许未配对的 Unicode surrogate。")); return false; }
        *out += quote(value.toString()); return true;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (!std::isfinite(number)) { fail(error, QStringLiteral("JCS 不允许非有限数字。")); return false; }
        if (number == 0) { *out += '0'; return true; }
        char buffer[64];
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), number,
                                             std::chars_format::general);
        if (converted.ec != std::errc{}) { fail(error, QStringLiteral("JCS 数字编码失败。")); return false; }
        QByteArray text(buffer, static_cast<qsizetype>(converted.ptr - buffer));
        text.replace('E', 'e');
        qsizetype e = text.indexOf('e');
        if (e >= 0) {
            bool ok = false;
            const int exponent = text.mid(e + 1).toInt(&ok);
            if (ok && exponent >= -6 && exponent < 21) {
                QByteArray mantissa = text.left(e);
                const bool negative = mantissa.startsWith('-');
                if (negative) mantissa.remove(0, 1);
                const qsizetype dot = mantissa.indexOf('.');
                const int integerDigits = dot < 0 ? mantissa.size() : dot;
                mantissa.replace(".", "");
                const int decimalPosition = integerDigits + exponent;
                if (decimalPosition <= 0)
                    mantissa.prepend(QByteArray(-decimalPosition, '0')).prepend("0.");
                else if (decimalPosition >= mantissa.size())
                    mantissa.append(QByteArray(decimalPosition - mantissa.size(), '0'));
                else
                    mantissa.insert(decimalPosition, '.');
                if (negative) mantissa.prepend('-');
                text = mantissa;
                e = -1;
            }
        }
        if (e >= 0) {
            qsizetype digit = e + 1;
            if (digit < text.size() && (text.at(digit) == '+' || text.at(digit) == '-')) ++digit;
            while (digit + 1 < text.size() && text.at(digit) == '0') text.remove(digit, 1);
        }
        *out += text;
        return true;
    }
    if (value.isArray()) {
        *out += '[';
        const QJsonArray array = value.toArray();
        for (qsizetype i = 0; i < array.size(); ++i) {
            if (i) *out += ',';
            if (!append(array.at(i), out, error)) return false;
        }
        *out += ']';
        return true;
    }
    if (value.isObject()) {
        *out += '{';
        const QJsonObject object = value.toObject();
        QStringList keys = object.keys();
        std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) { return a < b; });
        for (qsizetype i = 0; i < keys.size(); ++i) {
            if (i) *out += ',';
            if (!validUnicode(keys.at(i))) { fail(error, QStringLiteral("JCS 对象键包含未配对的 Unicode surrogate。")); return false; }
            *out += quote(keys.at(i));
            *out += ':';
            if (!append(object.value(keys.at(i)), out, error)) return false;
        }
        *out += '}';
        return true;
    }
    fail(error, QStringLiteral("JCS 遇到不支持的 JSON 值。"));
    return false;
}

} // namespace

QByteArray canonicalizeJson(const QJsonValue &value, QString *error)
{
    QByteArray result;
    if (!append(value, &result, error)) return {};
    return result;
}

} // namespace Sgs::Packages
