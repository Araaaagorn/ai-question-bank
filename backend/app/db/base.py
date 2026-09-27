"""SQLAlchemy 声明式基类。所有 ORM 模型继承 Base。"""

from sqlalchemy.orm import DeclarativeBase


class Base(DeclarativeBase):
    """统一元数据与命名约定。"""
