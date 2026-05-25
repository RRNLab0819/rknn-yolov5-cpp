#include "config.h"
#include <QDebug>
#include <QFileInfo>
#include <QSettings>

AppConfig AppConfig::defaults()
{
    AppConfig c;
    c.cameras.push_back({"/dev/video0", "前"});
    c.cameras.push_back({"/dev/video1", "后"});
    c.cameras.push_back({"/dev/video2", "左"});
    c.cameras.push_back({"/dev/video3", "右"});
    c.model.path      = "model/yolov5.rknn";
    c.model.labelFile = "model/coco_80_labels_list.txt";
    c.fpsLimit        = 30;
    c.inferTimeoutMs  = 8000;
    c.camTimeoutMs    = 5000;
    return c;
}

bool AppConfig::load(const QString& iniPath)
{
    if (!QFileInfo::exists(iniPath)) {
        qWarning("[config] file not found: %s, using defaults",
                 qPrintable(iniPath));
        return false;
    }

    QSettings s(iniPath, QSettings::IniFormat);
    cameras.clear();

    int count = s.value("camera/count", 4).toInt();
    for (int i = 0; i < count; i++) {
        s.beginGroup(QString("cam%1").arg(i));
        CameraConfig cam;
        cam.device = s.value("device",
                    QString("/dev/video%1").arg(i)).toString();
        cam.label  = s.value("label",
                    QString("CAM%1").arg(i + 1)).toString();
        cam.width  = s.value("width",  1920).toInt();
        cam.height = s.value("height", 1080).toInt();
        cameras.push_back(cam);
        s.endGroup();
    }

    s.beginGroup("model");
    model.path      = s.value("path",      "model/yolov5s_relu.rknn").toString();
    model.labelFile = s.value("label_file","model/coco_80_labels_list.txt").toString();
    model.confThreshold = s.value("conf_thresh", 0.40f).toFloat();
    model.nmsThreshold  = s.value("nms_thresh",  0.45f).toFloat();
    s.endGroup();

    s.beginGroup("display");
    fpsLimit = s.value("fps_limit", 30).toInt();
    s.endGroup();

    s.beginGroup("watchdog");
    inferTimeoutMs = s.value("infer_timeout_ms", 8000).toInt();
    camTimeoutMs   = s.value("cam_timeout_ms",   5000).toInt();
    s.endGroup();

    logFile = s.value("log/file").toString();

    qDebug("[config] loaded %s: %d cameras, model=%s, conf=%.2f, fps=%d",
           qPrintable(iniPath), (int)cameras.size(),
           qPrintable(model.path), model.confThreshold, fpsLimit);
    return true;
}
