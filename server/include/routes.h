#ifndef AIQB_ROUTES_H
#define AIQB_ROUTES_H

#include <microhttpd.h>

#include "config.h"

/* 请求分发：/api/v1/* → JSON API；其余路径 → 静态文件（web/ 目录） */
enum MHD_Result route_dispatch(struct MHD_Connection *conn, const char *url,
                               const char *method, const AppConfig *cfg);

#endif /* AIQB_ROUTES_H */
