#include <QApplication>
#include <QDebug>
#include <csignal>
#include <cstdlib>
#include "mainwindow.h"

// 全局指针，供信号处理器调用 qApp->quit()
static QApplication* gApp = nullptr;

static void sigHandler(int sig)
{
    const char* name = (sig == SIGTERM) ? "SIGTERM" : (sig == SIGINT) ? "SIGINT" : "SIGHUP";
    qWarning("[main] received %s, shutting down...", name);
    if (gApp) gApp->quit();
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM",        "eglfs");
    qputenv("QT_SCREEN_SCALE_FACTORS","1");
    qputenv("QT_QPA_EGLFS_HIDECURSOR","1");

    // 注册信号处理器，支持 systemctl stop / kill 优雅退出
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
        qCritical("用法: %s <model.rknn>", argv[0]);
        return 1;
    }

    MainWindow w(argv[1]);
    return app.exec();
}
