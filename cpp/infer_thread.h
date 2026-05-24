#pragma once
#include <QThread>
#include <QString>
#include <array>
#include <atomic>
#include "camera.h"
#include "yolov5.h"

// 独立NPU推理线程：永远4路全跑，不管当前显示哪路
// 安防场景：切到单路视图时其他路推理继续，有人立刻发信号报警
class InferThread : public QThread
{
    Q_OBJECT
public:
    // 置信度阈值：低于此值的检测框直接丢弃
    // 平衡检出率和误报率，可根据实际场景调整
    static constexpr float CONF_THRESHOLD = 0.35f;

    // 推理轮询：每路最多等待多久没新帧就跳过（ms）
    // 避免某一路卡顿时其他路被饿死
    static constexpr int FRAME_WAIT_MS = 8;

    explicit InferThread(const QString& modelPath, QObject* parent = nullptr);
    ~InferThread();

    void setCameras(std::array<CameraThread*, 4> cams) { m_cams = cams; }
    bool init();
    void stop();

    // 看门狗：上次推理成功的时间（主窗口定时检查）
    qint64 lastInferMs() const {
        return m_lastInferMs.load(std::memory_order_relaxed);
    }

signals:
    // hasPersons=true 表示当前帧检测到人，false 表示清空
    void personDetected(int camIdx, bool hasPersons);

protected:
    void run() override;

private:
    void inferOne(int idx, std::vector<uint8_t>& rgb);

    QString  m_modelPath;
    bool     m_running = false;
    bool     m_modelOk = false;

    rknn_app_context_t m_ctx{};
    std::array<CameraThread*, 4> m_cams{};

    // 看门狗
    std::atomic<qint64> m_lastInferMs{0};
};
