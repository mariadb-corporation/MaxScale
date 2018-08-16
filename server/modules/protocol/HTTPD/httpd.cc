/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file httpd.c - HTTP daemon protocol module
 *
 * The httpd protocol module is intended as a mechanism to allow connections
 * into the gateway for the purpose of accessing information within
 * the gateway with a REST interface
 * databases.
 *
 * In the first instance it is intended to allow a debug connection to access
 * internal data structures, however it may also be used to manage the
 * configuration of the gateway via REST interface.
 *
 * @verbatim
 * Revision History
 * Date         Who                     Description
 * 08/07/2013   Massimiliano Pinto      Initial version
 * 09/07/2013   Massimiliano Pinto      Added /show?dcb|session for all dcbs|sessions
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "HTTPD"

#include "httpd.h"
#include <ctype.h>
#include <maxscale/alloc.h>
#include <maxscale/protocol.h>
#include <maxscale/modinfo.h>
#include <maxscale/log.h>
#include <maxscale/resultset.hh>

#define ISspace(x) isspace((int)(x))
#define HTTP_SERVER_STRING "MaxScale(c) v.1.0.0"

static int httpd_read_event(DCB* dcb);
static int httpd_write_event(DCB *dcb);
static int httpd_write(DCB *dcb, GWBUF *queue);
static int httpd_error(DCB *dcb);
static int httpd_hangup(DCB *dcb);
static int httpd_accept(DCB *dcb);
static int httpd_close(DCB *dcb);
static int httpd_listen(DCB *dcb, char *config);
static int httpd_get_line(int sock, char *buf, int size);
static void httpd_send_headers(DCB *dcb, int final, bool auth_ok);
static char *httpd_default_auth();

extern "C"
{
/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_PROTOCOL MyObject =
    {
        httpd_read_event,   /**< Read - EPOLLIN handler        */
        httpd_write,        /**< Write - data from gateway     */
        httpd_write_event,  /**< WriteReady - EPOLLOUT handler */
        httpd_error,        /**< Error - EPOLLERR handler      */
        httpd_hangup,       /**< HangUp - EPOLLHUP handler     */
        httpd_accept,       /**< Accept                        */
        NULL,               /**< Connect                       */
        httpd_close,        /**< Close                         */
        httpd_listen,       /**< Create a listener             */
        NULL,               /**< Authentication                */
        httpd_default_auth, /**< Default authenticator         */
        NULL,               /**< Connection limit reached      */
        NULL,
        NULL,
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_PROTOCOL,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_PROTOCOL_VERSION,
        "An experimental HTTPD implementation for use in administration",
        "V1.2.0",
        MXS_NO_MODULE_CAPABILITIES,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

}
/*lint +e14 */

static const char* default_auth = "NullAuthAllow";

/**
 * The default authenticator name for this protocol
 *
 * @return name of authenticator
 */
static char *httpd_default_auth()
{
    return (char*)default_auth;
}

/**
 * Read event for EPOLLIN on the httpd protocol module.
 *
 * @param dcb   The descriptor control block
 * @return
 */
static int httpd_read_event(DCB* dcb)
{
    MXS_SESSION *session = dcb->session;

    int numchars = 1;
    char buf[HTTPD_REQUESTLINE_MAXLEN - 1] = "";
    char *query_string = NULL;
    char method[HTTPD_METHOD_MAXLEN - 1] = "";
    char url[HTTPD_SMALL_BUFFER] = "";
    size_t i, j;
    int headers_read = 0;
    HTTPD_session *client_data = NULL;
    GWBUF *uri;

    client_data = static_cast<HTTPD_session*>(dcb->data);

    /**
     * get the request line
     * METHOD URL HTTP_VER\r\n
     */

    numchars = httpd_get_line(dcb->fd, buf, sizeof(buf));

    i = 0;
    j = 0;
    while (!ISspace(buf[j]) && (i < sizeof(method) - 1))
    {
        method[i] = buf[j];
        i++;
        j++;
    }
    method[i] = '\0';

    strcpy(client_data->method, method);

    /* check allowed http methods */
    if (strcasecmp(method, "GET") && strcasecmp(method, "POST"))
    {
        //httpd_unimplemented(dcb->fd);
        return 0;
    }

    i = 0;

    while ( (j < sizeof(buf)) && ISspace(buf[j]))
    {
        j++;
    }

    while ((j < sizeof(buf) - 1) && !ISspace(buf[j]) && (i < sizeof(url) - 1))
    {
        url[i] = buf[j];
        i++;
        j++;
    }

    url[i] = '\0';

    /**
     * Get the query string if availble
     */

    if (strcasecmp(method, "GET") == 0)
    {
        query_string = url;
        while ((*query_string != '?') && (*query_string != '\0'))
        {
            query_string++;
        }
        if (*query_string == '?')
        {
            *query_string = '\0';
        }
    }

    /** If listener->authenticator is the default authenticator, it means that
     * we don't need to check the user credentials. All other authenticators
     * cause a 401 Unauthorized to be returned on the first try. */
    bool auth_ok = strcmp(httpd_default_auth(), dcb->listener->authenticator) == 0;

    /**
     * Get the request headers
     */

    while ((numchars > 0) && strcmp("\n", buf))
    {
        char *value = NULL;
        char *end = NULL;
        numchars = httpd_get_line(dcb->fd, buf, sizeof(buf));
        if ((value = strchr(buf, ':')))
        {
            *value = '\0';
            value++;
            end = &value[strlen(value) - 1];
            *end = '\0';

            if (strncasecmp(buf, "Hostname", 6) == 0)
            {
                strcpy(client_data->hostname, value);
            }
            if (strncasecmp(buf, "useragent", 9) == 0)
            {
                strcpy(client_data->useragent, value);
            }

            if (strcmp(buf, "Authorization") == 0)
            {
                GWBUF *auth_data = gwbuf_alloc_and_load(strlen(value), value);
                MXS_OOM_IFNULL(auth_data);

                if (auth_data)
                {
                    /** The freeing entry point is called automatically when
                     * the client DCB is closed */
                    dcb->authfunc.extract(dcb, auth_data);
                    auth_ok = dcb->authfunc.authenticate(dcb) == MXS_AUTH_SUCCEEDED;
                    gwbuf_free(auth_data);
                }
            }
        }
    }

    if (numchars)
    {
        headers_read = 1;
        memcpy(&client_data->headers_received, &headers_read, sizeof(int));
    }

    /**
     * Now begins the server reply
     */

    /* send all the basic headers and close with \r\n */
    httpd_send_headers(dcb, 1, auth_ok);

#if 0
    /**
     * ToDO: launch proper content handling based on the requested URI, later REST interface
     *
     */
    if (strcmp(url, "/show") == 0)
    {
        if (query_string && strlen(query_string))
        {
            if (strcmp(query_string, "dcb") == 0)
            {
                dprintAllDCBs(dcb);
            }
            if (strcmp(query_string, "session") == 0)
            {
                dprintAllSessions(dcb);
            }
        }
    }
    if (strcmp(url, "/services") == 0)
    {
        ResultSet *set, *seviceGetList();
        if ((set = serviceGetList()) != NULL)
        {
            resultset_stream_json(set, dcb);
            resultset_free(set);
        }
    }
#endif
    if (auth_ok && (uri = gwbuf_alloc(strlen(url) + 1)) != NULL)
    {
        strcpy((char *)GWBUF_DATA(uri), url);
        gwbuf_set_type(uri, GWBUF_TYPE_HTTP);
        MXS_SESSION_ROUTE_QUERY(session, uri);
    }

    /* force the client connecton close */
    dcb_close(dcb);

    return 0;
}

/**
 * EPOLLOUT handler for the HTTPD protocol module.
 *
 * @param dcb   The descriptor control block
 * @return
 */
static int httpd_write_event(DCB *dcb)
{
    return dcb_drain_writeq(dcb);
}

/**
 * Write routine for the HTTPD protocol module.
 *
 * Writes the content of the buffer queue to the socket
 * observing the non-blocking principles of the gateway.
 *
 * @param dcb   Descriptor Control Block for the socket
 * @param queue Linked list of buffes to write
 */
static int httpd_write(DCB *dcb, GWBUF *queue)
{
    int rc;
    rc = dcb_write(dcb, queue);
    return rc;
}

/**
 * Handler for the EPOLLERR event.
 *
 * @param dcb   The descriptor control block
 */
static int httpd_error(DCB *dcb)
{
    dcb_close(dcb);
    return 0;
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb   The descriptor control block
 */
static int httpd_hangup(DCB *dcb)
{
    dcb_close(dcb);
    return 0;
}

/**
 * Handler for the EPOLLIN event when the DCB refers to the listening
 * socket for the protocol.
 *
 * @param listener   The descriptor control block
 */
static int httpd_accept(DCB *listener)
{
    int n_connect = 0;
    DCB *client_dcb;

    while ((client_dcb = dcb_accept(listener)) != NULL)
    {
        HTTPD_session *client_data = NULL;

        /* create the session data for HTTPD */
        if ((client_data = (HTTPD_session *)MXS_CALLOC(1, sizeof(HTTPD_session))) == NULL)
        {
            dcb_close(client_dcb);
            continue;
        }
        client_dcb->data = client_data;

        client_dcb->session = session_alloc(listener->session->service, client_dcb);
        if (NULL == client_dcb->session || poll_add_dcb(client_dcb) == -1)
        {
            dcb_close(client_dcb);
            continue;
        }
        n_connect++;
    }

    return n_connect;
}

/**
 * The close handler for the descriptor. Called by the gateway to
 * explicitly close a connection.
 *
 * @param dcb   The descriptor control block
 */

static int httpd_close(DCB *dcb)
{
    return 0;
}

/**
 * HTTTP daemon listener entry point
 *
 * @param       listener        The Listener DCB
 * @param       config          Configuration (ip:port)
 */
static int httpd_listen(DCB *listener, char *config)
{
    return (dcb_listen(listener, config, "HTTPD") < 0) ? 0 : 1;
}

/**
 * HTTPD get line from client
 */
static int httpd_get_line(int sock, char *buf, int size)
{
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n'))
    {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0)
        {
            if (c == '\r')
            {
                n = recv(sock, &c, 1, MSG_PEEK);
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                {
                    if (recv(sock, &c, 1, 0) < 0)
                    {
                        c = '\n';
                    }
                }
                else
                {
                    c = '\n';
                }
            }
            buf[i] = c;
            i++;
        }
        else
        {
            c = '\n';
        }
    }

    buf[i] = '\0';

    return i;
}

/**
 * HTTPD send basic headers with 200 OK
 */
static void httpd_send_headers(DCB *dcb, int final, bool auth_ok)
{
    char date[64] = "";
    const char *fmt = "%a, %d %b %Y %H:%M:%S GMT";
    time_t httpd_current_time = time(NULL);

    struct tm tm;
    localtime_r(&httpd_current_time, &tm);
    strftime(date, sizeof(date), fmt, &tm);
    const char *response = auth_ok ? "200 OK" : "401 Unauthorized";
    dcb_printf(dcb,
               "HTTP/1.1 %s\r\n"
               "Date: %s\r\n"
               "Server: %s\r\n"
               "Connection: close\r\n"
               "WWW-Authenticate: Basic realm=\"MaxInfo\"\r\n"
               "Content-Type: application/json\r\n",
               response, date, HTTP_SERVER_STRING);

    /* close the headers */
    if (final)
    {
        dcb_printf(dcb, "\r\n");
    }
}
