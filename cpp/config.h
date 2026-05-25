#pragma once
#include <QString>
#include <QStringList>
#include <vector>

struct CameraConfig {
    QString device;      // /dev/video0
    QString label;       // 前/后/左/右
    int     width  = 1920;
    int     height = 1080;
};

struct ModelConfig {
    QString path;            // model.rknn
    QString labelFile;       // coco_80_labels_list.txt
    float   confThreshold = 0.40f;
    float   nmsThreshold  = 0.45f;
};

struct AppConfig {
    std::vector<CameraConfig> cameras;
    ModelConfig model;
    int    fpsLimit       = 30;
    int    inferTimeoutMs = 8000;
    int    camTimeoutMs   = 5000;
    QString logFile;           // 留空 = 不写日志文件

    bool load(const QString& iniPath);
    static AppConfig defaults();
};
