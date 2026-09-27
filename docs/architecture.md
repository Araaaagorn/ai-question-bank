# 项目架构与扩展指南

## 1. 总体架构

```
┌──────────┐   HTTP/JSON    ┌─────────────────────┐
│  前端     │ ─────────────▶ │  后端 API            │
│ Vue3+Vite│  /api/v1/*     │  FastAPI (Uvicorn)   │
└──────────┘                └─────────┬───────────┘
                                     │ 代理调用（API Key 不出后端）
                             ┌───────┴───────┐
                             │ 大模型供应商    │
                             │ (OpenAI 协议)  │
                             └───────────────┘
```

- 前端（`frontend/`）通过 `/api` 反向代理访问后端（开发期由 Vite proxy 处理，生产期由 Nginx 或直接同域部署）。
- 后端（`backend/`）为唯一业务入口：认证鉴权、多租户隔离、题库/试卷/解答树、额度、AI 代理、审计。
- 所有大模型调用由后端统一代理，**API Key 只存在于服务端 `.env`**。

## 2. 后端分层（backend/app/）

| 目录 | 职责 | 说明 |
| --- | --- | --- |
| `api/` | 路由层（REST 入口） | 按业务域拆分 router，如 `api/v1/questions.py` |
| `core/` | 横切能力 | 安全（JWT/加密）、AI 客户端、额度、审计、重复检测 |
| `models/` | SQLAlchemy ORM 模型 | 对应说明书第 3 章实体：User/Question/Paper/AnswerNode… |
| `schemas/` | Pydantic 请求/响应模型 | API 出入参定义 |
| `services/` | 业务逻辑 | 审核、解答生成、分支 fork、导出等 |
| `db/` | 数据库会话与基类 | `session.py` / `base.py` |

**约定：**
- 路由只做参数校验与调用 service，不写业务逻辑；
- 跨模块能力放 `core/`，避免循环依赖；
- 模型字段命名与说明书数据模型章节保持一致。

## 3. 前端分层（frontend/src/）

| 目录 | 职责 |
| --- | --- |
| `views/` | 页面级组件（登录、题库、试卷、解答树…） |
| `components/` | 可复用组件 |
| `router/` | 路由与权限守卫 |
| `stores/` | 全局状态（Pinia） |

## 4. 新增模块 / 新语言（扩展方式）

本仓库是 **monorepo**，采用「一个模块一个目录，各自自带 Makefile」的模式。新增模块步骤：

1. 新建目录，例如 `worker/`（语言自选：Go / Rust / Python…）；
2. 在该目录提供 `Makefile`，实现统一目标：`install lint test build run clean`；
3. 根 `Makefile` 的 `MODULES := backend frontend` 追加 `worker`；
4. 在 `.github/workflows/ci.yml` 中按需增加 job（复制 backend job 模板改语言即可）；
5. 更新本文件目录说明与 README 技术栈表。

根 Makefile 通过 `$(addprefix <目标>-,$(MODULES))` 自动为每个模块生成
`install-xxx` / `test-xxx` 等分派目标，新增模块**不需要**改动分派逻辑本身。

## 5. 关键目录约定

- `data/`、`uploads/`、`*.db`：运行时数据，已被 `.gitignore` 排除，不入库；
- `docs/`：需求、架构、环境文档；重大技术决策在此留档；
- `deploy/`：部署配置示例（systemd）；
- `scripts/`：仓库级运维脚本（环境检查/安装、GitHub 初始化）。

## 6. 安全边界（框架层面已内置的约束）

- `.env` 不入库，密钥只进 `.env.example` 占位；
- 后端统一代理 AI 调用，前端永远拿不到 API Key；
- 多租户隔离：所有业务查询必须带当前用户/所属老师上下文（`core/` 提供鉴权依赖）；
- 审计：AI 调用、额度扣减、审核操作在 `core/audit.py` 统一埋点（P1）。
