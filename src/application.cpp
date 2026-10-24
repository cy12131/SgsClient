#include "application.h"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QTimer>

#ifndef SGS_CLIENT_VERSION
#define SGS_CLIENT_VERSION "0.1.0-dev"
#endif

Application::Application(QGuiApplication &qtApplication)
    : m_qtApplication(qtApplication)
{
}

bool Application::initialize()
{
    QCoreApplication::setApplicationName(QStringLiteral("SGS Client"));
    QCoreApplication::setApplicationVersion(QStringLiteral(SGS_CLIENT_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("SgsClient"));

    // objectCreationFailed 是异步失败的兜底；紧随 loadFromModule 的根对象检查
    // 则让启动阶段能把错误明确传回 main，而不是留下一个无窗口的空进程。
    QObject::connect(
        &m_engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &m_qtApplication,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);

    m_engine.loadFromModule(QStringLiteral("SgsClient"), QStringLiteral("Main"));
    if (m_engine.rootObjects().isEmpty()) {
        qCritical() << "无法加载主 QML 界面。";
        return false;
    }

    // 该参数只供自动化冒烟测试使用：QML 已真实创建后再退出事件循环。
    // 正常运行不设置它，窗口会保持显示并由用户关闭。
    if (QCoreApplication::arguments().contains(QStringLiteral("--smoke-test"))) {
        QTimer::singleShot(0, &m_qtApplication, &QCoreApplication::quit);
    }

    return true;
}
