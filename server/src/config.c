#include <stdlib.h>

#include "config.h"

static const char *env_or(const char *key, const char *fallback) {
    const char *v = getenv(key);
    return (v != NULL && v[0] != '\0') ? v : fallback;
}

AppConfig config_load(void) {
    AppConfig cfg;
    cfg.port = atoi(env_or("PORT", "8000"));
    if (cfg.port <= 0 || cfg.port > 65535) cfg.port = 8000;
    cfg.db_path = env_or("DATABASE_PATH", "data/ai_question_bank.db");
    cfg.web_root = env_or("WEB_ROOT", "../web");
    cfg.ai_base_url = env_or("AI_API_BASE_URL", "https://api.openai.com/v1");
    cfg.ai_api_key = env_or("AI_API_KEY", "");
    cfg.ai_model = env_or("AI_MODEL", "gpt-4o-mini");
    return cfg;
}
