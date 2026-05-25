#!/bin/bash
set -e

APP="rknn_yolov5_demo"
BUILD_DIR="${1:-build}"
VERSION="$(date +%Y%m%d-%H%M)"
RELEASE="release/${APP}-${VERSION}"

echo "=== Packaging ${APP} ${VERSION} ==="

# 目录结构
mkdir -p "${RELEASE}/opt/rknn-app/model"
mkdir -p "${RELEASE}/opt/rknn-app/lib"
mkdir -p "${RELEASE}/etc/systemd/system"
mkdir -p "${RELEASE}/etc/logrotate.d"

# 可执行文件
cp "${BUILD_DIR}/${APP}" "${RELEASE}/opt/rknn-app/"

# 配置文件（不带密码/密钥的 example）
cp config.ini.example "${RELEASE}/opt/rknn-app/"

# 模型文件
cp ../model/*.rknn            "${RELEASE}/opt/rknn-app/model/" 2>/dev/null || true
cp ../model/coco_80_labels_list.txt "${RELEASE}/opt/rknn-app/model/"

# RGA 库（如果是动态链接）
if ldd "${BUILD_DIR}/${APP}" 2>/dev/null | grep -q librga; then
    echo "  -> RGA library required but not bundled (static link recommended)"
fi

# systemd 服务
cp deploy/rknn-avs.service "${RELEASE}/etc/systemd/system/"

# logrotate 配置
cat > "${RELEASE}/etc/logrotate.d/rknn-avs" <<'ROTATE'
/var/log/rknn-avs.log {
    size 10M
    rotate 5
    compress
    copytruncate
}
ROTATE

# 安装脚本
cat > "${RELEASE}/install.sh" <<'INSTALL'
#!/bin/bash
set -e
echo "Installing RK3568 AI Video Surveillance..."

cp -r opt/rknn-app /opt/
cp etc/systemd/system/rknn-avs.service /etc/systemd/system/
cp etc/logrotate.d/rknn-avs /etc/logrotate.d/

chmod +x /opt/rknn-app/rknn_yolov5_demo

echo "=== 安装完成 ==="
echo "编辑配置文件:  vi /opt/rknn-app/config.ini"
echo "启用自启动:    systemctl enable --now rknn-avs"
echo "查看状态:      systemctl status rknn-avs"
echo "查看日志:      journalctl -u rknn-avs -f"
INSTALL
chmod +x "${RELEASE}/install.sh"

# 打包
tar czf "${RELEASE}.tar.gz" -C release "${APP}-${VERSION}"
echo "=== Done: ${RELEASE}.tar.gz ==="
