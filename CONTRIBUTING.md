# 贡献指南（Collaboration & Code Review）

本项目为课程小组协作项目，所有变更**必须**通过 Pull Request 合入 `main`。以下规则与 `.github/` 下的 PR 模板、CODEOWNERS、CI、分支保护共同构成完整的「PR + 代码审查」机制。

## 1. 分支模型

- `main`：唯一正式分支，受保护。禁止直接 push、禁止 force push。
- 功能分支命名（从最新 `main` 切出）：

| 前缀 | 用途 | 示例 |
| --- | --- | --- |
| `feat/` | 新功能 | `feat/question-upload` |
| `fix/` | 缺陷修复 | `fix/quota-overflow` |
| `refactor/` | 重构（不改变行为） | `refactor/http-server` |
| `docs/` | 文档 | `docs/api-design` |
| `chore/` | 构建/工具/依赖 | `chore/ci-cache` |

## 2. 提交信息规范（Conventional Commits）

```
<type>(<scope>): <subject>

type: feat | fix | refactor | docs | chore | test | perf
scope: server | web | ci | docs（可选）
```

示例：

- `feat(server): 增加题目上传接口`
- `fix(web): 修复分支树折叠状态丢失`
- `docs: 补充环境部署说明`

## 3. Pull Request 流程

1. 从最新 `main` 切分支：`git switch -c feat/xxx`
2. 小步提交，提交信息遵循上述规范
3. 推送分支并创建 PR：`git push -u origin feat/xxx`，然后到 GitHub 创建 PR
4. 按 [PR 模板](.github/PULL_REQUEST_TEMPLATE.md) 填写：变更说明、自查清单、测试计划
5. 等待 CI 通过 + 至少 1 人 Review；按 Review 意见修改后重新推送
6. 通过后由 **Reviewer（非作者）** 点击 **Squash and merge** 合并

## 4. 代码审查规则

**Reviewer 检查清单：**

- [ ] 需求/设计是否正确（对照 `docs/AI题库项目整体预期功能说明书.md`）
- [ ] 是否引入安全风险：API Key/密码泄露、越权访问、注入、**路径穿越**、内存安全问题（C 语言重点：缓冲区、指针、资源释放）
- [ ] 是否破坏多租户隔离（教师/学生数据边界）
- [ ] 测试是否覆盖关键路径；CI 是否全绿
- [ ] 命名、结构是否符合 `docs/architecture.md` 约定
- [ ] 是否引入不必要的大改动（PR 应小而聚焦）

**约定：**

- 每个 PR 至少 1 人 Approve 方可合并（分支保护强制）。
- 作者不合并自己的 PR。
- 修改意见超过 3 轮仍未收敛时，建议面对面讨论而不是在评论区拉锯。
- 合并方式统一使用 **Squash and merge**，保持 `main` 历史干净。

## 5. 代码所有权（CODEOWNERS）

`main` 分支保护开启了「要求 Code Owner 审查」。请在 `.github/CODEOWNERS` 中按模块填入真实 GitHub 账号，例如：

```
server/ @组员A
web/    @组员B
```

不同模块的变更会自动要求对应负责人审查。

## 6. 机密信息红线

- `.env`、API Key、数据库口令**一律不得**出现在任何提交、PR、Issue 中。
- 新增密钥类配置只能进 `server/.env.example`（占位符），真实值写入本地 `.env`（已被 gitignore）。
- 一旦发现密钥入库：立即**吊销**该密钥，并在 PR 中说明。

## 7. 本地验证（提交前必做）

```bash
make build          # 必须零警告编译通过（-Wall -Wextra）
make test           # 冒烟测试必须通过
make lint           # 语法 + 语义检查
```

## 8. 新增模块/语言的扩展流程

1. 创建新目录 `mymodule/`，在其中提供自己的 `Makefile`（目标：install/lint/test/build/run/clean）。
2. 根 `Makefile` 的 `MODULES` 变量中追加 `mymodule`。
3. C 模块还需在 `server/Makefile` 的 `SRCS` 中登记新源文件。
4. 在 CI workflow 中按需增加 job（参考现有 server job）。
5. 更新 `docs/architecture.md` 的目录说明与 README 技术栈表。
