"""FastAPI 应用入口。

启动：.venv/bin/python -m uvicorn app.main:app --reload --port 8000
"""

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from app.api.health import router as health_router
from app.config import settings

app = FastAPI(
    title="AI+题库智能教学平台 API",
    version="0.1.0",
    description="多教师/多学生/多租户的智能题库与试卷管理平台",
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origin_list,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 业务路由统一挂载在 /api/v1 下，按功能模块拆分（见 app/api/）
app.include_router(health_router, prefix="/api/v1")


@app.get("/")
def root() -> dict:
    return {"name": "AI+题库智能教学平台", "docs": "/docs", "health": "/api/v1/health"}
