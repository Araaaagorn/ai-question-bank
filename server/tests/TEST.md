# 测试指南

## 测试架构（三阶段）

`tests/run_tests.sh` 分三个阶段执行：

```
Phase 0 ─ 预编译
  ├─ make build（编译 server 二进制）
  ├─ 编译 C 单元测试二进制 → /tmp/aqb_test_bin_$$
  └─（每次新增 C 测试源文件时在 COMPILE_RULES 加一行即可）

Phase 1 ─ 独立单元测试（无需 server）
  └─ 匹配 case_unit*.sh → 运行不依赖 HTTP 服务的测试

Phase 2 ─ API 集成测试（启动 server）
  ├─ 启动 server（curl 健康检查等待就绪）
  ├─ 匹配 case_*.sh 但排除 case_unit* → 运行 API 测试
  └─ 停止 server、清理数据库
```

## 运行测试

```bash
# 从 server/ 目录
bash tests/run_tests.sh --all              # 全部测试
bash tests/run_tests.sh kv                 # 仅 kv 目录的测试（含 Phase 1 和 Phase 2）
bash tests/run_tests.sh auth health        # 仅 auth 和 health 目录

# 从项目根目录
make test                                   # 等价于 bash tests/run_tests.sh --all
```

## 新增测试

### 命名约定

| 文件位置 | 命名规则 | 归属阶段 | 说明 |
|---|---|---|---|
| `tests/xxx/case_unit*.sh` | `case_unit*.sh` | Phase 1 | 不依赖 server 的单元测试 |
| `tests/xxx/case_*.sh`（非 unit） | `case_*.sh` | Phase 2 | 需要 server 运行的 API 测试 |
| `tests/xxx/case_*.c` | `case_*.c` | Phase 0 + Phase 1 | C 单元测试源码，需在 COMPILE_RULES 注册 |

### 新增 Phase 1 单元测试（Shell）

在 `tests/` 下创建子目录和 `case_unit*.sh` 文件：

```bash
# tests/mymod/case_unit_basic.sh
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

# 直接在这里测试，无需 server
[ -f "some_file" ] || exit 1
echo "[PASS] 基本检查通过"
```

### 新增 Phase 1 单元测试（C）

1. 编写 `tests/xxx/case_unit_yyy.c`
2. 在 `tests/run_tests.sh` 的 `COMPILE_RULES` 中添加一行：

```bash
COMPILE_RULES=(
    "xxx_yyy|gcc -std=c11 -Iinclude -Ithird_party tests/xxx/case_unit_yyy.c src/xxx.c ... -lsqlite3 -lm -o \${AQB_TEST_BIN_DIR}/xxx_yyy"
)
```

3. 编写对应的 `tests/xxx/case_unit_yyy.sh`（引用预编译的二进制）：

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
BIN="${AQB_TEST_BIN_DIR:?}/xxx_yyy"
"$BIN"
```

### 新增 Phase 2 API 测试

在 `tests/xxx/` 下创建 `case_*.sh`，使用 curl 访问 server 的 HTTP 接口：

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."

PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "  [PASS] $1"; }
fail() { FAIL=$((FAIL+1)); echo "  [FAIL] $1"; }

PORT="${TEST_PORT:-8099}"

# 使用 --noproxy '*' 绕过系统代理
BODY=$(curl -s --noproxy '*' "http://127.0.0.1:$PORT/api/v1/health")
echo "$BODY" | grep -q '"status":"ok"' && ok "健康检查" || fail "健康检查失败"

[ "$FAIL" -eq 0 ] || exit 1
```

> **注意**：所有 curl 命令必须加 `--noproxy '*'`，否则系统代理可能将请求重定向到其他端口。

## 测试目录独立

每个测试目录完全独立，`run_tests.sh` 自动发现目录下所有 `case_*.sh`：

```
tests/
├── auth/          # 认证相关 API 测试
│   ├── case_1.sh
│   ├── case_2.sh
│   ├── case_3.sh
│   └── case_4.sh
├── health/        # 健康检查测试
│   └── case_1.sh
├── kv/            # kv_store 单元测试
│   ├── case_unit.c
│   └── case_unit.sh
└── run_tests.sh   # 测试运行器
```

## 调试

- 测试日志写入 `/tmp/aqb_test_server.log`
- 服务端口默认为 8099，可通过 `TEST_PORT` 环境变量覆盖
- 数据库使用独立文件 `data/test_all.db`，每次运行自动重建