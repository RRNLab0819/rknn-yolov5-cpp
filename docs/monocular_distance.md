# 单目摄像头行人测距方案

基于 RK3568 + YOLOv5 的安防系统，利用单目摄像头估算行人与摄像头的距离。

## 1. 原理

### 1.1 针孔相机模型

```
                    H_person (1.7m)
                    ╔══════╗
                    ║      ║
                    ║ 行人 ║
                    ║      ║
                    ╚══════╝
                    /│
                   / │
                  /  │
                 /   │
                /    │
    ──────────/─────┼────── 地面
             D      │
    ════════════════╪══════
            摄像头   │ H_cam
    ════════════════╪══════
```

### 1.2 核心公式

**相似三角形**：实物高度与像素高度的比例 = 距离与焦距的比例

```
D = (H_person * f_y) / h_bbox
```

| 符号 | 含义 | 典型值 |
|------|------|--------|
| D | 行人与摄像头的水平距离 (m) | — |
| H_person | 行人实际身高 (m) | 1.70 |
| f_y | 摄像头垂直焦距 (像素) | 标定获得 |
| h_bbox | 检测框高度 (像素) | YOLOv5 输出 |

### 1.3 为什么用框高度比框底部更准

框底部代表脚的位置，理论上更准确，但容易受遮挡和地面不平影响。

框高度 = 头顶到脚底 = 整个人 → 对遮挡鲁棒性更好。

**实际建议**：两个都算，取加权平均。

---

## 2. 标定摄像头

### 2.1 拍棋盘格

打印一张 9×7 的棋盘格（每个格子 25mm），贴在平整的硬板上：

```bash
# 从不同角度拍 20-30 张照片
ffmpeg -i /dev/video0 -frames:v 30 chessboard_%03d.jpg
```

### 2.2 用 OpenCV 计算内参

```python
import cv2
import numpy as np
import glob

# 棋盘格规格
pattern_size = (8, 6)   # 内角点数量 (9-1, 7-1)
square_size  = 0.025    # 每格 25mm

objp = np.zeros((pattern_size[0] * pattern_size[1], 3), np.float32)
objp[:, :2] = np.mgrid[0:pattern_size[0], 0:pattern_size[1]].T.reshape(-1, 2)
objp *= square_size

objpoints = []
imgpoints = []

images = sorted(glob.glob('chessboard_*.jpg'))
for fname in images:
    img = cv2.imread(fname)
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    ret, corners = cv2.findChessboardCorners(gray, pattern_size, None)
    if ret:
        objpoints.append(objp)
        imgpoints.append(corners)

ret, mtx, dist, rvecs, tvecs = cv2.calibrateCamera(
    objpoints, imgpoints, gray.shape[::-1], None, None)

print(f"内参矩阵:\n{mtx}")
print(f"fx = {mtx[0][0]:.2f}  fy = {mtx[1][1]:.2f}")
print(f"cx = {mtx[0][2]:.2f}  cy = {mtx[1][2]:.2f}")
print(f"畸变系数: {dist.ravel()}")

np.savez('camera_calib.npz', mtx=mtx, dist=dist)
```

**RK3568 常见 MIPI 摄像头标定结果参考**（无畸变镜头，6mm 焦距）：

| 参数 | 640×640 模型输入 | 1920×1080 原始 |
|------|-----------------|----------------|
| fx | ~550 | ~1650 |
| fy | ~550 | ~1650 |
| cx | 320 | 960 |
| cy | 320 | 540 |

**注意**：标定时用原始分辨率（1920x1080），模型推理时用 640×640。由于 RGA 做了等比缩放+letterbox，输入模型的图像和原始图像的坐标关系需要反算 letterbox 参数。

---

## 3. 代码实现

### 3.1 数据结构

```cpp
// 添加到 infer_thread.h 或新文件 distance.h

struct DistanceResult {
    int camIdx;          // 哪路摄像头
    float distanceM;     // 距离（米）
    int bboxHeight;      // 框高度（像素）
    int confidence;      // 置信度(%)
};

struct CameraParams {
    float fx;            // 焦距（像素，Y方向）
    float fy;
    float cx;            // 光心
    float cy;
    float camHeightM;    // 摄像头安装高度（米）
    float pitchDeg;      // 俯角（度），0=水平，正数=向下
};
```

### 3.2 核心测距函数

```cpp
// 方法1：框高度法（推荐，对遮挡鲁棒）
float estimateByHeight(int bboxHeightPx, const CameraParams& cam)
{
    constexpr float H_PERSON = 1.70f;   // 假设行人平均身高 1.7m
    return (H_PERSON * cam.fy) / bboxHeightPx;
}

// 方法2：地面接触点法（框底部y坐标）
// 需要知道摄像头安装高度和俯角
float estimateByFootPos(int yBottom, const CameraParams& cam)
{
    float dy = yBottom - cam.cy;
    float rad = cam.pitchDeg * M_PI / 180.0f;
    return cam.camHeightM / tanf(atanf(dy / cam.fy) + rad);
}

// 加权融合（经验权重）
float estimateDistance(int yTop, int yBottom, const CameraParams& cam)
{
    int bboxH = yBottom - yTop;
    float dH = estimateByHeight(bboxH, cam);     // 框高度法
    float dF = estimateByFootPos(yBottom, cam);   // 脚底法

    // 近处用脚底法更准，远处用高度法更准
    float w = std::min(1.0f, dH / 5.0f);   // 5米内权重向脚底法倾斜
    return w * dF + (1.0f - w) * dH;
}
```

### 3.3 集成到推理流程

在 `InferThread::inferOne()` 中，检测到人体后计算距离：

```cpp
void InferThread::inferOne(int idx, std::vector<uint8_t>& rgb)
{
    // ... 现有推理代码 ...

    for (int i = 0; i < res.count; i++) {
        const auto& r = res.results[i];
        if (r.cls_id != 0) continue;           // 只要人
        if (r.prop < CONF_THRESHOLD) continue;

        // ★ 计算距离
        int yTop    = (int)r.box.top;
        int yBottom = (int)r.box.bottom;
        float dist  = estimateDistance(yTop, yBottom, m_camParams[idx]);

        boxes.push_back({
            (int)r.box.left,  (int)r.box.top,
            (int)r.box.right, (int)r.box.bottom,
            (int)(r.prop * 100.f + 0.5f),
            dist              // ← 新增距离字段
        });
    }
    // ...
}
```

---

## 4. 实际注意事项

### 4.1 精度影响因素

| 因素 | 影响 | 缓解方法 |
|------|------|----------|
| 行人真实身高差异 | 1.5m vs 1.9m → 误差 20%+ | 用场景平均值校准 |
| 非平地（坡道） | 脚底法失效 | 只用高度法 |
| 遮挡（下半身被遮挡） | 框高度偏小 → 距离偏大 | 用脚底法做交叉验证 |
| 摄像头抖动 | 检测框不稳定 | 滑动窗口平均（5帧） |
| 广角镜头畸变 | 边缘检测框变形 | 先做畸变校正再测距 |

### 4.2 滑动平均滤波

```cpp
// 对距离做 5 帧平滑，去除跳变
class DistanceFilter {
    std::deque<float> m_history;
    static constexpr int WINDOW = 5;
public:
    float push(float raw) {
        m_history.push_back(raw);
        if (m_history.size() > WINDOW) m_history.pop_front();
        float sum = 0;
        for (auto v : m_history) sum += v;
        return sum / m_history.size();
    }
};
```

### 4.3 摄像头安装建议

```
        俯角 = 15°~25°
        ↓
    ┌───┴───┐
    │ 摄像头 │  安装高度 2.5m ~ 3.5m
    └───────┘
        │\
        │ \
        │  \
        │   \   监控范围：3m ~ 30m
        │    \
    ────┴─────┴──── 地面
    <--盲区-->
     ~2m
```

- **安装高度**：2.5m ~ 3.5m（太低盲区小但易被破坏，太高俯角大测距误差大）
- **俯角**：15° ~ 25°（确保 3-5m 处的人脚底可见）
- **避免**：逆光安装、镜头正对强光源

---

## 5. 快速验证

```bash
# 1. 站在 5 米处，记录检测框高度
# 2. 反算焦距：fy = (5 * bbox_height) / 1.7
# 3. 换到 10 米、15 米、20 米验证
# 4. 如果远距离偏小 → H_person 设大了；偏大 → H_person 设小了
```

误差 < 15%（3m~25m 范围）即可满足安防场景需求。
