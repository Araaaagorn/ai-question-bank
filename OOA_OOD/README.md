# AI+题库智能教学平台 · 面向对象分析与设计文档（OOA/OOD）

> 课程阶段性设计文档。分析对象：`https://github.com/Araaaagorn/ai-question-bank`（main 分支，本地工作区 `ai-question-bank-fork`）。
> 全部结论经**代码走查 + 需求文档交叉验证**；无代码支撑的需求一律标注为目标功能（⬜）或部分实现（🟡），依据见 `04_设计依据与实现差异.md`。

## 阅读顺序

1. `01_系统概述.md` — 背景、目标、角色、边界、技术架构、功能实现状态总览
2. `02_OOA面向对象分析.md` — 用况图（3）＋用况说明、类图（3）、顺序图（5）、状态图（3）、包图（1）
3. `03_OOD面向对象设计.md` — 人机交互设计、控制驱动设计、数据管理设计（图 5）
4. `04_设计依据与实现差异.md` — 逐项设计依据/状态、需求-代码-设计不一致清单、新增建议、两轮自检记录

## 文档索引

| 文件 | 内容 |
| --- | --- |
| [01_系统概述.md](01_系统概述.md) | 系统概述与实现状态总览 |
| [02_OOA面向对象分析.md](02_OOA面向对象分析.md) | OOA 五类图与用况文字说明 |
| [03_OOD面向对象设计.md](03_OOD面向对象设计.md) | HCI / 控制驱动 / 数据管理 |
| [04_设计依据与实现差异.md](04_设计依据与实现差异.md) | 依据、差异、建议、自检 |

## 图形文件索引（PlantUML 源 + 渲染图）

所有图均提供 `.puml` 源文件（可编辑、可独立渲染）与 `.svg`（文档引用）/`.png`（位图备用）。

| 目录 | 源文件 | 图名 |
| --- | --- | --- |
| diagrams/usecase/ | uc1_auth_question.puml | 用况图1 认证与题库管理（已实现） |
| | uc2_ai_config.puml | 用况图2 AI 解答与 API 配置（已实现） |
| | uc3_target.puml | 用况图3 目标功能（未实现/部分实现） |
| diagrams/class/ | cls1_domain.puml | 类图1 用户与题库领域 |
| | cls2_ai.puml | 类图2 AI 解答、对话与额度领域 |
| | cls3_bce.puml | 类图3 边界-控制-实体总览 |
| diagrams/sequence/ | seq1_login.puml | 顺序图1 登录与身份认证 |
| | seq2_question_manage.puml | 顺序图2 教师创建与管理题目 |
| | seq3_ai_solve.puml | 顺序图3 学生提交题目获取 AI 解答 |
| | seq4_followup.puml | 顺序图4 追问形成多轮对话 |
| | seq5_teacher_api_config.puml | 顺序图5 教师配置 AI API |
| diagrams/state/ | st1_question.puml | 状态图1 题目生命周期 |
| | st2_ai_job.puml | 状态图2 AI 解答任务生命周期 |
| | st3_teacher_api.puml | 状态图3 教师 API 配置生命周期 |
| diagrams/package/ | pkg1_system.puml | 包图1 系统逻辑包与依赖 |
| diagrams/ood/ | ood1_navigation.puml | 导航结构图（HCI） |
| | ood2_page_flow.puml | 页面流转状态图（HCI） |
| | ood3_interaction_activity.puml | 典型交互活动图（HCI） |
| | ood4_control_arch.puml | 控制架构组件图（控制驱动） |
| | ood5_data_er.puml | 数据管理 ER 图（数据管理） |

## 渲染说明

- 源格式：PlantUML（UML 2.x 语法），UTF-8，中文命名、技术术语保留英文。
- 本次渲染方式：`tools/render_puml.py` 通过 kroki.io 公共渲染服务（POST 明文）批量生成 SVG+PNG；运行环境无 Java/本地 PlantUML 时的替代方案。
  ```bash
  cd OOA_OOD
  py tools/render_puml.py          # 全量渲染（需外网）
  py tools/render_puml.py seq3     # 仅渲染文件名含 seq3 的图
  ```
- 本地渲染（可选）：安装 Java 与 plantuml.jar 后 `java -jar plantuml.jar diagrams/**/*.puml`，或安装 PlantUML VS Code 插件直接预览 `.puml`。
- 渲染校验：脚本对每个输出检查文件头（`<svg` / PNG 魔数）与 "syntax error" 关键字；最近一次全量运行结果 **20/20 成功，failures: 0**（记录见 `04` 文档自检部分）。
- Markdown 中图片以相对路径引用同目录 `.svg`；`.png` 为同源位图备份，供不支持 SVG 的环境使用。

## 版本与口径

- 分析基线：仓库 main 分支当前内容（含 `docs/项目Backlog.md` 2026-11-09 核实口径）。
- 状态标记：✅ 已实现 ／ 🟡 部分实现 ／ ⬜ 目标功能 ／ 💡 新增设计建议。
- 重要已知差距（不掩饰）：前端未接入 v1.3 教师契约（默认 teacher 模式下对话页发送被禁用）；教师 API 配置与题库管理无前端页面；试卷/审核/导入导出/管理后台等仅有需求描述。详见 `04` 文档 §4.2。
