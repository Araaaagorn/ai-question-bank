# 项目架构与扩展指南（C 技术栈）

## 1. 总体架构

```
┌──────────┐   HTTP/JSON    ┌─────────────────────────────┐
│  web/     │ ─────────────▶ │ server（C11 后端）            │
│ 静态页面  │  /api/v1/*     │ libmicrohttpd + SQLite3      │
└──────────┘                │ + libcurl（AI 代理）          │
                             └─────────────┬───────────────┘
                                           │ 代理调用（API Key 不出后端）
                                   ┌───────┴───────┐
                                   │ 大模型供应商    │
                                   │ (OpenAI 协议)  │
                                   └───────────────┘
```

- `web/` 为纯静态页面（HTML/CSS/JS），由 C 服务端直接托管，**无 Node/npm 工具链**。
- `server/` 为唯一业务入口：认证鉴权、多租户隔离、题库/试卷/解答树、额度、AI 代理、审计。
- 所有大模型调用由后端统一代理，**API Key 只存在于服务端环境变量**。

## 2. 后端分层（server/）

| 目录/文件 | 职责 | 说明 |
| --- | --- | --- |
| `src/main.c` | 入口 | 加载配置 → 初始化数据库 → 启动 HTTP 服务 |
| `src/config.c` + `include/config.h` | 配置 | 环境变量读取（PORT/数据库/AI），默认值见 config.h |
| `src/db.c` + `include/db.h` | 数据库 | SQLite 初始化、建表（users/questions 骨架） |
| `src/http_server.c` + `include/http_server.h` | HTTP 服务 | libmicrohttpd 守护进程，信号优雅退出 |
| `src/routes.c` + `include/routes.h` | 路由 | /api/v1/* JSON API + 静态文件服务（防路径穿越） |
| `src/ai_client.c` + `include/ai_client.h` | AI 客户端 | libcurl 代理调用占位（功能开发阶段实现） |
| `third_party/cJSON.*` | JSON 库 | 单文件 MIT 协议，随仓库分发 |
| `tests/run_smoke.sh` | 冒烟测试 | 启动 → 健康检查 → 静态页 → 关闭 |

**约定：**
- 路由层只做分发与参数处理，业务规则放独立模块（后续按功能拆 `src/services_*.c`）；
- 头文件统一放 `include/`，源文件放 `src/`；
- 新增 C 源文件必须登记到 `server/Makefile` 的 `SRCS`。

## 3. 前端（web/）

| 目录 | 职责 |
| --- | --- |
| `index.html` | 页面骨架（后续按模块拆分页面） |
| `css/` | 样式 |
| `js/` | 脚本（fetch 调用后端 /api/v1/*） |

## 4. 新增模块 / 新语言（扩展方式）

本仓库是 **monorepo**，采用「一个模块一个目录，各自自带 Makefile」的模式。新增模块步骤：

1. 新建目录，例如 `worker/`（语言自选：Go / Rust / C++…）；
2. 在该目录提供 `Makefile`，实现统一目标：`install lint test build run clean`；
3. 根 `Makefile` 的 `MODULES := server` 追加 `worker`；
4. 在 `.github/workflows/ci.yml` 中按需增加 job（复制 server job 模板改语言即可）；
5. 更新本文件目录说明与 README 技术栈表。

根 Makefile 通过 `$(addprefix <目标>-,$(MODULES))` 自动为每个模块生成
`build-xxx` / `test-xxx` 等分派目标，新增模块**不需要**改动分派逻辑本身。

## 5. 关键目录约定

- `data/`、`server/data/`、`*.db`：运行时数据，已被 `.gitignore` 排除，不入库；
- `docs/`：需求、架构、环境文档；重大技术决策在此留档；
- `deploy/`：部署配置示例（systemd）；
- `scripts/`：仓库级运维脚本（环境检查/安装、GitHub 初始化）。

## 6. 安全边界（框架层面已内置的约束）

- `.env` 不入库，密钥只进 `.env.example` 占位；
- 后端统一代理 AI 调用，前端永远拿不到 API Key；
- 静态文件服务已做**路径穿越防护**（拒绝含 `..` 的路径）；
- 多租户隔离：所有业务查询必须带当前用户/所属老师上下文（后续在路由层统一鉴权）；
- C 代码审查重点：缓冲区边界、指针生命周期、资源释放（见 CONTRIBUTING.md）。
