"""应用配置：从环境变量 / .env 加载。"""

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    """全局配置。生产环境通过 backend/.env 覆盖默认值。"""

    app_env: str = "development"
    port: int = 8000
    database_url: str = "sqlite:///./data/ai_question_bank.db"

    # 安全
    secret_key: str = "change-me-in-production"

    # 大模型（OpenAI 兼容协议，后端统一代理调用）
    ai_api_base_url: str = "https://api.openai.com/v1"
    ai_api_key: str = ""
    ai_model: str = "gpt-4o-mini"

    # CORS（开发期允许前端 Dev Server）
    cors_origins: str = "http://localhost:5173"

    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8")

    @property
    def cors_origin_list(self) -> list[str]:
        return [o.strip() for o in self.cors_origins.split(",") if o.strip()]


settings = Settings()
