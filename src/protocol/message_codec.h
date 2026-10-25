#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace Sgs::Protocol {

inline constexpr quint32 kProtocolVersion = 1;
inline constexpr quint32 kMaximumBodySize = 65'536;
inline constexpr qint32 kMaximumRequestId = 2'147'483'647;

struct DecodeResult
{
    QVector<QJsonObject> messages;
    QString error;

    [[nodiscard]] bool isError() const { return !error.isEmpty(); }
};

// MessageCodec 只负责 SGS/1 的字节与 JSON 边界，不持有 Socket。
// 缓冲区允许跨多次 readyRead 保留半包；遇到致命帧错误后由调用方断开连接。
class MessageCodec final
{
public:
    [[nodiscard]] static QByteArray encode(const QJsonObject &message, QString *error = nullptr);
    [[nodiscard]] static bool validateCommonFields(const QJsonObject &message,
                                                   QString *error = nullptr);
    // 接收端除普通响应外，还允许已登记的服务端通知使用 request_id=0。
    // 发送端仍通过 validateCommonFields/encode 强制使用正整数请求编号。
    [[nodiscard]] static bool validateIncomingFields(const QJsonObject &message,
                                                     QString *error = nullptr);
    [[nodiscard]] DecodeResult append(const QByteArray &bytes);
    void reset();

private:
    QByteArray m_buffer;
};

} // namespace Sgs::Protocol
