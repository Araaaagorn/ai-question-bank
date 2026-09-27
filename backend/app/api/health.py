"""健康检查：用于部署验证与 CI 冒烟测试。"""

from fastapi import APIRouter

router = APIRouter(tags=["system"])


@router.get("/health")
def health() -> dict:
    return {"status": "ok"}
