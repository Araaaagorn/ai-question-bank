# AGENT.md — AI Agent 项目手册

> 维护者：戴儒骋、卢恒毅
> 用途：AI Agent 在本项目中生成/修改代码时的唯一权威参考。  
> 更新频率：项目结构、构建命令或编码规范变更时同步更新。

---

## 1. 项目概述

**AI+题库智能教学平台** 是一个面向教育场景的多教师、多学生、多租户智能题库与试卷管理系统。

- 教师维护题库/试卷并配置 AI 解答
- 学生提交题目、查看 AI 解答、在分支对话树中追问
- 管理员负责用户管理、全局 API、审计与运维

功能规格详见 [`AI题库项目整体预期功能说明书.md`](AI题库项目整体预期功能说明书.md)。

---

## 2. 技术栈

| 模块 | 技术 | 说明 |
|------|------|------|
| 后端 | **C11** · libmicrohttpd · SQLite3 · libcurl · cJSON | HTTP 服务 + 存储 + AI 代理（API Key 不出后端） |
| 前端 | 纯 HTML / CSS / JS | 静态页面，由 C 服务端直接托管，无构建工具链 |
| 构建 | GNU Make + gcc | `-std=c11 -Wall -Wextra`，零警告编译 |
| 部署 | systemd（Linux 服务器） | 见 `environment-setup.md` |
| 版本控制 | Git + GitHub | `main` 受保护，PR + CI + Code Review |

---

## 3. 目录结构

```
ai-question-bank/
├── Makefile                    # 根构建入口（模块化分派）
├── README.md / CONTRIBUTING.md / LICENSE
│
├── server/                     # C 后端（唯一业务入口）
│   ├── Makefile                # lint / build / test / run / clean
│   ├── include/                # 头文件
│   │   ├── ai_client.h         # AI 代理接口（libcurl）
│   │   ├── auth.h              # 认证鉴权接口
│   │   ├── config.h            # 环境变量配置
│   │   ├── db.h                # SQLite 数据库接口
│   │   ├── http_server.h       # HTTP 守护进程
│   │   └── routes.h            # 路由分发
│   ├── src/                    # 源码
│   │   ├── main.c              # 入口：加载配置 → 初始化 DB → 启动 HTTP
│   │   ├── config.c            # 环境变量读取（PORT/数据库/AI）
│   │   ├── db.c                # SQLite 初始化与建表
│   │   ├── http_server.c       # libmicrohttpd 守护进程
│   │   ├── routes.c            # /api/v1/* JSON API + 静态文件服务
│   │   ├── ai_client.c         # AI API 代理调用（libcurl）
│   │   └── auth.c              # 认证鉴权实现
│   ├── third_party/            # cJSON（MIT 协议，单文件库）
│   ├── tests/                  # 冒烟测试脚本
│   └── .env.example            # 环境变量配置示例
│
├── web/                        # 静态前端
│   ├── index.html
│   ├── css/style.css
│   └── js/app.js
│
├── docs/                       # 项目文档
│   ├── AGENT.md                # 本文件
│   ├── AI协作指南.md            # 团队协作流程
│   ├── AI题库项目整体预期功能说明书.md
│   ├── architecture.md         # 架构说明
│   ├── auth.md                 # 认证设计
│   └── environment-setup.md    # 环境部署指南
│
├── scripts/                    # 运维脚本
│   ├── check_env.sh / check_env.ps1
│   ├── install_env.sh
│   └── githooks/ (commit-msg / pre-commit)
│
├── deploy/systemd/             # 生产部署配置
└── .github/workflows/          # CI（每次 push/PR 自动 build + test）
```

---

## 4. 构建与运行

### 4.1 有 Linux 环境（课程服务器 / WSL2）

```bash
# 环境检查与依赖安装
bash scripts/check_env.sh
sudo bash scripts/install_env.sh   # gcc make libmicrohttpd-dev libcurl4-openssl-dev libsqlite3-dev

# 构建
make build          # 编译 server/
make test           # 冒烟测试（启动服务 → 健康检查 → 静态页 → 关闭）
make lint           # 静态检查

# 运行（开发模式）
make -C server run  # 启动 http://localhost:8000
curl http://localhost:8000/api/v1/health   # → {"status":"ok"}
```

### 4.2 Windows 本地开发

- **C 后端无法在 Windows 直接编译**（gcc 过旧），以 GitHub Actions CI 编译结果为准
- 前端可直接用浏览器打开 `web/index.html` 预览
- 可选方案：WSL2 + Ubuntu 获得本地编译能力

### 4.3 配置

所有配置通过**环境变量**传入（默认值见 `server/src/config.c`）：

| 变量 | 说明 | 默认值 |
|------|------|--------|
| `PORT` | 监听端口 | `8000` |
| `DATABASE_PATH` | SQLite 数据库路径 | `data/ai_question_bank.db` |
| `WEB_ROOT` | 静态文件根目录 | `../web`（相对 server/ 工作目录） |
| `AI_API_BASE_URL` | 大模型 API 地址 | `https://api.openai.com/v1` |
| `AI_API_KEY` | API Key | 空（必须填写） |
| `AI_MODEL` | 模型名称 | `gpt-4o-mini` |

生产部署：复制 `server/.env.example` → `server/.env`，systemd 通过 `EnvironmentFile` 加载。

---

## 5. 编码规范（C11）

### 5.1 编译约束（必须满足）

```
gcc -std=c11 -Wall -Wextra  → 零警告
```

- 所有源文件必须在 `server/Makefile` 的 `SRCS` 变量中登记
- 新增头文件放 `server/include/`，源文件放 `server/src/`
- 链接库：libmicrohttpd / libcurl / libsqlite3 / libpthread / libm

### 5.2 命名约定

| 类型 | 风格 | 示例 |
|------|------|------|
| 函数 | `snake_case` | `handle_question_upload()` |
| 结构体 | `snake_case` | `struct app_config` |
| 宏 / 常量 | `UPPER_SNAKE_CASE` | `MAX_PATH_LEN` |
| 文件名 | `snake_case` | `ai_client.c`, `http_server.h` |

### 5.3 内存管理（红线）

- **每次 `malloc` / `calloc` 必须有对应的 `free`**，禁止泄漏
- 禁止越界访问、悬垂指针（use-after-free / double-free）
- 函数返回指针时必须明确其生命周期（调用方释放 or 内部静态）
- 字符串操作优先 `snprintf` / `strncpy`，禁止裸 `sprintf` / `strcpy`

### 5.4 错误处理

- HTTP 错误统一返回 JSON：`{"error": "描述信息"}`
- HTTP 状态码语义必须正确：

| 场景 | 状态码 |
|------|--------|
| 未认证 | `401` |
| 无权限 | `403` |
| 资源不存在 | `404` |
| 参数错误 | `400` |
| 服务器内部错误 | `500` |
| 成功 | `200` / `201` |

- 每个返回值必须检查，stderr 打印有用诊断信息

---

## 6. 架构约束

### 6.1 分层原则

```
路由层（routes.c）  →  只做分发与参数处理
业务层（services_*.c）→ 业务规则
数据层（db.c）      → SQLite 读写
AI 层（ai_client.c）→ 大模型代理调用
```

- 路由层不直接操作数据库，通过 db.h/c 接口
- 业务规则放独立模块，不散落在路由处理函数中

### 6.2 安全红线

1. **API Key 永远不出后端**：所有 AI 调用由 server 代理，前端永远拿不到 Key
2. **`.env` 不入库**：已加入 `.gitignore`，敏感信息只能进 `.env.example`（占位符）
3. **路径穿越防护**：静态文件服务拒绝含 `..` 的路径
4. **多租户隔离**：所有业务查询必须带当前用户/所属教师上下文
5. **无硬编码密钥/密码**：敏感信息一律走环境变量
6. **SQL 注入防护**：使用参数化查询，禁止拼接 SQL 字符串

### 6.3 并发预留

- 保持「无共享可变状态」（no shared mutable state）
- 路由处理函数应为无副作用的纯函数，状态通过 db 持久化
- 考虑后续引入工作线程时的数据竞争风险

---

## 7. API 设计规范

### 7.1 URL 约定

```
/api/v1/health        → 健康检查（GET，无需认证）
/api/v1/auth/*         → 认证相关
/api/v1/questions/*    → 题库相关
/api/v1/exams/*        → 试卷相关
/api/v1/conversations/* → 对话树相关
/api/v1/admin/*        → 管理功能
```

### 7.2 请求/响应格式

- Content-Type: `application/json`
- 鉴权方式：Bearer Token（Authorization header）
- 所有 POST/PUT 请求体为 JSON
- 响应统一 JSON，错误格式 `{"error": "..."}`

### 7.3 API 文档

详见 `docs/api.md`（由戴儒骋维护），后端开发必须严格按 api.md 的接口签名实现。

---

## 8. 添加新功能的流程

### 8.1 C 后端新增源文件

1. 头文件放 `server/include/`
2. 源文件放 `server/src/`
3. 在 `server/Makefile` 的 `SRCS` 中追加新 `.c` 文件
4. 根 `Makefile` 无需修改（自动分派）

### 8.2 新增路由

1. 在 `server/src/routes.c` 的 URL 派遣函数中添加新路径匹配
2. 如需新业务模块，创建 `server/src/services_xxx.c`
3. 在 `server/tests/run_smoke.sh` 中添加冒烟测试用例
4. 更新 `docs/api.md` 对应接口定义

### 8.3 新增模块/语言（monorepo 扩展）

1. 创建新目录（如 `worker/`）
2. 提供自有 `Makefile`（实现 `install lint test build run clean` 目标）
3. 根 `Makefile` 的 `MODULES := server` 追加新目录名
4. `.github/workflows/ci.yml` 中按需增加 job
5. 更新 `docs/architecture.md` 和本文件

---

## 9. 测试

### 9.1 冒烟测试

- 脚本：`server/tests/run_smoke.sh`
- 覆盖：健康检查 → 静态页 → 登录 → 题目操作 → 对话 → 关闭
- CI 必须全绿，否则分支保护不允许合并

### 9.2 测试风格

```bash
#!/usr/bin/env bash
set -euo pipefail
BASE_URL="http://localhost:${PORT:-8000}"

# 1. 健康检查
HEALTH=$(curl -s "$BASE_URL/api/v1/health")
echo "$HEALTH" | grep -q '"status":"ok"' || { echo "FAIL: health"; exit 1; }
echo "PASS: health"

# ... 更多用例 ...
```

---

## 10. 常见坑与解决方案

| 问题 | 原因 | 解决 |
|------|------|------|
| 编译报 `undeclared` | 新源文件未登记 | 在 `server/Makefile` 的 `SRCS` 中追加 |
| `microhttpd.h` 找不到 | 缺少开发库 | `sudo apt install libmicrohttpd-dev` |
| 前端页面 404 | WEB_ROOT 路径不对 | 必须在 `server/` 目录下运行，WEB_ROOT 默认 `../web` |
| 服务器端口无法访问 | 防火墙/安全组 | 检查放行规则 |
| CI 检查名与分支保护不一致 | 配置不同步 | `gh pr checks <PR号>` 查看后更新 `scripts/branch_protection.json` |
| Windows 无法编译 | gcc 版本过旧 | 以 CI 编译结果为准；或装 WSL2 |
| `make test` 失败但服务正常 | 端口冲突/服务未完全启动 | 冒烟脚本中增加 `sleep 1` |

---

## 11. Git 工作流摘要

```
main（受保护） ← PR（Squash merge） ← feat/fix/docs/... 分支
```

- **禁止**直接 push main
- 分支命名：`feat/xxx` / `fix/xxx` / `refactor/xxx` / `docs/xxx` / `chore/xxx`
- 提交信息：Conventional Commits（`feat(server): 描述`）
- PR 至少 1 人 Approve + CI 全绿才可合并
- 作者不合并自己的 PR，统一 Squash and merge
- 详细规则见 `CONTRIBUTING.md`

---

## 12. 团队角色与模块归属

| 角色 | 成员 | 负责模块 |
|------|------|----------|
| 后端-认证 | 牛宇歌 | `auth.c/h`, 登录/注册/鉴权 |
| 后端-题库 | 李俊熠 | 题目 CRUD 路由, `db.c` 扩展 |
| 后端-AI | 刘英哲 | `ai_client.c/h`, 对话树 |
| 后端-安全/测试/架构 | 卢 | 代码审查、冒烟测试、CI、安全审计 |
| 前端 | 曹、王 | `web/` 全部 |
| 文档/接口 | 戴儒骋 | `AGENT.md`、`api.md`、功能说明书 |

---

> **AI Agent 使用提醒**：生成代码前，请完整阅读本文件 + `server/Makefile` + 相关头文件（`server/include/*.h`），确保输出满足 C11 零警告、内存安全、API 签名一致、安全红线不触碰。