#include "protocol/message_codec.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringDecoder>
#include <QtEndian>

#include <cmath>

namespace Sgs::Protocol {
namespace {

bool isIntegerInRange(const QJsonValue &value, const qint64 minimum, const qint64 maximum)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number
        && number >= static_cast<double>(minimum) && number <= static_cast<double>(maximum);
}

void setError(QString *target, const QString &message)
{
    if (target != nullptr) {
        *target = message;
    }
}

} // namespace

bool MessageCodec::validateCommonFields(const QJsonObject &message, QString *error)
{
    if (!isIntegerInRange(message.value(QStringLiteral("protocol")), 1, kMaximumRequestId)) {
        setError(error, QStringLiteral("protocol 必须是整数。"));
        return false;
    }
    if (message.value(QStringLiteral("protocol")).toInt() != kProtocolVersion) {
        setError(error, QStringLiteral("不支持的协议版本。"));
        return false;
    }
    if (!message.value(QStringLiteral("type")).isString()) {
        setError(error, QStringLiteral("type 必须是字符串。"));
        return false;
    }
    if (!isIntegerInRange(message.value(QStringLiteral("request_id")), 1,
                          kMaximumRequestId)) {
        setError(error, QStringLiteral("request_id 必须是 1～2147483647 的整数。"));
        return false;
    }
    if (!message.value(QStringLiteral("data")).isObject()) {
        setError(error, QStringLiteral("data 必须是 JSON 对象。"));
        return false;
    }
    return true;
}

QByteArray MessageCodec::encode(const QJsonObject &message, QString *error)
{
    QString validationError;
    if (!validateCommonFields(message, &validationError)) {
        setError(error, validationError);
        return {};
    }
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > static_cast<qsizetype>(kMaximumBodySize)) {
        setError(error, QStringLiteral("JSON 消息体长度超出 SGS/1 范围。"));
        return {};
    }
    QByteArray frame(4, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(body.size()), frame.data());
    frame.append(body);
    return frame;
}

DecodeResult MessageCodec::append(const QByteArray &bytes)
{
    DecodeResult result;
    m_buffer.append(bytes);
    while (m_buffer.size() >= 4) {
        const quint32 bodySize = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_buffer.constData()));
        if (bodySize == 0 || bodySize > kMaximumBodySize) {
            result.error = QStringLiteral("收到非法 SGS/1 帧长度：%1。").arg(bodySize);
            m_buffer.clear();
            return result;
        }
        const qsizetype frameSize = 4 + static_cast<qsizetype>(bodySize);
        if (m_buffer.size() < frameSize) {
            break;
        }
        const QByteArray body = m_buffer.mid(4, bodySize);
        m_buffer.remove(0, frameSize);
        if (body.startsWith("\xEF\xBB\xBF")) {
            result.error = QStringLiteral("JSON 消息体不得包含 UTF-8 BOM。");
            m_buffer.clear();
            return result;
        }
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString text = decoder.decode(body);
        if (decoder.hasError()) {
            result.error = QStringLiteral("收到无效 UTF-8 消息体。");
            m_buffer.clear();
            return result;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            result.error = QStringLiteral("收到无效 JSON 对象：%1。").arg(parseError.errorString());
            m_buffer.clear();
            return result;
        }
        const QJsonObject message = document.object();
        QString validationError;
        if (!validateCommonFields(message, &validationError)) {
            result.error = QStringLiteral("消息公共字段无效：%1").arg(validationError);
            m_buffer.clear();
            return result;
        }
        result.messages.append(message);
    }
    return result;
}

void MessageCodec::reset()
{
    m_buffer.clear();
}

} // namespace Sgs::Protocol
