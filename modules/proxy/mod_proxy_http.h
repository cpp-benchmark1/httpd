#ifndef MOD_PROXY_HTTP_H
#define MOD_PROXY_HTTP_H

#include "httpd.h"

void proxy_http_process_incoming_buffer(const char *buf, size_t len);

#endif /* MOD_PROXY_HTTP_H */ 