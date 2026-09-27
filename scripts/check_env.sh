#!/usr/bin/env bash
# =============================================================================
# 课程服务器环境依赖检查（Linux）
# 用法：bash scripts/check_env.sh
# 说明：只读检查，不修改系统；缺失项会给出安装指引（scripts/install_env.sh）
# =============================================================================
set -u

missing=()

ok()   { printf "  \033[32m[OK]  \033[0m %-14s %s\n" "$1" "$2"; }
warn() { printf "  \033[33m[警告]\033[0m %-12s %s\n" "$1" "$2"; }
miss() { printf "  \033[31m[缺失]\033[0m %-12s %s\n" "$1" "$2"; missing+=("$1"); }

echo "== 课程服务器环境检查 =="
echo ""

# ---- git ----
if command -v git >/dev/null 2>&1; then
  ok "git" "$(git --version)"
else
  miss "git" "未安装"
fi

# ---- python3 >= 3.10 ----
if command -v python3 >/dev/null 2>&1; then
  pyver="$(python3 -c 'import sys; print(".".join(map(str, sys.version_info[:3])))' 2>/dev/null)"
  if python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)' 2>/dev/null; then
    ok "python3" "$pyver (满足 >= 3.10)"
  else
    miss "python3" "$pyver (版本过低，需 >= 3.10)"
  fi
else
  miss "python3" "未安装"
fi

# ---- python3-venv（创建虚拟环境必需）----
if python3 -c 'import venv' >/dev/null 2>&1; then
  ok "python3-venv" "可用"
else
  miss "python3-venv" "模块缺失，需安装（Ubuntu: sudo apt install python3-venv）"
fi

# ---- pip3 ----
if command -v pip3 >/dev/null 2>&1; then
  ok "pip3" "$(pip3 --version 2>/dev/null | awk '{print $2}')"
else
  miss "pip3" "未安装"
fi

# ---- node >= 18 ----
if command -v node >/dev/null 2>&1; then
  nver="$(node --version 2>/dev/null)"
  maj="${nver#v}"; maj="${maj%%.*}"
  if [ "$maj" -ge 18 ] 2>/dev/null; then
    ok "node" "$nver (满足 >= 18)"
  else
    miss "node" "$nver (版本过低，需 >= 18，建议用 NodeSource 安装 Node 20)"
  fi
else
  miss "node" "未安装"
fi

# ---- npm ----
if command -v npm >/dev/null 2>&1; then
  ok "npm" "$(npm --version 2>/dev/null)"
else
  miss "npm" "未安装"
fi

# ---- make ----
if command -v make >/dev/null 2>&1; then
  ok "make" "$(make --version 2>/dev/null | head -1 | awk '{print $3}')"
else
  miss "make" "未安装"
fi

echo ""
if [ "${#missing[@]}" -eq 0 ]; then
  echo "✔ 环境完整，可以直接执行：make setup"
  exit 0
else
  echo "✘ 缺失组件：${missing[*]}"
  echo "  可执行 bash scripts/install_env.sh 安装（需要 sudo，建议逐项确认）"
  exit 1
fi
