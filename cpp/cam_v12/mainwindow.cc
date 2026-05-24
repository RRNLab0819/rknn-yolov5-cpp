#include "mainwindow.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QApplication>
#include <QGuiApplication>
#include <QDateTime>
#include <QDebug>

static const char* kDirName[] = {"前", "后", "左", "右"};

int MainWindow::camOf(ViewMode m)
{
    switch (m) {
    case ViewMode::Front: return 0;
    case ViewMode::Rear:  return 1;
    case ViewMode::Left:  return 2;
    case ViewMode::Right: return 3;
    default:              return 0;
    }
}

ViewMode MainWindow::modeOf(int idx)
{
    switch (idx) {
    case 0: return ViewMode::Front;
    case 1: return ViewMode::Rear;
    case 2: return ViewMode::Left;
    case 3: return ViewMode::Right;
    default:return ViewMode::Front;
    }
}

// ═══════════════════════════════════════════════════════════════
// CellWidget
// ═══════════════════════════════════════════════════════════════
CellWidget::CellWidget(int idx, QWidget* parent)
    : QLabel(parent), m_curIdx(idx)
{
    setAlignment(Qt::AlignCenter);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(80, 60);
    setStyleSheet("background:#04080f; border:1px solid #101c2c;");

    m_tag = new QLabel(this);
    m_tag->setFocusPolicy(Qt::NoFocus);
    m_tag->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_tag->setStyleSheet(
        "color:#1e3350; font-size:12px; font-weight:bold;"
        "background:transparent; padding:1px 5px;");
    m_tag->move(6, 6);
    m_tag->raise();

    m_hud = new QLabel("等待…", this);
    m_hud->setFocusPolicy(Qt::NoFocus);
    m_hud->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hud->setStyleSheet(
        "color:#00d858; font-size:12px; font-weight:bold;"
        "background:rgba(0,0,0,150); padding:2px 7px; border-radius:3px;");
    m_hud->raise();

    setDirection(idx);
}

void CellWidget::setDirection(int camIdx)
{
    m_curIdx = camIdx;
    QString txt = QString("%1  CAM%2")
                  .arg(camIdx < 4 ? kDirName[camIdx] : "?")
                  .arg(camIdx + 1);
    m_tag->setText(txt);
    m_tag->adjustSize();
    m_tag->move(6, 6);
}

void CellWidget::resizeEvent(QResizeEvent* e)
{
    QLabel::resizeEvent(e);
    m_hud->adjustSize();
    m_hud->move(6, height() - m_hud->height() - 6);
}

void CellWidget::showFrame(const QImage& img, int fps)
{
    if (img.isNull()) return;
    setPixmap(QPixmap::fromImage(
        img.scaled(size(), Qt::KeepAspectRatio, Qt::FastTransformation)));
    m_hud->setText(QString("● %1fps").arg(fps));
    m_hud->setStyleSheet(
        "color:#00d858; font-size:12px; font-weight:bold;"
        "background:rgba(0,0,0,150); padding:2px 7px; border-radius:3px;");
    m_hud->adjustSize();
    m_hud->move(6, height() - m_hud->height() - 6);
    m_hud->raise();
    m_tag->raise();
}

void CellWidget::showWaiting()
{
    clear();
    setText(QString("%1 CAM%2\n\n等待信号…")
            .arg(m_curIdx < 4 ? kDirName[m_curIdx] : "?")
            .arg(m_curIdx + 1));
    setStyleSheet("background:#04080f; color:#1e3350; font-size:16px;"
                  "border:1px solid #101c2c;");
    m_hud->setText("等待");
}

void CellWidget::showError()
{
    clear();
    setText(QString("CAM%1 错误").arg(m_curIdx + 1));
    setStyleSheet("background:#0a0404; color:#5a1a1a; font-size:16px;"
                  "border:1px solid #3a0c0c;");
    m_hud->setText("错误");
}

void CellWidget::showReconnecting()
{
    clear();
    setText(QString("%1 CAM%2\n\n↻ 重连中…")
            .arg(m_curIdx < 4 ? kDirName[m_curIdx] : "?")
            .arg(m_curIdx + 1));
    setStyleSheet("background:#0a0800; color:#aa7700; font-size:16px;"
                  "border:1px solid #3a2c00;");
    m_hud->setText("↻ 重连");
    m_hud->setStyleSheet(
        "color:#ffaa00; font-size:12px; font-weight:bold;"
        "background:rgba(0,0,0,150); padding:2px 7px; border-radius:3px;");
    m_hud->adjustSize();
    m_hud->move(6, height() - m_hud->height() - 6);
}

// ═══════════════════════════════════════════════════════════════
// NavButton
// ═══════════════════════════════════════════════════════════════
NavButton::NavButton(const QString& label, QWidget* parent)
    : QPushButton(label, parent), m_label(label)
{
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(44);
    setMinimumWidth(90);
    setState(false, false);
}

void NavButton::setState(bool active, bool alert)
{
    if (active) {
        setStyleSheet(
            "QPushButton {"
            "  background:#1a4080; color:#ffffff;"
            "  font-size:16px; font-weight:bold;"
            "  border:2px solid #3070d0; border-radius:6px; padding:0 18px;"
            "}"
            "QPushButton:pressed { background:#0e2860; }");
    } else if (alert) {
        setStyleSheet(
            "QPushButton {"
            "  background:#6a0a0a; color:#ff4444;"
            "  font-size:16px; font-weight:bold;"
            "  border:2px solid #cc2222; border-radius:6px; padding:0 18px;"
            "}"
            "QPushButton:pressed { background:#500808; }");
    } else {
        setStyleSheet(
            "QPushButton {"
            "  background:#0c1828; color:#507090;"
            "  font-size:16px; font-weight:bold;"
            "  border:1px solid #1a2c40; border-radius:6px; padding:0 18px;"
            "}"
            "QPushButton:pressed { background:#101e30; }"
            "QPushButton:hover   { background:#101e30; color:#80a0c0; }");
    }
}

// ═══════════════════════════════════════════════════════════════
// MainWindow
// ═══════════════════════════════════════════════════════════════
MainWindow::MainWindow(const QString& model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    setStyleSheet("background:#04080f;");
    setFocusPolicy(Qt::StrongFocus);
    QGuiApplication::setOverrideCursor(Qt::BlankCursor);

    buildUI();
    showFullScreen();
    setFocus(Qt::OtherFocusReason);
    startCams();

    // GUI 刷新定时器：33ms ≈ 30fps
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::onTimer);
    m_timer->start(33);

    // 看门狗定时器：每秒检查一次
    m_watchdog = new QTimer(this);
    connect(m_watchdog, &QTimer::timeout, this, &MainWindow::onWatchdogTick);
    m_watchdog->start(1000);
}

MainWindow::~MainWindow()
{
    m_timer->stop();
    m_timer->stop();
    m_watchdog->stop();

    if (m_infer) { m_infer->stop(); m_infer->wait(5000); delete m_infer; }
    for (auto* c : m_cams) {
        if (!c) continue;
        c->stop(); c->wait(3000); delete c;
    }
    QGuiApplication::restoreOverrideCursor();
}

// ─────────────────────────────────────────────────────────────
void MainWindow::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_stack = new QStackedWidget(this);
    m_stack->setFocusPolicy(Qt::NoFocus);
    root->addWidget(m_stack, 1);

    // ── 4路全景 ───────────────────────────────────────────────
    m_gridWidget = new QWidget;
    m_gridWidget->setFocusPolicy(Qt::NoFocus);
    {
        auto* gl = new QGridLayout(m_gridWidget);
        gl->setContentsMargins(4, 4, 4, 2);
        gl->setSpacing(4);
        gl->setColumnStretch(0, 1); gl->setColumnStretch(1, 1);
        gl->setRowStretch(0, 1);   gl->setRowStretch(1, 1);
        for (int i = 0; i < 4; i++) {
            m_cells[i] = new CellWidget(i, m_gridWidget);
            m_cells[i]->showWaiting();
            gl->addWidget(m_cells[i], i / 2, i % 2);
        }
    }
    m_stack->addWidget(m_gridWidget);

    // ── 单路全屏 ──────────────────────────────────────────────
    m_singleWidget = new QWidget;
    m_singleWidget->setFocusPolicy(Qt::NoFocus);
    {
        auto* vl = new QVBoxLayout(m_singleWidget);
        vl->setContentsMargins(0, 0, 0, 0);
        vl->setSpacing(0);
        m_singleCell = new CellWidget(0, m_singleWidget);
        m_singleCell->showWaiting();
        vl->addWidget(m_singleCell);
    }
    m_stack->addWidget(m_singleWidget);

    // ── 底部导航栏 ────────────────────────────────────────────
    m_navBar = new QWidget(this);
    m_navBar->setFixedHeight(56);
    m_navBar->setFocusPolicy(Qt::NoFocus);
    m_navBar->setStyleSheet("background:#060d18; border-top:1px solid #0e1e30;");
    {
        auto* hl = new QHBoxLayout(m_navBar);
        hl->setContentsMargins(16, 6, 16, 6);
        hl->setSpacing(0);

        auto* co = new QLabel("北斗拓疆", m_navBar);
        co->setFocusPolicy(Qt::NoFocus);
        co->setFixedWidth(120);
        co->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        co->setStyleSheet(
            "color:#2a5080; font-size:15px; font-weight:bold;"
            "letter-spacing:2px; background:transparent;");
        hl->addWidget(co);

        m_btnPano  = new NavButton("⊞  全景", m_navBar);
        m_btnFront = new NavButton("▲  前",   m_navBar);
        m_btnRear  = new NavButton("▼  后",   m_navBar);
        m_btnLeft  = new NavButton("◀  左",   m_navBar);
        m_btnRight = new NavButton("▶  右",   m_navBar);

        m_camBtns[0] = m_btnFront;
        m_camBtns[1] = m_btnRear;
        m_camBtns[2] = m_btnLeft;
        m_camBtns[3] = m_btnRight;

        auto* bl = new QHBoxLayout;
        bl->setContentsMargins(0, 0, 0, 0);
        bl->setSpacing(10);
        bl->addWidget(m_btnPano,  1);
        bl->addWidget(m_btnFront, 1);
        bl->addWidget(m_btnRear,  1);
        bl->addWidget(m_btnLeft,  1);
        bl->addWidget(m_btnRight, 1);
        hl->addLayout(bl, 1);

        hl->addSpacing(120);

        connect(m_btnPano,  &QPushButton::clicked, this, [this]{ switchView(ViewMode::Panorama); setFocus(Qt::OtherFocusReason); });
        connect(m_btnFront, &QPushButton::clicked, this, [this]{ switchView(ViewMode::Front);    setFocus(Qt::OtherFocusReason); });
        connect(m_btnRear,  &QPushButton::clicked, this, [this]{ switchView(ViewMode::Rear);     setFocus(Qt::OtherFocusReason); });
        connect(m_btnLeft,  &QPushButton::clicked, this, [this]{ switchView(ViewMode::Left);     setFocus(Qt::OtherFocusReason); });
        connect(m_btnRight, &QPushButton::clicked, this, [this]{ switchView(ViewMode::Right);    setFocus(Qt::OtherFocusReason); });
    }
    root->addWidget(m_navBar);

    updateNavButtons();
}

// ─────────────────────────────────────────────────────────────
void MainWindow::startCams()
{
    for (int i = 0; i < 4; i++) {
        m_cams[i] = new CameraThread(i, this);

        connect(m_cams[i], &CameraThread::errorOccurred,
                this, &MainWindow::onCamError, Qt::QueuedConnection);
        connect(m_cams[i], &CameraThread::reconnecting,
                this, &MainWindow::onCamReconnecting, Qt::QueuedConnection);
        connect(m_cams[i], &CameraThread::reconnected,
                this, &MainWindow::onCamReconnected, Qt::QueuedConnection);

        m_cams[i]->init();   // 探测设备（失败了 run() 内部会重连）
        m_cams[i]->start();
    }

    m_infer = new InferThread(m_model, this);
    m_infer->setCameras(m_cams);
    connect(m_infer, &InferThread::personDetected,
            this, &MainWindow::onPersonDetected, Qt::QueuedConnection);
    if (m_infer->init())
        m_infer->start();
}

// ─────────────────────────────────────────────────────────────
void MainWindow::onTimer()
{
    if (m_mode == ViewMode::Panorama) {
        for (int i = 0; i < 4; i++) {
            if (!m_cams[i]) continue;
            QImage frame; int fps = 0;
            if (m_cams[i]->takeLatestFrame(frame, fps))
                m_cells[i]->showFrame(frame, fps);
        }
    } else {
        int idx = camOf(m_mode);
        if (!m_cams[idx]) return;
        QImage frame; int fps = 0;
        if (m_cams[idx]->takeLatestFrame(frame, fps))
            m_singleCell->showFrame(frame, fps);
    }
}

// ─────────────────────────────────────────────────────────────
// 看门狗：每秒检查推理线程和各路摄像头是否还活着
void MainWindow::onWatchdogTick()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 检查推理线程心跳
    if (m_infer && m_infer->isRunning()) {
        qint64 lastInfer = m_infer->lastInferMs();
        if (lastInfer > 0 && (now - lastInfer) > INFER_TIMEOUT_MS) {
            qWarning("[watchdog] InferThread stalled for >%dms! Restarting...",
                     INFER_TIMEOUT_MS);
            m_infer->stop();
            m_infer->wait(3000);
            // 重新初始化推理线程（模型已加载，只需重启线程）
            m_infer->start();
            qDebug("[watchdog] InferThread restarted");
        }
    }

    // 检查摄像头采集线程心跳
    for (int i = 0; i < 4; i++) {
        if (!m_cams[i] || !m_cams[i]->isRunning()) continue;
        if (!m_cams[i]->isAlive(now, CAM_TIMEOUT_MS)) {
            qWarning("[watchdog] cam%d thread stalled! Restarting...", i);
            m_cams[i]->stop();
            m_cams[i]->wait(3000);
            m_cams[i]->start();
            qDebug("[watchdog] cam%d thread restarted", i);
        }
    }
}

// ─────────────────────────────────────────────────────────────
void MainWindow::onPersonDetected(int camIdx, bool hasPersons)
{
    if (camIdx < 0 || camIdx >= 4) return;

    if (hasPersons) {
        m_alertCount[camIdx] = ALERT_HOLD;
    } else {
        if (m_alertCount[camIdx] > 0)
            m_alertCount[camIdx]--;
    }

    updateNavButtons();
}

// ─────────────────────────────────────────────────────────────
void MainWindow::onCamError(int cam, QString msg)
{
    qWarning("[cam%d] %s", cam, qPrintable(msg));
    if (m_mode == ViewMode::Panorama && m_cells[cam])
        m_cells[cam]->showError();
    else if (cam == camOf(m_mode) && m_singleCell)
        m_singleCell->showError();
}

void MainWindow::onCamReconnecting(int cam)
{
    qDebug("[cam%d] reconnecting...", cam);
    if (m_mode == ViewMode::Panorama && m_cells[cam])
        m_cells[cam]->showReconnecting();
    else if (cam == camOf(m_mode) && m_singleCell)
        m_singleCell->showReconnecting();

    // 重连期间清掉该路报警，避免残影报警
    m_alertCount[cam] = 0;
    updateNavButtons();
}

void MainWindow::onCamReconnected(int cam)
{
    qDebug("[cam%d] reconnected OK", cam);
    // 恢复正常后 showFrame 会自然接管显示，不需要额外操作
}

// ─────────────────────────────────────────────────────────────
void MainWindow::switchView(ViewMode mode)
{
    m_mode = mode;
    if (mode == ViewMode::Panorama) {
        m_stack->setCurrentIndex(0);
    } else {
        int idx = camOf(mode);
        m_singleCell->setDirection(idx);
        m_stack->setCurrentIndex(1);
    }
    updateNavButtons();
    setFocus(Qt::OtherFocusReason);
}

void MainWindow::updateNavButtons()
{
    m_btnPano->setState(m_mode == ViewMode::Panorama, false);
    for (int i = 0; i < 4; i++) {
        bool active = (m_mode == modeOf(i));
        bool alert  = !active && (m_alertCount[i] > 0);
        m_camBtns[i]->setState(active, alert);
    }
}

// ─────────────────────────────────────────────────────────────
void MainWindow::keyPressEvent(QKeyEvent* e)
{
    switch (e->key()) {
    case Qt::Key_0:     switchView(ViewMode::Panorama); break;
    case Qt::Key_1:     switchView(ViewMode::Front);    break;
    case Qt::Key_2:     switchView(ViewMode::Rear);     break;
    case Qt::Key_3:     switchView(ViewMode::Left);     break;
    case Qt::Key_4:     switchView(ViewMode::Right);    break;
    case Qt::Key_Up:    switchView(ViewMode::Front);    break;
    case Qt::Key_Down:  switchView(ViewMode::Rear);     break;
    case Qt::Key_Left:  switchView(ViewMode::Left);     break;
    case Qt::Key_Right: switchView(ViewMode::Right);    break;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter: switchView(ViewMode::Panorama); break;
    case Qt::Key_Escape:
    case Qt::Key_Q:     qApp->quit();                   break;
    default: QWidget::keyPressEvent(e); return;
    }
}
