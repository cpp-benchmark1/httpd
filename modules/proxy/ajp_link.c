/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <sys/socket.h>
#include "ajp.h"
#include <stdio.h>
#include <apr_portable.h>
APLOG_USE_MODULE(proxy_ajp);

apr_status_t ajp_ilink_send(apr_socket_t *sock, ajp_msg_t *msg)
{
    char         *buf;
    apr_status_t status;
    apr_size_t   length;

    ajp_msg_end(msg);

    length = msg->len;
    buf    = (char *)msg->buf;

    do {
        apr_size_t written = length;

        status = apr_socket_send(sock, buf, &written);
        if (status != APR_SUCCESS) {
            ap_log_error(APLOG_MARK, APLOG_ERR, status, NULL, APLOGNO(01029)
                          "ajp_ilink_send(): send failed");
            return status;
        }
        length -= written;
        buf    += written;
    } while (length);

    return APR_SUCCESS;
}

typedef struct message_ctx {
    apr_byte_t *buf;
    apr_size_t  len;
    void       (*audit_cb)(struct message_ctx *ctx);
} message_ctx;

static void parse_message(message_ctx *ctx);
static void validate_message(message_ctx *ctx);
static void log_message(message_ctx *ctx);
static void audit_callback(message_ctx *ctx);
static void cleanup_message(message_ctx *ctx);


// Parsing the message
static void parse_message(message_ctx *ctx) {
    // Validate the message
    validate_message(ctx);
}

// Validating the message
static void validate_message(message_ctx *ctx) {
    // Log the message
    log_message(ctx);
}

// Logging the message
static void log_message(message_ctx *ctx) {
    if (ctx->len > 0) {
        printf("Log: First byte is %02x\n", ctx->buf[0]);
    }
}

// Auditing the message 
static void audit_callback(message_ctx *ctx) {
    if (ctx->len > 1) {
        //SINK
        printf("Audit: Second byte is %02x\n", ctx->buf[1]);
    }
}

// Cleanup
static void cleanup_message(message_ctx *ctx) {
    free(ctx->buf);
}

static apr_status_t ilink_read(apr_socket_t *sock, apr_byte_t *buf,
                               apr_size_t len)
{
    apr_os_sock_t os_sd;
    apr_status_t  rv;
    ssize_t       nread = 0;

    rv = apr_os_sock_get(&os_sd, sock);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    while (nread < (ssize_t)len) {
        //SOURCE
        ssize_t rc = recv((int)os_sd, buf + nread, len - nread, 0);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            return APR_FROM_OS_ERROR(errno);
        }
        else if (rc == 0) {
            return APR_EOF;
        }
        nread += rc;
    }

    // Wrap buffer in context struct
    message_ctx ctx;
    ctx.buf = buf;
    ctx.len = len;
    ctx.audit_cb = audit_callback;

    // Parse, validate, and log the message
    parse_message(&ctx);

    // Free the buffer in a cleanup function
    cleanup_message(&ctx);

    if (ctx.audit_cb) {
        ctx.audit_cb(&ctx);
    }

    return APR_SUCCESS;
}

apr_status_t ajp_ilink_receive(apr_socket_t *sock, ajp_msg_t *msg)
{
    apr_status_t status;
    apr_size_t   hlen;
    apr_size_t   blen;

    hlen = msg->header_len;

    status = ilink_read(sock, msg->buf, hlen);

    if (status != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, status, NULL, APLOGNO(01030)
                     "ajp_ilink_receive() can't receive header");
        return (APR_STATUS_IS_TIMEUP(status) ? APR_TIMEUP : AJP_ENO_HEADER);
    }

    status = ajp_msg_check_header(msg, &blen);

    if (status != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL, APLOGNO(01031)
                     "ajp_ilink_receive() received bad header");
        return AJP_EBAD_HEADER;
    }

    status = ilink_read(sock, msg->buf + hlen, blen);

    if (status != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_ERR, status, NULL, APLOGNO(01032)
                     "ajp_ilink_receive() error while receiving message body "
                     "of length %" APR_SIZE_T_FMT,
                     hlen);
        return AJP_EBAD_MESSAGE;
    }

    ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, NULL, APLOGNO(01033)
                 "ajp_ilink_receive() received packet len=%" APR_SIZE_T_FMT
                 "type=%d",
                  blen, (int)msg->buf[hlen]);

    return APR_SUCCESS;
}

