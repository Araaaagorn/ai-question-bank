#!/usr/bin/env bash
# =============================================================================
# 课程服务器系统级依赖安装（Linux，需要 sudo）
# 用法：sudo bash scripts/install_env.sh
# 覆盖：git / make / python3+venv+pip / node+npm（按发行版选择源）
#
# 注意：
#   - 本项目依赖请勿用系统 pip 直接装，统一走 backend 的 venv（make install）。
#   - Debian/Ubuntu 自带的 node 可能过旧，脚本会提示 NodeSource 安装方式。
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

install_apt() {
  apt-get update
  apt-get install -y git make curl ca-certificates \
    python3 python3-venv python3-pip \
    nodejs npm
}

install_dnf() {
  dnf install -y git make curl \
    python3 python3-pip \
    nodejs npm
}

install_yum() {
  yum install -y git make curl \
    python3 python3-pip \
    nodejs npm
}

case "$PKG_MGR" in
  apt) install_apt ;;
  dnf) install_dnf ;;
  yum) install_yum ;;
esac

echo ""
echo "✔ 系统依赖安装完成。"
echo "  提示：若 node 版本 < 18（用 bash scripts/check_env.sh 确认），"
echo "  建议通过 NodeSource 安装 Node 20："
echo "    curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -"
echo "    sudo apt-get install -y nodejs"
echo ""
echo "  接下来执行：make setup   （在仓库根目录，安装项目依赖）"
