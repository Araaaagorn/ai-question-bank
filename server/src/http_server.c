#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <microhttpd.h>

#include "http_server.h"
#include "routes.h"

static volatile sig_atomic_t g_stop = 0;

static void handle_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static enum MHD_Result request_handler(void *cls, struct MHD_Connection *conn,
                                       const char *url, const char *method,
                                       const char *version, const char *upload_data,
                                       size_t *upload_data_size, void **con_cls) {
    (void)version;
    (void)upload_data;
    (void)upload_data_size;
    (void)con_cls;
    return route_dispatch(conn, url, method, (const AppConfig *)cls);
}

int http_server_start(const AppConfig *cfg) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    struct MHD_Daemon *daemon = MHD_start_daemon(
        MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_SOCKET,
        (uint16_t)cfg->port, NULL, NULL,
        request_handler, (void *)cfg,
        MHD_OPTION_END);
    if (daemon == NULL) {
        fprintf(stderr, "✘ HTTP 服务启动失败（端口 %d 可能被占用）\n", cfg->port);
        return -1;
    }

    fprintf(stderr, "✔ HTTP 服务运行中: http://0.0.0.0:%d （Ctrl+C 停止）\n", cfg->port);
    while (!g_stop) sleep(1);

    MHD_stop_daemon(daemon);
    fprintf(stderr, "✔ 服务已停止\n");
    return 0;
}
