"""数据库引擎与会话工厂。

开发默认 SQLite（数据文件位于 data/，不入库）；生产通过 DATABASE_URL
切换 PostgreSQL 等。SQLite 模式下会自动创建数据文件所在目录。
"""
from pathlib import Path

from sqlalchemy import create_engine
from sqlalchemy.orm import sessionmaker

from app.config import settings

if settings.database_url.startswith("sqlite"):
    # sqlite:///./data/xxx.db → 提取文件路径，确保父目录存在
    db_path = settings.database_url.replace("sqlite:///", "", 1)
    if db_path and db_path != ":memory:" and not db_path.startswith("/"):
        Path(db_path).parent.mkdir(parents=True, exist_ok=True)

engine = create_engine(
    settings.database_url,
    connect_args={"check_same_thread": False} if settings.database_url.startswith("sqlite") else {},
)

SessionLocal = sessionmaker(bind=engine, autocommit=False, autoflush=False)


def get_db():
    """FastAPI 依赖：每个请求一个数据库会话。"""
    db = SessionLocal()
    try:
        yield db
    finally:
        db.close()
