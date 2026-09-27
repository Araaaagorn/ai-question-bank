"""ORM 模型层。

规划实体（对应说明书第 3 章数据模型）：
User / TeacherProfile / StudentProfile / TeacherStudentRelation /
Question / Paper / PaperQuestion / Submission / Review /
AnswerThread / AnswerNode / Fork / APIKey / Quota / Donation / AuditLog

新增模型：在本目录新建 <entity>.py，继承 app.db.base.Base，并在
__init__.py 中导入以完成注册。
"""

from app.db.base import Base  # noqa: F401  （模型均继承 Base）

__all__ = ["Base"]
