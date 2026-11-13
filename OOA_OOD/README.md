# AI+题库智能教学平台 · 面向对象分析与设计文档（OOA/OOD）

> 课程阶段性设计文档。分析对象：`https://github.com/Araaaagorn/ai-question-bank`（main 分支，本地工作区 `ai-question-bank-fork`）。
> 全部结论经**代码走查 + 需求文档交叉验证**；无代码支撑的需求一律标注为目标功能（⬜）或部分实现（🟡），依据见 `04_设计依据与实现差异.md`。

## 阅读顺序

1. `01_文档总体说明.md` — 第 1 章：目的、应用范围、文档组成（含全部图的编号、名称、份数与文件位置）
2. `02_OOA面向对象分析.md` — 第 2 章：用况图文档（3 份）、类图文档（2 份）、顺序图文档（5 份）、状态图文档（3 份）、包图文档（1 份），含模板要求的全部描述表格
3. `03_OOD面向对象设计.md` — 第 3 章：设计类图文档（含问题域/人机交互/控制驱动/数据管理四部分设计说明）、设计顺序图文档、设计状态图文档、设计包图文档
4. `04_设计依据与实现差异.md` — 逐项设计依据/状态、需求-代码-设计不一致清单、新增建议、课程模板修订记录与两轮自检记录

## 文档索引

| 文件 | 内容 |
| --- | --- |
| [01_文档总体说明.md](01_文档总体说明.md) | 第 1 章：目的、应用范围、文档组成 |
| [02_OOA面向对象分析.md](02_OOA面向对象分析.md) | 第 2 章：五类 OOA 图 + 全部描述表格与用况说明 |
| [03_OOD面向对象设计.md](03_OOD面向对象设计.md) | 第 3 章：设计类图/顺序图/状态图/包图 + 四部分设计说明 |
| [04_设计依据与实现差异.md](04_设计依据与实现差异.md) | 依据、差异、建议、模板修订记录、自检 |
| [05_课程规范审查与修改报告.md](05_课程规范审查与修改报告.md) | 按六份课件与课程模板的逐图审查结论、修改清单、渲染与验证结果、尚待确认事项 |

## 图形文件索引（PlantUML 源 + 渲染图）

所有图均提供 `.puml` 源文件（可编辑、可独立渲染）与 `.svg`（文档引用）/`.png`（位图备用）。编号与文档内图号一致。

| 图号 | 目录 / 源文件 | 图名 |
| --- | --- | --- |
| 图2-1 | diagrams/usecase/uc1_auth_question.puml | 用况图1 认证与题库管理（已实现） |
| 图2-2 | diagrams/usecase/uc2_ai_config.puml | 用况图2 AI 解答与 API 配置（已实现） |
| 图2-3 | diagrams/usecase/uc3_target.puml | 用况图3 目标功能（未实现/部分实现） |
| 图2-4 | diagrams/class/cls1_domain.puml | 类图1 用户与题库领域 |
| 图2-5 | diagrams/class/cls2_ai.puml | 类图2 AI 解答、对话与额度领域 |
| 图2-6 | diagrams/sequence/seq1_login.puml | 顺序图1 登录与身份认证 |
| 图2-7 | diagrams/sequence/seq2_question_manage.puml | 顺序图2 教师创建与管理题目 |
| 图2-8 | diagrams/sequence/seq3_ai_solve.puml | 顺序图3 学生提交题目获取 AI 解答 |
| 图2-9 | diagrams/sequence/seq4_followup.puml | 顺序图4 追问形成多轮对话 |
| 图2-10 | diagrams/sequence/seq5_teacher_api_config.puml | 顺序图5 教师配置 AI API |
| 图2-11 | diagrams/state/st1_question.puml | 状态图1 题目生命周期 |
| 图2-12 | diagrams/state/st2_ai_job.puml | 状态图2 AI 解答任务生命周期 |
| 图2-13 | diagrams/state/st3_teacher_api.puml | 状态图3 教师 API 配置生命周期 |
| 图2-14 | diagrams/package/pkg1_system.puml | 包图1 OOA 分析模型的包组织 |
| 图3-1 | diagrams/class/cls3_bce.puml | OOD 设计类图（四部分设计组织） |
| 图3-2 | diagrams/ood/ood1_navigation.puml | 导航结构图（人机交互设计·命令组织） |
| 图3-3 | diagrams/ood/ood2_page_flow.puml | 前端应用页面状态机（人机交互设计） |
| 图3-4 | diagrams/ood/ood3_interaction_activity.puml | 典型交互活动图（人机交互设计） |
| 图3-5 | diagrams/ood/ood4_control_arch.puml | 控制流与主动对象（控制驱动设计） |
| 图3-6 | diagrams/ood/ood5_data_er.puml | 永久类 ↔ 表映射与逻辑结构（数据管理设计） |
| 图3-7 | diagrams/ood/ood6_design_sequence.puml | 设计顺序图：AI 解答提交的控制与数据协作 |
| 图3-8 | diagrams/ood/ood7_conversation_state.puml | 设计状态图：对话（Conversation）生命周期 |
| 图3-9 | diagrams/ood/ood8_design_packages.puml | 设计包图：设计模型的包组织（四部分） |
| 图3-10 | diagrams/ood/ood9_login_design_sequence.puml | 设计顺序图：登录与身份认证 |
| 图3-11 | diagrams/ood/ood10_question_design_sequence.puml | 设计顺序图：教师题目管理 |
| 图3-12 | diagrams/ood/ood11_teacher_api_design_sequence.puml | 设计顺序图：教师配置本人 AI 接口 |

## 渲染说明

- 源格式：PlantUML（UML 2.x 语法），UTF-8，类名中英双重命名、属性名沿用代码字段名、操作名用问题域术语。
- 渲染方式：`tools/render_puml.py` 批量生成 SVG+PNG，**优先走 www.plantuml.com 官方服务**（GET + PlantUML 压缩编码），失败时**回退 kroki.io**（POST 明文）。两条通道都需要外网；运行环境无 Java/本地 PlantUML 时以此替代。
  > 说明：kroki.io 的 SVG 稳定且快，但其 PNG 后端对大图经常握手超时；plantuml.com 的 PNG 快，
  > 但对超长图（编码后 URL 超过服务端上限）会返回 HTTP 400。脚本据此做了通道分工与回退，
  > 并把“URL 过长的 400”与“真正的语法错误”区分开（后者不重试、直接报错）。
  ```bash
  cd OOA_OOD
  py -u tools/render_puml.py                # 全量渲染 SVG+PNG（需外网）
  py -u tools/render_puml.py --svg-only     # 只渲染 SVG（快速语法校验）
  py -u tools/render_puml.py seq3           # 仅渲染文件名含 seq3 的图
  py -u tools/gen_msg_tables.py seq1_login  # 从 .puml 导出模板要求的对象表与消息表
  py -u tools/check_refs.py                 # 校验 Markdown 引用与渲染产物完整性
  ```
- 本地渲染（可选）：安装 Java 与 plantuml.jar 后 `java -jar plantuml.jar diagrams/**/*.puml`，或安装 PlantUML VS Code 插件直接预览 `.puml`。
- 渲染校验：脚本对每个输出检查文件头（`<svg` / PNG 魔数）与 "syntax error" 关键字，并输出 `svg: n/N ok`、`png: n/N ok` 与失败明细；最近一次全量运行结果见 `04` §4.4 自检记录。
- **图 ↔ 表一致性**：《02》§2.3.2/§2.3.3 与《03》§3.2.2/§3.2.3 的对象表与消息表由 `tools/gen_msg_tables.py` 从 `.puml` 源文件机械导出，不是手工誊写；重绘顺序图后重跑该脚本即可同步。
- Markdown 中图片以相对路径引用同目录 `.svg`；`.png` 为同源位图备份，供不支持 SVG 的环境使用。

## 版本与口径

- 分析基线：仓库 main 分支当前内容；需求与功能状态以 `docs/软件需求说明书.md`（用况概览 UC-01~UC-10、F-01~F-17）、`docs/AI题库项目整体预期功能说明书.md`、`docs/项目开发时间轴.md` §3 功能清单（F01~F19、I01~I07）为准。
  > ⚠ 勘误：本文档组此前多处引用的 `docs/项目Backlog.md` **在仓库中不存在**；
  > 与其 F 编号一致的清单实际位于 `docs/项目开发时间轴.md` §3，已全部改引该文件。
- 课程基线：六份课件（第3讲 软件需求、第10讲 OOA、第11讲 顺序图通讯图活动图、第12讲 状态图、第13讲 OOD-1、第14讲 OOD-2）与课程模板《5、面向对象分析设计文档.docx》；逐图审查与修订记录见 `04` §4.5~§4.6。
- 模板基线：《面向对象分析设计文档》第 1~3 章结构（2026-11-11 首次对齐，本轮按课件复核修订）。
- 状态标记：✅ 已实现 ／ 🟡 部分实现 ／ ⬜ 目标功能（含“设计抽象”“建议设计”）／ 💡 新增设计建议。
- 重要已知差距（不掩饰）：**题库管理仍无前端页面**（题目写操作当前经 HTTP 接口工具调用）；题目新增的 8 次键值写入非单一事务；试卷/审核/导入导出/管理后台/个人中心等仅有需求描述；审计表只写不读。详见 `04` §4.2。
  > ✅ 已消除的历史差距（上游 #24/#25 已实现，文档已同步）：前端 `aiAccess()` 已按 v1.3 契约携带 `question_id`，并按 `reason_code` 细分横幅文案、渲染配额（剩余次数与重置时刻）与 429 处置；教师 AI API 配置页已上线（`#teacher-api`，`web/js/teacher-api.js` 的 `TeacherApiPage`，导航栏「AI 配置」入口仅对 teacher 显示）。
