#!/bin/bash
# uninstall.sh - Uninstall vStuSystem systemd service
SERVICE_NAME="vStuSystem"
INSTALL_DIR="/opt/vStuSystem"

if [ "$(id -u)" -ne 0 ]; then
    echo "请使用sudo执行" >&2
    exit 1
fi

systemctl stop "${SERVICE_NAME}" 2>/dev/null || true
systemctl disable "${SERVICE_NAME}" 2>/dev/null || true
rm -f "/etc/systemd/system/${SERVICE_NAME}.service"
systemctl daemon-reload

if [ -d "$INSTALL_DIR" ]; then
    read -p "是否删除安装目录 ${INSTALL_DIR}（含数据）？[y/N]: " CONFIRM
    if [ "$CONFIRM" = "y" ] || [ "$CONFIRM" = "Y" ]; then
        rm -rf "$INSTALL_DIR"
        echo "已删除 $INSTALL_DIR"
    else
        echo "保留 $INSTALL_DIR"
    fi
fi

echo "卸载完成"
