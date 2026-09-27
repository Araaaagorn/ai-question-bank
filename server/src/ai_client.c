#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

#include "ai_client.h"

/* 大模型统一代理客户端（libcurl + cJSON）。
 *
 * TODO(功能开发阶段，对应功能说明书第 6/7 章)：
 *   1. 组装 OpenAI 兼容的 chat/completions JSON 请求体（cJSON）；
 *   2. curl_easy_init() → curl_easy_setopt() 携带 cfg->ai_api_key，
 *      向 cfg->ai_base_url 发起 POST；
 *   3. cJSON 解析响应，提取 choices[0].message.content 写入 out；
 *   4. 所有调用经后端代理，API Key 永不暴露给前端。
 *
 * 安全红线（功能说明书 10.3 节）：
 *   - 提示词仅允许解答当前题目相关内容，禁止越狱/无关提问；
 *   - 不泄露系统提示、其他学生隐私、API Key。
 */
int ai_completion(const AppConfig *cfg, const char *prompt, char *out, size_t out_len) {
    (void)cfg;
    (void)prompt;
    if (out != NULL && out_len > 0) out[0] = '\0';
    return -1; /* 未实现 */
}
