#!/bin/bash
# package.sh - Build and package vStuSystem into tar.gz
set -e

VERSION="0.0.2"
PKG_NAME="vStuSystem-${VERSION}-linux-x86_64"
PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

if [ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]; then
    echo "请在项目根目录执行" >&2
    exit 1
fi

if [ ! -f "$PROJECT_ROOT/bin/vStuSystem" ]; then
    echo "二进制不存在，开始编译..."
    mkdir -p "$PROJECT_ROOT/build"
    cd "$PROJECT_ROOT/build"
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    if [ ! -f "$PROJECT_ROOT/bin/vStuSystem" ]; then
        echo "编译失败" >&2
        exit 1
    fi
fi

echo "strip 调试符号..."
strip "$PROJECT_ROOT/bin/vStuSystem"

TMP_PARENT="$(mktemp -d)"
TMP_DIR="${TMP_PARENT}/${PKG_NAME}"
mkdir -p "$TMP_DIR/bin" "$TMP_DIR/webui" "$TMP_DIR/conf" "$TMP_DIR/sbin"

cp "$PROJECT_ROOT/bin/vStuSystem" "$TMP_DIR/bin/"
cp -r "$PROJECT_ROOT/webui/"* "$TMP_DIR/webui/"
cp "$PROJECT_ROOT/conf/register_student.conf" "$TMP_DIR/conf/"
cp "$PROJECT_ROOT/sbin/admin_ctl.sh" "$TMP_DIR/sbin/"
cp "$PROJECT_ROOT/packaging/linux/install.sh" "$TMP_DIR/sbin/"
cp "$PROJECT_ROOT/packaging/linux/uninstall.sh" "$TMP_DIR/sbin/"

chmod 755 "$TMP_DIR/bin/vStuSystem" "$TMP_DIR/sbin/admin_ctl.sh" "$TMP_DIR/sbin/install.sh" "$TMP_DIR/sbin/uninstall.sh"

OUT_DIR="$PROJECT_ROOT/packaging/output"
mkdir -p "$OUT_DIR"
cd "$TMP_PARENT"
tar czf "$OUT_DIR/${PKG_NAME}.tar.gz" "${PKG_NAME}"

rm -rf "$TMP_PARENT"

echo "包路径: $OUT_DIR/${PKG_NAME}.tar.gz"
ls -lh "$OUT_DIR/${PKG_NAME}.tar.gz" | awk '{print "包大小: "$5}'
