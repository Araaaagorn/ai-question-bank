#!/usr/bin/env bash
# =============================================================================
# 课程服务器环境依赖检查（Linux）—— C 技术栈
# 用法：bash scripts/check_env.sh
# 说明：只读检查，不修改系统；缺失项会给出安装指引（scripts/install_env.sh）
# =============================================================================
set -u

missing=()

ok()   { printf "  \033[32m[OK]  \033[0m %-16s %s\n" "$1" "$2"; }
miss() { printf "  \033[31m[缺失]\033[0m %-14s %s\n" "$1" "$2"; missing+=("$1"); }

echo "== 课程服务器环境检查（C 技术栈）=="
echo ""

# ---- git ----
if command -v git >/dev/null 2>&1; then
  ok "git" "$(git --version)"
else
  miss "git" "未安装"
fi

# ---- gcc ----
if command -v gcc >/dev/null 2>&1; then
  ok "gcc" "$(gcc --version | head -1)"
else
  miss "gcc" "未安装（Ubuntu: sudo apt install build-essential）"
fi

# ---- make ----
if command -v make >/dev/null 2>&1; then
  ok "make" "$(make --version 2>/dev/null | head -1 | awk '{print $3}')"
else
  miss "make" "未安装"
fi

# ---- pkg-config ----
if command -v pkg-config >/dev/null 2>&1; then
  ok "pkg-config" "可用"
else
  miss "pkg-config" "未安装"
fi

# ---- 开发库（libmicrohttpd / libcurl / sqlite3）----
check_lib() { # 参数: 显示名 pkg-config名 头文件路径
  local name="$1" pc="$2" header="$3"
  if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists "$pc" 2>/dev/null; then
    ok "$name" "$(pkg-config --modversion "$pc")"
  elif [ -f "/usr/include/$header" ]; then
    ok "$name" "头文件已找到 ($header)"
  else
    miss "$name" "未安装开发库（Ubuntu: sudo apt install ${pc}-dev）"
  fi
}
check_lib "libmicrohttpd" "libmicrohttpd" "microhttpd.h"
check_lib "libcurl" "libcurl" "curl/curl.h"
check_lib "sqlite3" "sqlite3" "sqlite3.h"

echo ""
if [ "${#missing[@]}" -eq 0 ]; then
  echo "✔ 环境完整，可以直接执行：make build && make test"
  exit 0
else
  echo "✘ 缺失组件：${missing[*]}"
  echo "  可执行 sudo bash scripts/install_env.sh 安装"
  exit 1
fi
