#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "auth.h"

/* ── 内存 Session 存储（固定数组，单线程 demo 够用） ── */
static Session g_sessions[MAX_SESSIONS];
static int g_session_count = 0;

/* 生成随机十六进制 token（用 rand()，demo 够用） */
static void generate_token(char *buf, size_t len) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len - 1; i++) {
        buf[i] = hex[rand() % 16];
    }
    buf[len - 1] = '\0';
}

const char *session_create(int user_id, const char *role, const char *name) {
    if (g_session_count >= MAX_SESSIONS) return NULL;

    Session *s = &g_sessions[g_session_count];
    generate_token(s->token, sizeof(s->token));

    /* 确保唯一性（极低概率碰撞，简单重试） */
    for (int retry = 0; retry < 5; retry++) {
        bool collision = false;
        for (int i = 0; i < g_session_count; i++) {
            if (g_sessions[i].active &&
                strcmp(g_sessions[i].token, s->token) == 0) {
                collision = true;
                break;
            }
        }
        if (!collision) break;
        generate_token(s->token, sizeof(s->token));
    }

    s->user_id    = user_id;
    s->expires_at = time(NULL) + 86400;  /* 24 小时后过期 */
    s->active     = true;
    snprintf(s->role, sizeof(s->role), "%s", role);
    snprintf(s->name, sizeof(s->name), "%s", name);

    g_session_count++;
    return s->token;
}

Session *session_validate(const char *token) {
    if (token == NULL || token[0] == '\0') return NULL;
    time_t now = time(NULL);

    for (int i = 0; i < g_session_count; i++) {
        Session *s = &g_sessions[i];
        if (s->active && strcmp(s->token, token) == 0) {
            if (s->expires_at > now) return s;
            /* 过期 → 标记失效 */
            s->active = false;
            return NULL;
        }
    }
    return NULL;
}

void session_destroy(const char *token) {
    if (token == NULL) return;
    for (int i = 0; i < g_session_count; i++) {
        if (g_sessions[i].active && strcmp(g_sessions[i].token, token) == 0) {
            g_sessions[i].active = false;
            return;
        }
    }
}

const char *extract_bearer_token(const char *auth_header) {
    if (auth_header == NULL) return NULL;

    /* 不区分大小写匹配 "Bearer " 前缀 */
    const char *prefix = "Bearer ";
    size_t plen = strlen(prefix);

    if (strncasecmp(auth_header, prefix, plen) != 0) return NULL;
    const char *token = auth_header + plen;
    if (token[0] == '\0') return NULL;
    return token;
}