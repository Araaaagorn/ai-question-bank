#ifndef AIQB_AI_CLIENT_H
#define AIQB_AI_CLIENT_H

#include "config.h"

/* 调用大模型生成解答（OpenAI 兼容协议，libcurl 实现）。
 * 框架阶段为预留接口（返回 -1 未实现）；功能开发阶段在
 * src/ai_client.c 中完成实现。 */
int ai_completion(const AppConfig *cfg, const char *prompt, char *out, size_t out_len);

#endif /* AIQB_AI_CLIENT_H */
