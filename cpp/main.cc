#include <QApplication>
#include <QDebug>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QMutex>
#include <csignal>
#include <cstdlib>
#include "config.h"
#include "mainwindow.h"

static QApplication* gApp = nullptr;

// 日志落盘：同时输出到 stderr 和文件
static QtMessageHandler s_oldHandler = nullptr;
static QMutex             s_logMutex;

static void logToFile(QtMsgType type, const QMessageLogContext& ctx,
                      const QString& msg)
{
    QMutexLocker lk(&s_logMutex);
    QFile f("/var/log/rknn-avs.log");
    if (f.open(QIODevice::Append | QIODevice::WriteOnly)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ")
           << msg << "\n";
    }
    // 同时走原来的 handler（stderr）
    if (s_oldHandler)
        s_oldHandler(type, ctx, msg);
}

static void sigHandler(int sig)
{
    const char* name = (sig == SIGTERM) ? "SIGTERM"
                     : (sig == SIGINT)  ? "SIGINT"  : "SIGHUP";
    qWarning("[main] received %s, shutting down...", name);
    if (gApp) gApp->quit();
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM",        "eglfs");
    qputenv("QT_SCREEN_SCALE_FACTORS","1");
    qputenv("QT_QPA_EGLFS_HIDECURSOR","1");

    struct sigaction sa{};
    sa.sa_handler = sigHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGHUP,  &sa, nullptr);

    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QApplication app(argc, argv);
    gApp = &app;

    if (argc < 2) {
        qCritical("用法: %s <config.ini | model.rknn>", argv[0]);
        return 1;
    }

    // 加载配置：支持 .ini 配置文件或兼容旧版的模型路径
    AppConfig cfg;
    QString arg1(argv[1]);
    if (arg1.endsWith(".ini")) {
        cfg = AppConfig::defaults();
        cfg.load(arg1);
    } else {
        // 兼容旧版：直接传 model.rknn 路径
        cfg = AppConfig::defaults();
        cfg.model.path = arg1;
    }

    // 日志落盘（如果配置了路径）
    if (!cfg.logFile.isEmpty()) {
        s_oldHandler = qInstallMessageHandler(logToFile);
    }

    MainWindow w(cfg);
    return app.exec();
}
