#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QStackedWidget>
#include <array>
#include "camera.h"
#include "infer_thread.h"

enum class ViewMode { Panorama, Front, Rear, Left, Right };

// ── 视频显示格子 ──────────────────────────────────────────────
class CellWidget : public QLabel
{
    Q_OBJECT
public:
    explicit CellWidget(int camIdx, QWidget* parent = nullptr);
    void showFrame(const QImage& img, int fps);
    void showWaiting();
    void showError();
    void showReconnecting();   // 新增：显示重连中状态
    void setDirection(int camIdx);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    int     m_curIdx;
    QLabel* m_hud;
    QLabel* m_tag;
};

// ── 底部导航按钮（支持三种状态：普通/选中/报警）──────────────
class NavButton : public QPushButton
{
    Q_OBJECT
public:
    explicit NavButton(const QString& label, QWidget* parent = nullptr);
    void setState(bool active, bool alert);

private:
    QString m_label;
};

// ── 主窗口 ────────────────────────────────────────────────────
class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(const QString& model, QWidget* parent = nullptr);
    ~MainWindow();

protected:
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void onTimer();
    void onCamError(int cam, QString msg);
    void onCamReconnecting(int cam);
    void onCamReconnected(int cam);
    void onPersonDetected(int camIdx, bool hasPersons);
    void onWatchdogTick();     // 看门狗定时器槽

    void switchView(ViewMode mode);

private:
    void buildUI();
    void startCams();
    void updateNavButtons();
    static int camOf(ViewMode m);
    static ViewMode modeOf(int camIdx);

    QString   m_model;
    ViewMode  m_mode = ViewMode::Panorama;

    std::array<CameraThread*, 4> m_cams{};
    InferThread* m_infer = nullptr;

    QTimer* m_timer    = nullptr;
    QTimer* m_watchdog = nullptr;   // 独立看门狗定时器，1秒触发一次

    std::array<int, 4> m_alertCount{};
    static constexpr int ALERT_HOLD = 8;

    // 看门狗：推理线程连续多少秒没动就认为卡死
    static constexpr int INFER_TIMEOUT_MS  = 8000;
    // 看门狗：摄像头采集线程多久没帧就认为卡死
    static constexpr int CAM_TIMEOUT_MS    = 5000;

    QStackedWidget* m_stack       = nullptr;
    QWidget*        m_gridWidget  = nullptr;
    std::array<CellWidget*, 4> m_cells{};
    QWidget*        m_singleWidget = nullptr;
    CellWidget*     m_singleCell   = nullptr;

    QWidget*   m_navBar   = nullptr;
    NavButton* m_btnPano  = nullptr;
    NavButton* m_btnFront = nullptr;
    NavButton* m_btnRear  = nullptr;
    NavButton* m_btnLeft  = nullptr;
    NavButton* m_btnRight = nullptr;

    std::array<NavButton*, 4> m_camBtns{};
};
