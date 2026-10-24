#include "application.h"

#include <QGuiApplication>

#include <cstdlib>

int main(int argc, char *argv[])
{
    // QGuiApplication 必须比 QQmlApplicationEngine 更早创建、更晚销毁。
    QGuiApplication qtApplication(argc, argv);
    Application application(qtApplication);

    if (!application.initialize()) {
        return EXIT_FAILURE;
    }

    return qtApplication.exec();
}
