# AI+题库智能教学平台

一个面向教育场景的 **多教师、多学生、多租户** 智能题库与试卷管理平台。教师维护题库/试卷并配置 AI 解答；学生提交题目、查看 AI 解答、在分支对话树中追问；管理员负责用户、全局 API、审计与运维。

功能预期详见 [`docs/AI题库项目整体预期功能说明书.md`](docs/AI题库项目整体预期功能说明书.md)。

## 技术栈

| 模块 | 技术 | 说明 |
| --- | --- | --- |
| backend | Python ≥ 3.10 · FastAPI · SQLAlchemy · Uvicorn | REST API，统一代理所有大模型调用，API Key 不出后端 |
| frontend | Node ≥ 18 · Vue 3 · Vite | Web 客户端 |
| 构建 | GNU Make | 根 `Makefile` 统一分派各模块 |
| 部署 | systemd + Uvicorn（课程服务器） | 见 `docs/environment-setup.md` |

## 目录结构

```
.
├── Makefile               # 根构建入口（模块化分派，支持新增语言/模块）
├── backend/               # Python/FastAPI 后端
│   ├── Makefile           # 后端构建规则（venv / lint / test / run）
│   ├── requirements*.txt  # 依赖清单
│   ├── app/               # 应用代码（api / core / models / schemas / services / db）
│   ├── tests/             # pytest 测试
│   └── scripts/           # 数据库初始化等
├── frontend/              # Vue3 + Vite 前端
│   ├── Makefile
│   ├── package.json
│   └── src/
├── docs/                  # 需求文档、架构说明、环境部署指南
├── scripts/               # 仓库级脚本：环境检查/安装、GitHub 初始化
├── deploy/                # 部署配置（systemd 服务示例）
└── .github/               # PR 模板、CODEOWNERS、CI workflow、分支保护
```

新增语言/功能时如何扩展：见 [`docs/architecture.md`](docs/architecture.md)。

## 快速开始

### 在课程服务器（Linux）上

```bash
# 1. 环境检查与系统依赖安装（python3>=3.10, node>=18, make, git）
bash scripts/check_env.sh          # 先检查，缺什么看提示
bash scripts/install_env.sh        # 按提示安装系统依赖（需 sudo，可只装缺的）

# 2. 安装项目依赖并运行测试
make setup                         # = 环境检查 + 安装 backend/frontend 全部依赖
make lint && make test             # 静态检查 + 测试
make -C backend run                # 启动后端 http://<服务器IP>:8000
make -C frontend run               # 启动前端 http://<服务器IP>:5173
```

### 本地 Windows 开发

```powershell
powershell -ExecutionPolicy Bypass -File scripts/check_env.ps1   # 检查环境
# 后端
cd backend
python -m venv .venv
.venv\Scripts\pip install -r requirements-dev.txt
.venv\Scripts\python -m uvicorn app.main:app --reload --port 8000
# 前端
cd frontend
npm install
npm run dev
```

### 配置

后端首次运行前复制 `backend/.env.example` 为 `backend/.env` 并填写（数据库、密钥、AI API 等）。`.env` 已被 `.gitignore` 排除，**严禁**提交。

## 协作规范

- **分支模型**：`main` 为受保护分支，禁止直接推送；所有改动走 feature 分支 + Pull Request。
- **代码审查**：每个 PR 至少 1 人 Review、CI 全绿后才可合并（分支保护已配置）。
- **提交信息**：Conventional Commits，如 `feat: 支持题目批量导入`。
- 详细规则见 [`CONTRIBUTING.md`](CONTRIBUTING.md) 与 [`docs/architecture.md`](docs/architecture.md)。

## License

MIT
