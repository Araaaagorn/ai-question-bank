#ifndef AIQB_HTTP_SERVER_H
#define AIQB_HTTP_SERVER_H

#include "config.h"

/* 启动 HTTP 服务（libmicrohttpd）。阻塞运行，收到 SIGINT/SIGTERM 后优雅退出。
 * 返回 0 正常退出；-1 启动失败。 */
int http_server_start(const AppConfig *cfg);

#endif /* AIQB_HTTP_SERVER_H */
