#include "camera.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <string.h>
#include <algorithm>
#include <QDateTime>
#include <QPainter>
#include <QDebug>
#include "im2d.h"
#include "RgaApi.h"

static constexpr int    FPS_LIMIT = 25;
static constexpr qint64 INTERVAL  = 1000 / FPS_LIMIT;  // 40ms

// ─────────────────────────────────────────────────────────────
CameraThread::CameraThread(int idx, QObject* p)
    : QThread(p), m_idx(idx)
{
    m_inferRGB.resize(MW * MH * 3);
}

CameraThread::~CameraThread()
{
    stop();
    wait(5000);
    closeAll();
}

// 首次初始化：主线程调用，只做一次设备探测，验证摄像头存在
// 实际流水线在 run() 里面建立，失败了会自动重连
bool CameraThread::init()
{
    // 简单探测设备是否存在，不 mmap，不 streamon
    QString dev = QString("/dev/video%1").arg(m_idx);
    int fd = ::open(dev.toUtf8().constData(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        qWarning("[cam%d] device not found at init: %s", m_idx, strerror(errno));
        // 不 emit error，允许 run() 里重连
        return false;
    }
    ::close(fd);
    qDebug("[cam%d] device probed OK", m_idx);
    return true;
}

void CameraThread::stop()
{
    m_running = false;
}

void CameraThread::touchWatchdog()
{
    m_lastActiveMs.store(QDateTime::currentMSecsSinceEpoch(),
                         std::memory_order_relaxed);
}

bool CameraThread::isAlive(qint64 nowMs, int timeoutMs) const
{
    qint64 last = m_lastActiveMs.load(std::memory_order_relaxed);
    if (last == 0) return true;  // 还没开始采集，不判超时
    return (nowMs - last) < timeoutMs;
}

bool CameraThread::takeLatestFrame(QImage& out, int& fps)
{
    QMutexLocker lk(&m_mu);
    if (!m_hasNew) return false;
    out      = std::move(m_frame);
    fps      = m_fps;
    m_hasNew = false;
    return true;
}

bool CameraThread::takeInferFrame(std::vector<uint8_t>& rgb)
{
    QMutexLocker lk(&m_mu);
    if (!m_hasInfer) return false;
    rgb        = m_inferRGB;
    m_hasInfer = false;
    return true;
}

void CameraThread::setBoxes(std::vector<PersonBox> boxes)
{
    QMutexLocker lk(&m_mu);
    m_boxes = std::move(boxes);
}

// ─────────────────────────────────────────────────────────────
// 内部重连：在 run() 循环里调用，失败时睡等后重试
// stop() 被调用后立即退出返回 false
bool CameraThread::tryReconnect()
{
    closeAll();

    // 清掉旧的推理框，避免重连后显示残影
    {
        QMutexLocker lk(&m_mu);
        m_boxes.clear();
        m_hasNew   = false;
        m_hasInfer = false;
        m_ts.clear();
        m_fps = 0;
    }

    emit reconnecting(m_idx);

    int retries = 0;
    while (m_running) {
        retries++;
        qDebug("[cam%d] reconnect attempt %d/%d", m_idx, retries, RECONNECT_MAX);

        if (openDev() && initMmap() && startStream()) {
            qDebug("[cam%d] reconnected OK (attempt %d)", m_idx, retries);
            emit reconnected(m_idx);
            return true;
        }

        closeAll();

        if (retries >= RECONNECT_MAX) {
            emit errorOccurred(m_idx,
                QString("cam%1 reconnect failed after %2 attempts, giving up")
                    .arg(m_idx).arg(retries));
            // 给up后继续等，不退出线程，以便外部 stop() 干净退出
            // 每隔 10s 再试一次（设备可能被拔掉重插）
            for (int i = 0; i < 5000 && m_running; i++)
                msleep(2);
            retries = 0;  // 重置计数，继续尝试
        } else {
            // 正常重试间隔
            for (int i = 0; i < RECONNECT_DELAY_MS && m_running; i++)
                msleep(1);
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────
void CameraThread::run()
{
    m_running = true;
    std::vector<uint8_t> rgb(MW * MH * 3);
    qint64 lastStore = 0;

    // 首次建立流水线
    if (!openDev() || !initMmap() || !startStream()) {
        closeAll();
        emit errorOccurred(m_idx, QString("cam%1 initial open failed, will retry").arg(m_idx));
        if (!tryReconnect()) return;
    }

    qDebug("[cam%d] streaming started", m_idx);

    while (m_running) {
        fd_set fds; FD_ZERO(&fds); FD_SET(m_fd, &fds);
        timeval tv{0, 500000};  // 500ms 超时，便于响应 stop()
        int r = select(m_fd + 1, &fds, nullptr, nullptr, &tv);

        if (!m_running) break;

        if (r < 0) {
            if (errno == EINTR) continue;
            qWarning("[cam%d] select error: %s, reconnecting...", m_idx, strerror(errno));
            emit errorOccurred(m_idx, QString("cam%1 select error: %2").arg(m_idx).arg(strerror(errno)));
            if (!tryReconnect()) break;
            continue;
        }
        if (r == 0) {
            // select 超时：主动探测设备是否还在
            // 拔掉摄像头后 select 不会报错，只会持续超时
            // 用 VIDIOC_QUERYCAP 探一下，失败说明设备已消失
            v4l2_capability cap{};
            if (ioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) {
                qWarning("[cam%d] device lost (detected via timeout probe)", m_idx);
                emit errorOccurred(m_idx, QString("cam%1 device unplugged").arg(m_idx));
                if (!tryReconnect()) break;
            }
            continue;
        }

        v4l2_buffer buf{}; v4l2_plane pl[1]{};
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.m.planes = pl;
        buf.length   = 1;

        if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EIO || errno == ENODEV) {
                qWarning("[cam%d] DQBUF error (device lost?): %s", m_idx, strerror(errno));
                emit errorOccurred(m_idx, QString("cam%1 device lost, reconnecting").arg(m_idx));
                if (!tryReconnect()) break;
                continue;
            }
            continue;  // 其他临时错误，跳过这帧
        }

        const void* nv12 = m_bufs[buf.index].start;
        bool ok = rgaScale(nv12, CW, CH, rgb.data(), MW, MH);

        // 立刻还 buffer，不阻塞摄像头流水线
        {
            v4l2_buffer qb{}; v4l2_plane qp[1]{};
            qb.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            qb.memory   = V4L2_MEMORY_MMAP;
            qb.index    = buf.index;
            qb.m.planes = qp;
            qb.length   = 1;
            ioctl(m_fd, VIDIOC_QBUF, &qb);
        }

        if (!ok) continue;

        // 成功采集到帧，更新看门狗
        touchWatchdog();

        // 帧率限制：最多 25fps 送给 GUI
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastStore < INTERVAL) continue;
        lastStore = now;

        QImage img(rgb.data(), MW, MH, MW * 3, QImage::Format_RGB888);
        img = img.copy();
        drawBoxes(img);

        int fps = updateFps(now);

        {
            QMutexLocker lk(&m_mu);
            m_frame    = std::move(img);
            m_fps      = fps;
            m_hasNew   = true;
            m_inferRGB = rgb;   // 直接赋值，避免不必要的拷贝
            m_hasInfer = true;
        }
    }

    stopStream();
    closeAll();
    qDebug("[cam%d] thread exit", m_idx);
}

// ─────────────────────────────────────────────────────────────
bool CameraThread::rgaScale(const void* nv12, int sw, int sh,
                             void* rgb,        int dw, int dh)
{
    rga_buffer_t s = wrapbuffer_virtualaddr(
        const_cast<void*>(nv12), sw, sh, RK_FORMAT_YCbCr_420_SP);
    rga_buffer_t d = wrapbuffer_virtualaddr(
        rgb, dw, dh, RK_FORMAT_RGB_888);
    im_rect sr{0, 0, sw, sh};
    im_rect dr{0, 0, dw, dh};
    return improcess(s, d, {}, sr, dr, {}, IM_COLOR_SPACE_DEFAULT)
           == IM_STATUS_SUCCESS;
}

// ─────────────────────────────────────────────────────────────
void CameraThread::drawBoxes(QImage& img)
{
    std::vector<PersonBox> boxes;
    {
        QMutexLocker lk(&m_mu);
        boxes = m_boxes;
    }
    if (boxes.empty()) return;

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);
    QFont f; f.setPixelSize(14); f.setBold(true); p.setFont(f);
    QFontMetrics fm(f);

    for (auto& b : boxes) {
        int w = b.x2 - b.x1, h = b.y2 - b.y1;
        p.setPen(QPen(QColor(0, 220, 80), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(b.x1, b.y1, w, h);

        QString lb = QString("人 %1%").arg(b.conf);
        int tw = fm.horizontalAdvance(lb) + 8;
        int th = fm.height() + 4;
        int ly = (b.y1 >= th) ? b.y1 - th : b.y1;
        p.fillRect(b.x1, ly, tw, th, QColor(0, 140, 40, 210));
        p.setPen(Qt::white);
        p.drawText(b.x1 + 4, ly + th - 4, lb);
    }
}

int CameraThread::updateFps(qint64 ms)
{
    m_ts.push_back(ms);
    while (m_ts.size() > 30) m_ts.pop_front();
    if (m_ts.size() < 2) return 0;
    qint64 span = m_ts.back() - m_ts.front();
    return span > 0 ? (int)((m_ts.size() - 1) * 1000 / span) : 0;
}

// ─────────────────────────────────────────────────────────────
bool CameraThread::openDev()
{
    QString dev = QString("/dev/video%1").arg(m_idx);
    m_fd = ::open(dev.toUtf8().constData(), O_RDWR | O_NONBLOCK);
    if (m_fd < 0) {
        qWarning("[cam%d] open %s failed: %s", m_idx, qPrintable(dev), strerror(errno));
        return false;
    }

    v4l2_format fmt{};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width                  = CW;
    fmt.fmt.pix_mp.height                 = CH;
    fmt.fmt.pix_mp.pixelformat            = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field                  = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes             = 1;
    fmt.fmt.pix_mp.plane_fmt[0].bytesperline = CW;
    fmt.fmt.pix_mp.plane_fmt[0].sizeimage   = CW * CH * 3 / 2;
    if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
        qWarning("[cam%d] VIDIOC_S_FMT failed: %s", m_idx, strerror(errno));
        ::close(m_fd); m_fd = -1;
        return false;
    }
    return true;
}

bool CameraThread::initMmap()
{
    v4l2_requestbuffers req{};
    req.count  = 4;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) return false;

    m_bufs.resize(req.count);
    for (unsigned i = 0; i < req.count; i++) {
        v4l2_buffer b{}; v4l2_plane pl[1]{};
        b.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        b.memory   = V4L2_MEMORY_MMAP;
        b.index    = i;
        b.m.planes = pl;
        b.length   = 1;
        if (ioctl(m_fd, VIDIOC_QUERYBUF, &b) < 0) return false;

        m_bufs[i].length = pl[0].length;
        m_bufs[i].start  = mmap(nullptr, pl[0].length,
                                PROT_READ | PROT_WRITE, MAP_SHARED,
                                m_fd, pl[0].m.mem_offset);
        if (m_bufs[i].start == MAP_FAILED) return false;
    }
    return true;
}

bool CameraThread::startStream()
{
    for (unsigned i = 0; i < m_bufs.size(); i++) {
        v4l2_buffer b{}; v4l2_plane pl[1]{};
        b.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        b.memory   = V4L2_MEMORY_MMAP;
        b.index    = i;
        b.m.planes = pl;
        b.length   = 1;
        ioctl(m_fd, VIDIOC_QBUF, &b);
    }
    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    return ioctl(m_fd, VIDIOC_STREAMON, &t) == 0;
}

void CameraThread::stopStream()
{
    if (m_fd < 0) return;
    v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(m_fd, VIDIOC_STREAMOFF, &t);
}

void CameraThread::closeAll()
{
    for (auto& b : m_bufs)
        if (b.start && b.start != MAP_FAILED)
            munmap(b.start, b.length);
    m_bufs.clear();
    if (m_fd >= 0) { ::close(m_fd); m_fd = -1; }
}
