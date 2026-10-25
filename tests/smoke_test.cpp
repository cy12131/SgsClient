#include <QDir>
#include <QLibraryInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QtTest>

#ifndef SGS_CLIENT_EXECUTABLE
#error "SGS_CLIENT_EXECUTABLE must point to the built client"
#endif

class SmokeTest final : public QObject
{
    Q_OBJECT

private slots:
    void qmlApplicationStartsAndStops();
};

void SmokeTest::qmlApplicationStartsAndStops()
{
    QProcess process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    environment.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
    environment.insert(
        QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"),
        QDir(QLibraryInfo::path(QLibraryInfo::PluginsPath)).filePath(QStringLiteral("platforms")));
    process.setProcessEnvironment(environment);
    process.setProgram(QString::fromUtf8(SGS_CLIENT_EXECUTABLE));
    process.setArguments({QStringLiteral("--smoke-test")});
    process.start();

    QVERIFY2(process.waitForStarted(5000), qPrintable(process.errorString()));
    QVERIFY2(process.waitForFinished(10000), "客户端冒烟测试超时");
    const auto output = process.readAllStandardError();
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QVERIFY2(process.exitCode() == 0, output.constData());

    QVERIFY2(!output.contains("QQmlApplicationEngine failed"), output.constData());
    QVERIFY2(!output.contains("module \""), output.constData());
}

QTEST_GUILESS_MAIN(SmokeTest)

#include "smoke_test.moc"
