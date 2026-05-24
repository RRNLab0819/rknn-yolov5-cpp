#include <QApplication>
#include <QDebug>
#include "mainwindow.h"

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM",        "eglfs");
    qputenv("QT_SCREEN_SCALE_FACTORS","1");
    // 双保险：环境变量 + 代码里的 BlankCursor
    qputenv("QT_QPA_EGLFS_HIDECURSOR","1");

    QApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QApplication app(argc, argv);

    if (argc < 2) {
        qCritical("用法: %s <model.rknn>", argv[0]);
        return 1;
    }

    MainWindow w(argv[1]);
    return app.exec();
}
