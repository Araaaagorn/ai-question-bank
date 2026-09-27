"""初始化数据库：创建全部表。

用法（backend 目录下）：
    .venv/bin/python scripts/init_db.py
"""
import sys
from pathlib import Path

# 保证以 `python scripts/init_db.py` 直接运行时也能找到 app 包
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from app.db.base import Base  # noqa: E402
from app.db.session import engine  # noqa: E402
from app import models  # noqa: E402,F401  导入以注册模型


def main() -> None:
    Base.metadata.create_all(bind=engine)
    print(f"✔ 数据库初始化完成：{engine.url}")


if __name__ == "__main__":
    main()
