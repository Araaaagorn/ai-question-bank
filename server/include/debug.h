#ifndef AIQB_DEBUG_H
#define AIQB_DEBUG_H

/**
 * @file debug.h
 * @brief 全局调试日志基础设施。
 *
 * 启用/禁用方式：
 *   定义 DEBUG       → 调试日志生效（默认开启）
 *   将 DEBUG 改为 DEBUGx → 调试日志全部编译为空（零开销）
 *
 * 使用方式：
 *   #include "debug.h"
 *   KV_DEBUG("namespace=%s key=%s", ns, key);
 */

#include <stdio.h>

#define DEBUG
/* ── 若要禁用 debug 输出，将上面一行改为： ── */
/* #define DEBUGx                                                         */

#ifdef DEBUG

/**
 * @brief 调试日志宏。
 *        输出格式： [DEBUG] 文件名:行号: 消息
 * @param fmt  格式字符串（const char*）
 * @param ...  可变参数
 */
#define KV_DEBUG(fmt, ...) \
    fprintf(stderr, "[DEBUG] %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__)

#else

#define KV_DEBUG(fmt, ...) ((void)0)

#endif /* DEBUG */

#endif /* AIQB_DEBUG_H */