#!/usr/bin/env bash
# =============================================================================
# 课程服务器系统级依赖安装（Linux，需要 sudo）—— C 技术栈
# 用法：sudo bash scripts/install_env.sh
# 覆盖：git / gcc / make / pkg-config / libmicrohttpd / libcurl / sqlite3
# =============================================================================
set -euo pipefail

echo "== 检测发行版 =="
if command -v apt-get >/dev/null 2>&1; then
  PKG_MGR="apt"
elif command -v dnf >/dev/null 2>&1; then
  PKG_MGR="dnf"
elif command -v yum >/dev/null 2>&1; then
  PKG_MGR="yum"
else
  echo "✘ 未能识别包管理器（apt/dnf/yum），请手动安装依赖。" >&2
  exit 1
fi
echo "  使用包管理器: $PKG_MGR"
echo ""

case "$PKG_MGR" in
  apt)
    apt-get update
    apt-get install -y git make gcc pkg-config \
      libmicrohttpd-dev libcurl4-openssl-dev libsqlite3-dev
    ;;
  dnf)
    dnf install -y git make gcc pkgconf-pkg-config \
      libmicrohttpd-devel libcurl-devel sqlite-devel
    ;;
  yum)
    yum install -y git make gcc pkgconfig \
      libmicrohttpd-devel libcurl-devel sqlite-devel
    ;;
esac

echo ""
echo "✔ 系统依赖安装完成。接下来：make build && make test"
