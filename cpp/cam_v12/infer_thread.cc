#include "infer_thread.h"
#include <QDateTime>
#include <QDebug>
#include <algorithm>

InferThread::InferThread(const QString& modelPath, QObject* parent)
    : QThread(parent), m_modelPath(modelPath) {}

InferThread::~InferThread()
{
    stop();
    wait(5000);
    if (m_modelOk) release_yolov5_model(&m_ctx);
}

bool InferThread::init()
{
    int ret = init_yolov5_model(m_modelPath.toUtf8().constData(), &m_ctx);
    if (ret == 0) {
        m_modelOk = true;
        qDebug("[infer] model loaded OK: %s", qPrintable(m_modelPath));
    } else {
        qWarning("[infer] model load FAILED (ret=%d), running without detection", ret);
    }
    return true;
}

void InferThread::stop() { m_running = false; }

void InferThread::run()
{
    m_running = true;
    qDebug("[infer] thread started, modelOk=%d, confThreshold=%.2f",
           (int)m_modelOk, CONF_THRESHOLD);

    // 轮询起始路，保证4路公平轮转
    int cur = 0;

    while (m_running) {
        bool anyWork = false;

        // 每轮扫一遍4路，对每路都尝试取帧
        // 改为：不用 break，扫完整一圈，避免慢路饿死
        for (int i = 0; i < 4; i++) {
            int idx = (cur + i) % 4;
            if (!m_cams[idx]) continue;

            std::vector<uint8_t> rgb;
            if (!m_cams[idx]->takeInferFrame(rgb)) continue;

            if (m_modelOk) {
                inferOne(idx, rgb);
                // 更新推理心跳
                m_lastInferMs.store(QDateTime::currentMSecsSinceEpoch(),
                                    std::memory_order_relaxed);
            }

            anyWork = true;
            // 注意：这里不 break，继续检查剩余路
            // 如果某帧推理耗时很短（<5ms），连续处理多路效率更高
        }

        // 轮转起始路，下一轮从下一个摄像头开始，保证绝对公平
        cur = (cur + 1) % 4;

        if (!anyWork) msleep(FRAME_WAIT_MS);
    }

    qDebug("[infer] thread exit");
}

void InferThread::inferOne(int idx, std::vector<uint8_t>& rgb)
{
    image_buffer_t img{};
    img.width         = CameraThread::MW;
    img.height        = CameraThread::MH;
    img.width_stride  = CameraThread::MW;
    img.height_stride = CameraThread::MH;
    img.format        = IMAGE_FORMAT_RGB888;
    img.virt_addr     = rgb.data();
    img.size          = (uint32_t)(rgb.size());

    object_detect_result_list res{};
    if (inference_yolov5_model(&m_ctx, &img, &res) != 0) {
        qWarning("[infer] inference failed on cam%d", idx);
        return;
    }

    std::vector<PersonBox> boxes;
    boxes.reserve(res.count);

    for (int i = 0; i < res.count; i++) {
        const auto& r = res.results[i];

        // 只要行人（cls_id == 0）
        if (r.cls_id != 0) continue;

        // ★ 置信度过滤：低于阈值直接丢弃，减少误报
        if (r.prop < CONF_THRESHOLD) continue;

        boxes.push_back({
            std::max(0,    (int)r.box.left),
            std::max(0,    (int)r.box.top),
            std::min(CameraThread::MW - 1, (int)r.box.right),
            std::min(CameraThread::MH - 1, (int)r.box.bottom),
            (int)(r.prop * 100.f + 0.5f)
        });
    }

    bool hasPerson = !boxes.empty();
    m_cams[idx]->setBoxes(std::move(boxes));

    // Qt::QueuedConnection 保证跨线程安全，GUI线程收到后更新按钮颜色
    emit personDetected(idx, hasPerson);
}
