#!/bin/bash
# install.sh - Install vStuSystem as systemd service
set -e

INSTALL_DIR="/opt/vStuSystem"
SERVICE_NAME="vStuSystem"
SERVICE_PORT=18080
PKG_DIR="$(cd "$(dirname "$0")/.." && pwd)"

if [ "$(id -u)" -ne 0 ]; then
    echo "请使用sudo执行" >&2
    exit 1
fi

if [ -f "/etc/systemd/system/${SERVICE_NAME}.service" ]; then
    echo "服务已安装，请先执行uninstall.sh" >&2
    exit 1
fi

mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/webui" "$INSTALL_DIR/conf" "$INSTALL_DIR/data" "$INSTALL_DIR/logs" "$INSTALL_DIR/sbin"

cp "$PKG_DIR/bin/vStuSystem" "$INSTALL_DIR/bin/"
cp -r "$PKG_DIR/webui/"* "$INSTALL_DIR/webui/"
cp "$PKG_DIR/conf/register_student.conf" "$INSTALL_DIR/conf/"
cp "$PKG_DIR/sbin/admin_ctl.sh" "$INSTALL_DIR/sbin/"
cp "$PKG_DIR/sbin/uninstall.sh" "$INSTALL_DIR/sbin/"

chmod 755 "$INSTALL_DIR/bin/vStuSystem"
chmod 755 "$INSTALL_DIR/sbin/admin_ctl.sh" "$INSTALL_DIR/sbin/uninstall.sh"
chmod 644 "$INSTALL_DIR/conf/register_student.conf"
chmod 700 "$INSTALL_DIR/data" "$INSTALL_DIR/logs"

CONF_FILE="$INSTALL_DIR/conf/register_student.conf"
sed -i "s|^path = ./logs/register_student.log|path = $INSTALL_DIR/logs/register_student.log|" "$CONF_FILE"
sed -i "s|^path = ./data/register_student.db|path = $INSTALL_DIR/data/register_student.db|" "$CONF_FILE"
sed -i "s|^path = ./data/uploads|path = $INSTALL_DIR/data/uploads|" "$CONF_FILE"
sed -i "s|^path = ./data/operation_log.db|path = $INSTALL_DIR/data/operation_log.db|" "$CONF_FILE"

PORT_INFO=""
if command -v ss >/dev/null 2>&1; then
    PORT_INFO=$(ss -tlnp 2>/dev/null | grep ":${SERVICE_PORT} " || true)
elif command -v lsof >/dev/null 2>&1; then
    PORT_INFO=$(lsof -i :${SERVICE_PORT} -t 2>/dev/null || true)
fi
if [ -n "$PORT_INFO" ]; then
    echo "警告: 端口 ${SERVICE_PORT} 已被占用" >&2
    echo "$PORT_INFO" >&2
    echo "请修改 $CONF_FILE 中的 port 配置后重启服务" >&2
fi

cat > "/etc/systemd/system/${SERVICE_NAME}.service" <<EOF
[Unit]
Description=vStuSystem - Xingya Education System
After=network.target

[Service]
Type=simple
WorkingDirectory=${INSTALL_DIR}
ExecStart=${INSTALL_DIR}/bin/vStuSystem -c ${INSTALL_DIR}/conf/register_student.conf
Restart=on-failure
RestartSec=5
KillMode=mixed
TimeoutStopSec=30
StandardOutput=journal+console
StandardError=journal+console

[Install]
WantedBy=multi-user.target
EOF
chmod 644 "/etc/systemd/system/${SERVICE_NAME}.service"

systemctl daemon-reload
systemctl enable "${SERVICE_NAME}"
systemctl start "${SERVICE_NAME}"

if ! systemctl is-active --quiet "${SERVICE_NAME}"; then
    echo "服务启动失败，请查看日志:" >&2
    journalctl -u "${SERVICE_NAME}" -n 50 --no-pager >&2
    exit 1
fi

systemctl status "${SERVICE_NAME}" --no-pager || true
LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
if [ -z "$LOCAL_IP" ]; then
    LOCAL_IP="localhost"
fi
PROTOCOL="http"
if grep -q '^enabled = true' "$CONF_FILE" 2>/dev/null; then
    PROTOCOL="https"
fi
echo "安装完成，访问地址: ${PROTOCOL}://${LOCAL_IP}:${SERVICE_PORT}"
