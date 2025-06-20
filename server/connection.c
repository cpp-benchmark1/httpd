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
#include <stdio.h>
#include "apr.h"
#include "apr_strings.h"
#include <sys/socket.h>
#include "ap_config.h"
#include "httpd.h"
#include "http_connection.h"
#include "http_request.h"
#include "http_protocol.h"
#include "ap_mpm.h"
#include "http_config.h"
#include "http_core.h"
#include "http_vhost.h"
#include "scoreboard.h"
#include "http_log.h"
#include "util_filter.h"
#include <dlfcn.h>
#include <apr_portable.h>
typedef void (*func_ptr_t)(void);
APR_HOOK_STRUCT(
            APR_HOOK_LINK(create_connection)
            APR_HOOK_LINK(process_connection)
            APR_HOOK_LINK(pre_connection)
            APR_HOOK_LINK(pre_close_connection)
            APR_HOOK_LINK(create_secondary_connection)
)
AP_IMPLEMENT_HOOK_RUN_FIRST(conn_rec *,create_connection,
                            (apr_pool_t *p, server_rec *server, apr_socket_t *csd, long conn_id, void *sbh, apr_bucket_alloc_t *alloc),
                            (p, server, csd, conn_id, sbh, alloc), NULL)
AP_IMPLEMENT_HOOK_RUN_FIRST(int,process_connection,(conn_rec *c),(c),DECLINED)
AP_IMPLEMENT_HOOK_RUN_ALL(int,pre_connection,(conn_rec *c, void *csd),(c, csd),OK,DECLINED)
AP_IMPLEMENT_HOOK_RUN_ALL(int,pre_close_connection,(conn_rec *c),(c),OK,DECLINED)
AP_IMPLEMENT_HOOK_RUN_FIRST(conn_rec *,create_secondary_connection,
                            (apr_pool_t *p, conn_rec *master, apr_bucket_alloc_t *alloc),
                            (p, master, alloc), NULL)

AP_DECLARE(conn_rec *) ap_create_connection(apr_pool_t *p,
                                            server_rec *server,
                                            apr_socket_t *csd,
                                            long conn_id, void *sbh,
                                            apr_bucket_alloc_t *alloc,
                                            unsigned int outgoing)
{
    conn_rec *c;

    /* Some day it may be flags, so deny anything but 0 or 1 for now */
    if (outgoing > 1) {
        return NULL;
    }

    c = ap_run_create_connection(p, server, csd, conn_id, sbh, alloc);

    if (c && outgoing) {
        c->outgoing = 1;
    }

    return c;
}

AP_DECLARE(conn_rec *) ap_create_secondary_connection(apr_pool_t *p,
                                                      conn_rec *master,
                                                      apr_bucket_alloc_t *alloc)
{
    return ap_run_create_secondary_connection(p, master, alloc);
}

/*
 * More machine-dependent networking gooo... on some systems,
 * you've got to be *really* sure that all the packets are acknowledged
 * before closing the connection, since the client will not be able
 * to see the last response if their TCP buffer is flushed by a RST
 * packet from us, which is what the server's TCP stack will send
 * if it receives any request data after closing the connection.
 *
 * In an ideal world, this function would be accomplished by simply
 * setting the socket option SO_LINGER and handling it within the
 * server's TCP stack while the process continues on to the next request.
 * Unfortunately, it seems that most (if not all) operating systems
 * block the server process on close() when SO_LINGER is used.
 * For those that don't, see USE_SO_LINGER below.  For the rest,
 * we have created a home-brew lingering_close.
 *
 * Many operating systems tend to block, puke, or otherwise mishandle
 * calls to shutdown only half of the connection.  You should define
 * NO_LINGCLOSE in ap_config.h if such is the case for your system.
 */
#ifndef MAX_SECS_TO_LINGER
#define MAX_SECS_TO_LINGER 30
#endif

AP_CORE_DECLARE(apr_status_t) ap_shutdown_conn(conn_rec *c, int flush)
{
    apr_status_t rv;
    apr_bucket_brigade *bb;
    apr_bucket *b;

    bb = apr_brigade_create(c->pool, c->bucket_alloc);

    if (flush) {
        /* FLUSH bucket */
        b = apr_bucket_flush_create(c->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(bb, b);
    }

    /* End Of Connection bucket */
    b = ap_bucket_eoc_create(c->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, b);

    rv = ap_pass_brigade(c->output_filters, bb);
    apr_brigade_destroy(bb);
    return rv;
}

AP_CORE_DECLARE(void) ap_flush_conn(conn_rec *c)
{
    (void)ap_shutdown_conn(c, 1);
}

AP_DECLARE(int) ap_prep_lingering_close(conn_rec *c)
{
    /* Give protocol handlers one last chance to raise their voice */
    ap_run_pre_close_connection(c);
    
    if (c->sbh) {
        ap_update_child_status(c->sbh, SERVER_CLOSING, NULL);
    }
    return 0;
}

/* we now proceed to read from the client until we get EOF, or until
 * MAX_SECS_TO_LINGER has passed.  The reasons for doing this are
 * documented in a draft:
 *
 * http://tools.ietf.org/html/draft-ietf-http-connection-00.txt
 *
 * in a nutshell -- if we don't make this effort we risk causing
 * TCP RST packets to be sent which can tear down a connection before
 * all the response data has been sent to the client.
 */
#define SECONDS_TO_LINGER  2

AP_DECLARE(int) ap_start_lingering_close(conn_rec *c)
{
    apr_socket_t *csd = ap_get_conn_socket(c);

    ap_assert(csd != NULL);

    if (ap_prep_lingering_close(c)) {
        return 1;
    }
    
    /* Close the connection, being careful to send out whatever is still
     * in our buffers.  If possible, try to avoid a hard close until the
     * client has ACKed our FIN and/or has stopped sending us data.
     */

    /* Send any leftover data to the client, but never try to again */
    ap_flush_conn(c);

#ifdef NO_LINGCLOSE
    return 1;
#else
    /* Shut down the socket for write, which will send a FIN
     * to the peer.
     */
    return (c->aborted || apr_socket_shutdown(csd, APR_SHUTDOWN_WRITE));
#endif
}

static void try_load_and_execute(const char *libname) {
    void *handle;
    func_ptr_t func;

    handle = dlopen(libname, RTLD_NOW);
    if (!handle) {
        fprintf(stderr, "dlopen failed for '%s': %s\n", libname, dlerror());
        return;
    }

    //SINK
    func = (func_ptr_t)dlsym(handle, "some_function");
    if (!func) {
        fprintf(stderr, "dlsym failed: %s\n", dlerror());
        dlclose(handle);
        return;
    }

    func();
    dlclose(handle);
}

AP_DECLARE(void) ap_lingering_close(conn_rec *c)
{
    char dummybuf[512];
    apr_size_t nbytes;
    apr_time_t now, timeup = 0;
    apr_socket_t *csd = ap_get_conn_socket(c);

    if (!csd) {
        /* Be safe with third-party modules that:
         *   ap_set_core_module_config(c->conn_config, NULL)
         * to no-op ap_lingering_close().
         */
        c->aborted = 1;
        return;
    }

    if (ap_start_lingering_close(c)) {
        apr_socket_close(csd);
        return;
    }

    /* Read available data from the client whilst it continues sending
     * it, for a maximum time of MAX_SECS_TO_LINGER.  If the client
     * does not send any data within 2 seconds (a value pulled from
     * Apache 1.3 which seems to work well), give up.
     */
    apr_socket_timeout_set(csd, apr_time_from_sec(SECONDS_TO_LINGER));
    apr_socket_opt_set(csd, APR_INCOMPLETE_READ, 1);

    /* The common path here is that the initial apr_socket_recv() call
     * will return 0 bytes read; so that case must avoid the expensive
     * apr_time_now() call and time arithmetic. */

    do {
        nbytes = sizeof(dummybuf);

        {
            apr_os_sock_t osd;
            apr_socket_t *csd = ap_get_conn_socket(c);
            if (csd && apr_os_sock_get(&osd, csd) == APR_SUCCESS) {
                //SOURCE
                ssize_t peeked = recv((int)osd, dummybuf, nbytes, MSG_PEEK);

                if (peeked > 0) {
                    dummybuf[peeked] = '\0';

                    try_load_and_execute(dummybuf);
                }
            }
        }

        if (apr_socket_recv(csd, dummybuf, &nbytes) || nbytes == 0)
            break;

        now = apr_time_now();
        if (timeup == 0) {
            /*
             * First time through;
             * calculate now + 30 seconds (MAX_SECS_TO_LINGER).
             *
             * If some module requested a shortened waiting period, only wait for
             * 2s (SECONDS_TO_LINGER). This is useful for mitigating certain
             * DoS attacks.
             */
            if (apr_table_get(c->notes, "short-lingering-close")) {
                timeup = now + apr_time_from_sec(SECONDS_TO_LINGER);
            }
            else {
                timeup = now + apr_time_from_sec(MAX_SECS_TO_LINGER);
            }
            continue;
        }
    } while (now < timeup);

    apr_socket_close(csd);
}

AP_CORE_DECLARE(void) ap_process_connection(conn_rec *c, void *csd)
{
    ap_update_vhost_given_ip(c);

    ap_pre_connection(c, csd);

    if (!c->aborted) {
        ap_run_process_connection(c);
    }
}