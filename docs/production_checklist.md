# 量产上线检查清单

## 一、代码层（必须改）

### 1.1 设备路径配置化

**现状**：`/dev/video0-3` 写死在 `camera.cc:39` 和 `camera.cc:320`

**风险**：不同批次板子的 MIPI 通道编号可能不同，USB 摄像头枚举顺序也不确定

**方案**：加一个 `config.ini`，格式：

```ini
[camera]
count = 4

[cam0]
device = /dev/video0
label  = 前
width  = 1920
height = 1080

[cam1]
device = /dev/video1
label  = 后
width  = 1920
height = 1080

[cam2]
device = /dev/video2
label  = 左
width  = 1920
height = 1080

[cam3]
device = /dev/video3
label  = 右
width  = 1920
height = 1080

[model]
path        = /opt/rknn-app/model/yolov5s_relu.rknn
label_file  = /opt/rknn-app/model/coco_80_labels_list.txt
conf_thresh = 0.35
nms_thresh  = 0.45

[display]
fps_limit = 30

[watchdog]
infer_timeout_ms = 8000
cam_timeout_ms   = 5000
```

Qt 里用 `QSettings` 读取 `.ini` 文件，启动时传入路径：`./rknn_yolov5_demo /opt/rknn-app/config.ini`

改动量：新建一个 `Config` 类，修改 `main.cc` 和 `CameraThread` 构造函数。

---

### 1.2 systemd 自启动

文件 `/etc/systemd/system/rknn-avs.service`：

```ini
[Unit]
Description=RK3568 AI Video Surveillance
After=multi-user.target

[Service]
Type=simple
ExecStart=/opt/rknn-app/rknn_yolov5_demo /opt/rknn-app/config.ini
WorkingDirectory=/opt/rknn-app
Restart=always
RestartSec=5
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

启用：

```bash
systemctl enable rknn-avs
systemctl start rknn-avs
systemctl status rknn-avs   # 看状态
journalctl -u rknn-avs -f   # 看日志
```

---

### 1.3 日志持久化

**现状**：`qDebug/qWarning` 默认打到 stderr，串口关了就丢了

**方案**：在 `main.cc` 里安装自定义 message handler：

```cpp
void logToFile(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    static QMutex mu;
    QMutexLocker lk(&mu);
    QFile f("/var/log/rknn-avs.log");
    if (f.open(QIODevice::Append | QIODevice::WriteOnly)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
           << " [" << type << "] " << msg << "\n";
    }
}

// main() 里：
qInstallMessageHandler(logToFile);
```

配合 logrotate `/etc/logrotate.d/rknn-avs`：

```
/var/log/rknn-avs.log {
    size 10M
    rotate 5
    compress
    copytruncate
}
```

---

## 二、部署层

### 2.1 目录结构

```
/opt/rknn-app/
├── rknn_yolov5_demo       # 可执行文件
├── config.ini              # 配置文件
├── model/
│   ├── yolov5s_relu.rknn   # 模型文件
│   └── coco_80_labels_list.txt
├── lib/
│   └── librga.so           # RGA 库（如需要）
└── log -> /var/log/rknn-avs.log
```

### 2.2 打包脚本

`pack.sh`：

```bash
#!/bin/bash
set -e

mkdir -p release/opt/rknn-app/{model,lib}
cp build/rknn_yolov5_demo     release/opt/rknn-app/
cp ../model/yolov5s_relu.rknn release/opt/rknn-app/model/
cp ../model/coco_80_labels_list.txt release/opt/rknn-app/model/
cp config.ini.example         release/opt/rknn-app/
cp rknn-avs.service           release/etc/systemd/system/
# RGA 库（如果动态链接）
cp /path/to/librga.so         release/opt/rknn-app/lib/

tar czf rknn-avs-$(date +%Y%m%d).tar.gz -C release .
echo "Done: rknn-avs-$(date +%Y%m%d).tar.gz"
```

---

## 三、硬件/生产层

### 3.1 每台设备标定

量产时需要每台设备烧录各自的标定参数（如果做测距）：

- `fx`, `fy`, `cx`, `cy` → 摄像头内参
- `cam_height_m` → 安装高度
- `pitch_deg` → 俯角

方案：产线工装固定距离（如 5m）放一个标准目标，自动计算焦距，写入设备。

### 3.2 老化测试

出厂前至少跑 24 小时：

```bash
# 监控脚本，每小时记录一次
while true; do
    echo "$(date) | FPS=$(tail -1 /var/log/rknn-avs.log | grep -oP '\d+fps') | Temp=$(cat /sys/class/thermal/thermal_zone0/temp)"
    sleep 3600
done
```

关注：
- 内存是否泄漏（`cat /proc/$(pidof rknn_yolov5_demo)/status | grep VmRSS`）
- NPU 温度是否过高
- 看门狗重启次数

### 3.3 出货前检查清单

- [ ] 4 路摄像头全部出图
- [ ] 每路有人经过时按钮变红报警
- [ ] 全景/单路切换正常
- [ ] 拔掉一路摄像头 → 显示错误 → 插回 → 自动恢复
- [ ] 断电重启 → systemd 自启动 → 恢复工作
- [ ] 连续运行 24h 无崩溃无内存泄漏
- [ ] 配置文件和模型文件烧录正确
- [ ] 序列号/设备标签贴好

---

## 四、明天继续的操作

```bash
# 1. 进入项目目录
cd /home/rrn/3568/rknn_model_zoo-1.6.0/examples/yolov5

# 2. 拉取最新代码
git pull origin master

# 3. 把改动同步到板子上编译
scp -r cpp/ root@<板子IP>:/opt/rknn-app/src/
ssh root@<板子IP>
cd /opt/rknn-app/src
mkdir -p build && cd build
cmake .. -DTARGET_SOC=rk3568
make -j4

# 4. 跟我说你要改哪个
#    - "加配置文件"   → P0，改代码量最大
#    - "加 systemd"   → 纯脚本，最快
#    - "加日志落盘"   → 小改动
#    - "加测距代码"   → 看文档直接写
```

明天打开终端后直接说「继续 RK3568 项目」，我会根据这个文档和当前代码状态接上。
