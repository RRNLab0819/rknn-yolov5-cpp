#pragma once
#include <QThread>
#include <QImage>
#include <QMutex>
#include <QString>
#include <deque>
#include <vector>
#include <atomic>

struct V4L2Buf  { void* start = nullptr; size_t length = 0; };
struct PersonBox{ int x1, y1, x2, y2, conf; };

class CameraThread : public QThread
{
    Q_OBJECT
public:
    static constexpr int CW = 1920, CH = 1080;
    static constexpr int MW = 640,  MH = 640;

    // 重连参数
    static constexpr int RECONNECT_DELAY_MS = 2000; // 掉线后等2秒再试
    static constexpr int RECONNECT_MAX      = 10;   // 最大连续重试次数

    explicit CameraThread(int idx, QObject* parent = nullptr);
    ~CameraThread();

    bool init();  // 首次初始化（主线程调用，仅打开设备验证一次）
    void stop();

    // GUI线程：取最新显示帧（已叠框）
    bool takeLatestFrame(QImage& out, int& fps);

    // InferThread：取一帧原始RGB做推理，无新帧返回false
    bool takeInferFrame(std::vector<uint8_t>& rgb);

    // InferThread：推理完毕，写回框列表
    void setBoxes(std::vector<PersonBox> boxes);

    // 看门狗支持
    void   touchWatchdog();                              // 采集线程每帧调用
    bool   isAlive(qint64 nowMs, int timeoutMs = 5000) const; // 主窗口定时查

signals:
    void errorOccurred(int idx, QString msg);
    void reconnecting(int idx);   // 开始重连，UI显示"重连中"
    void reconnected(int idx);    // 重连成功，UI恢复正常

protected:
    void run() override;

private:
    bool openDev();
    bool initMmap();
    bool startStream();
    void stopStream();
    void closeAll();
    bool rgaScale(const void* nv12Src, int sw, int sh,
                  void* rgbDst,        int dw, int dh);
    void drawBoxes(QImage& img);
    int  updateFps(qint64 nowMs);

    // 内部重连循环，stop()后返回false
    bool tryReconnect();

    const int            m_idx;
    int                  m_fd      = -1;
    bool                 m_running = false;
    std::vector<V4L2Buf> m_bufs;

    QMutex m_mu;

    QImage m_frame;
    int    m_fps    = 0;
    bool   m_hasNew = false;

    std::vector<uint8_t> m_inferRGB;
    bool                 m_hasInfer = false;

    std::vector<PersonBox> m_boxes;
    std::deque<qint64>     m_ts;

    // 看门狗：atomic，跨线程读写无需加锁
    std::atomic<qint64> m_lastActiveMs{0};
};
