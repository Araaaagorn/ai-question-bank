# AI+题库智能教学平台 — AI 辅助开发计划

> 版本：v1.0 | 日期：2026-10-10 | 维护：戴儒骋、卢恒毅

---

## 一、概述

### 1.1 背景

本项目（C11 + libmicrohttpd + SQLite3）技术栈对多数团队成员较陌生，开发效率面临挑战。通过系统化使用 AI 辅助编码，可大幅降低学习曲线、提高代码质量与一致性。

### 1.2 目标

- 用 AI 辅助完成 80% 以上的初始代码生成
- 确保 AI 生成代码符合 C11 零警告、内存安全、API 统一等规范
- CI 自动验证 AI 输出质量，人工审查仅聚焦架构与安全
- 7 人团队中每人掌握 AI 辅助开发流程

---

## 二、AI 辅助开发工作流

### 2.1 总体流程

```
需求分析 → 喂给 AI → 生成代码 → AI 自查 → 本地 review → commit + push → CI 验证 → PR Review → 合并
```

### 2.2 各阶段 AI 角色

| 阶段 | AI 做什么 | 人做什么 |
|------|----------|----------|
| 需求理解 | 解读 api.md 接口定义 | 确认需求正确性 |
| 代码生成 | 根据 AGENT.md + 头文件 + Makefile 生成 C 代码 | 粘贴 Prompt 模板 |
| AI 自查 | 逐条对照编码规范自检 | 确认自查通过 |
| 本地 review | — | 阅代码逻辑、安全风险 |
| CI 验证 | GitHub Actions 自动 build+test | 看 CI 结果，红了修 |
| PR Review | — | 代码审查（架构/安全/规范） |
| 测试 | 生成冒烟测试用例 | 确认覆盖场景 |

### 2.3 喂给 AI 的文件清单

#### 通用必喂（所有人）

| 文件 | 作用 |
|------|------|
| `docs/AGENT.md` | 项目手册：结构/构建/规范/模块/常见坑 |
| `CONTRIBUTING.md` | 分支命名、commit 规范、PR 流程 |
| 根 `Makefile` + `server/Makefile` | 编译约束：`-std=c11 -Wall -Wextra`、链接库 |
| 相关现有源码 1–2 个文件 | 风格模板，如 `server/src/routes.c` |

#### 按角色追加

| 角色 | 追加喂入 |
|------|----------|
| 后端（牛/李/刘） | `docs/api.md` 接口定义、`docs/architecture.md`、`server/include/*.h` 头文件 |
| 卢（安全/测试） | `.gitignore`、`server/.env.example`、现有冒烟测试 |
| 前端（曹/王） | `web/index.html`、`web/css/style.css`、`web/js/app.js`、`docs/api.md` |
| 文档（戴） | `AGENT.md` 现有内容、功能说明书 |

---

## 三、AI Prompt 模板

### 3.1 后端代码生成模板

将以下约束块粘贴在任务描述前：

```
你是本项目的 C 技术栈开发者。项目约束见附带的 AGENT.md、Makefile 与相关源码。
请完成：<具体任务>

输出必须满足：
1. C11 标准，gcc -Wall -Wextra 编译零警告（注意 include 路径与 LDLIBS 匹配 server/Makefile）；
2. HTTP 错误统一返回 JSON {"error":"..."}，状态码语义正确（401/403/404/500）；
3. 动态内存必须 malloc/free 配对，禁止泄漏、越界、悬垂指针；
4. 禁止硬编码 API Key / 密码，敏感信息一律走环境变量（参考 server/.env.example）；
5. 路由与数据结构签名与现有头文件（server/include/*.h）保持一致；
6. 不破坏现有功能，新增路由不影响既有 /api/v1/health 与静态文件服务；
7. 完成后，给出需要在 server/tests/ 中补充的冒烟测试用例。
```

### 3.2 前端代码生成模板

```
你是本项目的前端开发者。项目使用纯 HTML/CSS/JS，无构建工具链。
请完成：<具体任务>

输出必须满足：
1. 不引入 npm/webpack/vite 等工具链，直接修改 web/ 下静态文件；
2. fetch 调用后端 /api/v1/* 接口，携带 Authorization: Bearer <token>；
3. 错误处理：后端返回 {"error":"..."} 时展示用户可读提示；
4. 与 docs/api.md 定义的接口字段完全一致；
5. 保持与现有 style.css / app.js 风格一致。
```

### 3.3 文档生成模板

```
你是本项目的文档维护者。项目背景见附带的 AGENT.md 与功能说明书。
请完成：<具体任务>

输出必须满足：
1. Markdown 格式，与现有 docs/ 风格一致；
2. 引用的文件路径使用相对路径（如 docs/architecture.md）；
3. 事实信息以 AGENT.md 和功能说明书为准，不编造。
```

---

## 四、各角色 AI 使用指南

### 4.1 牛宇歌 — 后端认证模块

```
任务示例：实现用户注册接口 POST /api/v1/auth/register

输入：
- docs/AGENT.md（项目规范）
- server/Makefile（编译约束）
- server/include/auth.h + routes.h（函数签名）
- docs/api.md（接口定义）
- server/src/auth.c（现有风格参考）
- server/src/routes.c（路由注册方式）

Prompt：
[粘贴约束块] → 请实现 handle_register()，
接收 {"username","password","name","role":"student"}，
校验字段完整性 → SHA-256 哈希 → 插入 users 表 → 返回 201 和用户信息。
```

### 4.2 李俊熠 — 后端题库模块

```
任务示例：实现题目列表接口 GET /api/v1/questions

输入：AGENT.md + server/Makefile + server/include/db.h + routes.h + docs/api.md

注意：
- 多租户隔离：所有查询以当前登录用户所属教师为 scope
- 支持分页：?page=1&limit=20
- 支持筛选：?type=单选&difficulty=3&tag=数学
```

### 4.3 刘英哲 — 后端 AI 模块

```
任务示例：实现 AI 解答调用 ai_client.c

输入：AGENT.md + server/Makefile + server/include/ai_client.h + config.h

安全约束：
- API Key 从环境变量 AI_API_KEY 读取，绝对不硬编码
- libcurl 请求构造重试机制（3 次）
- 超时 30s 返回 {"error":"AI 服务不可用"}
```

### 4.4 卢恒毅 — 安全/测试/架构

```
任务示例：新增 API 冒烟测试 case

输入：AGENT.md + server/tests/run_tests.sh + 现有 case_*.sh

约束：
- 新 case 自给自足（自动登录获取 token）
- curl 加 --noproxy '*' 绕过系统代理
- 使用 sed 而非 grep/cut 解析 token
```

### 4.5 曹、王 — 前端

```
任务示例：实现题库列表页

输入：web/index.html + web/css/style.css + web/js/app.js + docs/api.md

约束：
- 纯静态页，fetch 调后端
- Bearer Token 存储在 localStorage
- 未登录重定向到登录页
```

---

## 五、AI 输出验证机制

### 5.1 自动化验证（CI）

| 检查项 | 工具 | 阈值 |
|--------|------|------|
| 编译 | `make build` | 零警告通过 |
| 冒烟测试 | `make test` | 全部 case 通过 |
| 代码行数 | git diff | PR < 500 行（推荐） |

### 5.2 AI 自查清单

代码生成后，AI 必须逐条回答：

1. ☐ `gcc -std=c11 -Wall -Wextra` 是否零警告？
2. ☐ 所有 `malloc` 是否有对应 `free`？
3. ☐ HTTP 错误是否返回 JSON `{"error":"..."}`？
4. ☐ 状态码是否语义正确（401/403/404/500）？
5. ☐ 是否有硬编码的密钥/密码？
6. ☐ 新增源文件是否登记到 `server/Makefile` 的 SRCS？
7. ☐ 新增路由是否不影响 `/api/v1/health`？
8. ☐ 是否给出了补充冒烟测试用例？

### 5.3 人工验证（PR Review）

- 对照 AGENT.md 第 5–6 章逐条审查
- 特别关注：缓冲区边界、指针生命周期、资源释放、多租户隔离

---

## 六、效率提升预估

| 场景 | 无 AI | 有 AI | 提升 |
|------|-------|-------|------|
| 新接口开发（C 后端） | 4–6 小时 | 1–2 小时 | 60–70% |
| 前端页面开发 | 3–4 小时 | 0.5–1 小时 | 70–80% |
| 冒烟测试编写 | 1–2 小时 | 15–30 分钟 | 75% |
| Bug 修复 | 1–2 小时 | 20–40 分钟 | 60% |
| 文档编写 | 2–3 小时 | 30–60 分钟 | 70% |

---

## 七、注意事项与红线

### 7.1 安全红线（AI 也不能违反）

1. **绝对不把 API Key 写入代码**：哪怕 AI 建议了也要拒绝
2. **`.env` 永远不入库**：AI 生成的配置示例只能进 `.env.example`
3. **密码必须哈希**：AI 生成代码若用明文存储密码，直接打回

### 7.2 常见陷阱

| 陷阱 | 应对 |
|------|------|
| AI 生成代码看起来对但编译不过 | 以 CI `make build` 结果为准 |
| AI 说不依赖某个库但实际需要 | 核对 `server/Makefile` 的 LDLIBS |
| AI 生成了 Windows-only 代码 | C11 跨平台，CI 会暴露 |
| AI 生成了不存在的函数 | 对照 `server/include/*.h` 头文件检查 |

### 7.3 质量底线

- AI 生成的代码 **必须** 经过 CI 验证
- 不要只看 AI 说"没问题"就合入
- 如果 AI 自查和 CI 结果矛盾，以 CI 为准

---

## 八、总结

本计划确保 7 人团队在 C 技术栈项目中系统化使用 AI 辅助开发。核心原则：

> **喂对文件 → 约束到位 → AI 自查 → CI 兜底 → 人工审安全**

通过 AGENT.md 统一规范、Prompt 模板标准化、CI 自动验证三层保障，实现 AI 辅助下的高质量快速开发。