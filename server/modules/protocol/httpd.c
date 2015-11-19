/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
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
 * Date		Who			Description
 * 08/07/2013	Massimiliano Pinto	Initial version
 * 09/07/2013 	Massimiliano Pinto	Added /show?dcb|session for all dcbs|sessions
 *
 * @endverbatim
 */

#include <httpd.h>
#include <gw.h>
#include <modinfo.h>
#include <log_manager.h>
#include <resultset.h>

MODULE_INFO info = {
	MODULE_API_PROTOCOL,
	MODULE_IN_DEVELOPMENT,
	GWPROTOCOL_VERSION,
	"An experimental HTTPD implementation for use in admnistration"
};

#define ISspace(x) isspace((int)(x))
#define HTTP_SERVER_STRING "MaxScale(c) v.1.0.0"
static char *version_str = "V1.0.1";

static int httpd_read_event(DCB* dcb);
static int httpd_write_event(DCB *dcb);
static int httpd_write(DCB *dcb, GWBUF *queue);
static int httpd_error(DCB *dcb);
static int httpd_hangup(DCB *dcb);
static int httpd_accept(DCB *dcb);
static int httpd_close(DCB *dcb);
static int httpd_listen(DCB *dcb, char *config);
static int httpd_get_line(int sock, char *buf, int size);
static void httpd_send_headers(DCB *dcb, int final);

/**
 * The "module object" for the httpd protocol module.
 */
static GWPROTOCOL MyObject = { 
	httpd_read_event,			/**< Read - EPOLLIN handler	 */
	httpd_write,				/**< Write - data from gateway	 */
	httpd_write_event,			/**< WriteReady - EPOLLOUT handler */
	httpd_error,				/**< Error - EPOLLERR handler	 */
	httpd_hangup,				/**< HangUp - EPOLLHUP handler	 */
	httpd_accept,				/**< Accept			 */
	NULL,					/**< Connect			 */
	httpd_close,				/**< Close			 */
	httpd_listen,				/**< Create a listener		 */
	NULL,					/**< Authentication		 */
	NULL					/**< Session			 */
	};

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
GWPROTOCOL *
GetModuleObject()
{
	return &MyObject;
}

/**
 * Read event for EPOLLIN on the httpd protocol module.
 *
 * @param dcb	The descriptor control block
 * @return
 */
static int
httpd_read_event(DCB* dcb)
{
SESSION		*session = dcb->session;
ROUTER_OBJECT	*router = session->service->router;
ROUTER		*router_instance = session->service->router_instance;
void		*rsession = session->router_session;

int numchars = 1;
char buf[HTTPD_REQUESTLINE_MAXLEN-1] = "";
char *query_string = NULL;
char method[HTTPD_METHOD_MAXLEN-1] = "";
char url[HTTPD_SMALL_BUFFER] = "";
size_t i, j;
int headers_read = 0;
HTTPD_session *client_data = NULL;
GWBUF	*uri;

	client_data = dcb->data;

	/**
	 * get the request line
	 * METHOD URL HTTP_VER\r\n
	 */

	numchars = httpd_get_line(dcb->fd, buf, sizeof(buf));

	i = 0; j = 0;
	while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
		method[i] = buf[j];
		i++; j++;
	}
	method[i] = '\0';

	strcpy(client_data->method, method);

	/* check allowed http methods */
	if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
		//httpd_unimplemented(dcb->fd);
		return 0;
	}

	i = 0;

	while ( (j < sizeof(buf)) && ISspace(buf[j])) {
		j++;
	}

	while ((j < sizeof(buf) - 1) && !ISspace(buf[j]) && (i < sizeof(url) - 1)) {
		url[i] = buf[j];
		i++; j++;
	}

	url[i] = '\0';

	/**
	 * Get the query string if availble
	 */

	if (strcasecmp(method, "GET") == 0) {
		query_string = url;
		while ((*query_string != '?') && (*query_string != '\0'))
			query_string++;
		if (*query_string == '?') {

			*query_string = '\0';
			query_string++;
		}
	}

	/**
	 * Get the request headers
	 */

	while ((numchars > 0) && strcmp("\n", buf)) {
		char *value = NULL;
		char *end = NULL;
		numchars = httpd_get_line(dcb->fd, buf, sizeof(buf));
		if ( (value = strchr(buf, ':'))) {
			*value = '\0';
			value++;
			end = &value[strlen(value) -1];
			*end = '\0';

			if (strncasecmp(buf, "Hostname", 6) == 0) {
				strcpy(client_data->hostname, value);
			}
			if (strncasecmp(buf, "useragent", 9) == 0) {
				strcpy(client_data->useragent, value);
			}
		}
	}

	if (numchars) {
		headers_read = 1;
		memcpy(&client_data->headers_received, &headers_read, sizeof(int));
	}

	/**
	 * Now begins the server reply
	 */

	/* send all the basic headers and close with \r\n */
	httpd_send_headers(dcb, 1);

#if 0
	/**
	 * ToDO: launch proper content handling based on the requested URI, later REST interface
	 *
	 */
	if (strcmp(url, "/show") == 0) {
		if (query_string && strlen(query_string)) {
			if (strcmp(query_string, "dcb") == 0)
				dprintAllDCBs(dcb);
			if (strcmp(query_string, "session") == 0)
				dprintAllSessions(dcb);
		}
	}
	if (strcmp(url, "/services") == 0) {
		RESULTSET *set, *seviceGetList();
		if ((set = serviceGetList()) != NULL)
		{
			resultset_stream_json(set, dcb);
			resultset_free(set);
		}
	}
#endif
	if ((uri = gwbuf_alloc(strlen(url) + 1)) != NULL)
	{
		strcpy((char *)GWBUF_DATA(uri), url);
		gwbuf_set_type(uri, GWBUF_TYPE_HTTP);
		SESSION_ROUTE_QUERY(session, uri);
	}

	/* force the client connecton close */
        dcb_close(dcb);

	return 0;
}

/**
 * EPOLLOUT handler for the HTTPD protocol module.
 *
 * @param dcb	The descriptor control block
 * @return
 */
static int
httpd_write_event(DCB *dcb)
{
	return dcb_drain_writeq(dcb);
}

/**
 * Write routine for the HTTPD protocol module.
 *
 * Writes the content of the buffer queue to the socket
 * observing the non-blocking principles of the gateway.
 *
 * @param dcb	Descriptor Control Block for the socket
 * @param queue	Linked list of buffes to write
 */
static int
httpd_write(DCB *dcb, GWBUF *queue)
{
        int rc;
        rc = dcb_write(dcb, queue);
	return rc;
}

/**
 * Handler for the EPOLLERR event.
 *
 * @param dcb	The descriptor control block
 */
static int
httpd_error(DCB *dcb)
{
	dcb_close(dcb);
	return 0;
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb	The descriptor control block
 */
static int
httpd_hangup(DCB *dcb)
{
	dcb_close(dcb);
	return 0;
}

/**
 * Handler for the EPOLLIN event when the DCB refers to the listening
 * socket for the protocol.
 *
 * @param dcb	The descriptor control block
 */
static int
httpd_accept(DCB *dcb)
{
int	n_connect = 0;

	while (1)
	{
		int			so = -1;
		struct sockaddr_in	addr;
		socklen_t		addrlen;
		DCB			*client = NULL;
		HTTPD_session		*client_data = NULL;

		if ((so = accept(dcb->fd, (struct sockaddr *)&addr, &addrlen)) == -1)
			return n_connect;
		else
		{
			atomic_add(&dcb->stats.n_accepts, 1);
			
			if((client = dcb_alloc(DCB_ROLE_REQUEST_HANDLER))){
				client->fd = so;
				client->remote = strdup(inet_ntoa(addr.sin_addr));
				memcpy(&client->func, &MyObject, sizeof(GWPROTOCOL));

				/* create the session data for HTTPD */
				client_data = (HTTPD_session *)calloc(1, sizeof(HTTPD_session));
				client->data = client_data;
			
				client->session =
                                	session_alloc(dcb->session->service, client);

				if (NULL == client->session || poll_add_dcb(client) == -1)
					{
						close(so);
                                                dcb_close(client);
						return n_connect;
					}
				n_connect++;
			}
		}
	}
	
	return n_connect;
}

/**
 * The close handler for the descriptor. Called by the gateway to
 * explicitly close a connection.
 *
 * @param dcb	The descriptor control block
 */

static int
httpd_close(DCB *dcb)
{
	return 0;
}

/**
 * HTTTP daemon listener entry point
 *
 * @param	listener	The Listener DCB
 * @param	config		Configuration (ip:port)
 */
static int
httpd_listen(DCB *listener, char *config)
{
struct sockaddr_in	addr;
int			one = 1;
int         rc;
int			syseno = 0;

	memcpy(&listener->func, &MyObject, sizeof(GWPROTOCOL));
	if (!parse_bindconfig(config, 6442, &addr))
		return 0;

	if ((listener->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		return 0;
	}

        /* socket options */
	syseno = setsockopt(listener->fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   (char *)&one,
                   sizeof(one));

	if(syseno != 0){
                char errbuf[STRERROR_BUFLEN];
		MXS_ERROR("Failed to set socket options. Error %d: %s",
                          errno, strerror_r(errno, errbuf, sizeof(errbuf)));
		return 0;
	}
        /* set NONBLOCKING mode */
        setnonblocking(listener->fd);

        /* bind address and port */
        if (bind(listener->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
        	return 0;
	}

        rc = listen(listener->fd, SOMAXCONN);
        
        if (rc == 0) {
            MXS_NOTICE("Listening httpd connections at %s", config);
        } else {
            int eno = errno;
            errno = 0;
            char errbuf[STRERROR_BUFLEN];
            fprintf(stderr,
                    "\n* Failed to start listening http due error %d, %s\n\n",
                    eno,
                    strerror_r(eno, errbuf, sizeof(errbuf)));
            return 0;
        }

        
        if (poll_add_dcb(listener) == -1)
	{
		return 0;
	}
	return 1;
}

/**
 * HTTPD get line from client
 */
static int httpd_get_line(int sock, char *buf, int size) {
	int i = 0;
	char c = '\0';
	int n;

	while ((i < size - 1) && (c != '\n')) {
		n = recv(sock, &c, 1, 0);
		/* DEBUG printf("%02X\n", c); */
		if (n > 0) {
			if (c == '\r') {
				n = recv(sock, &c, 1, MSG_PEEK);
				/* DEBUG printf("%02X\n", c); */
				if ((n > 0) && (c == '\n')) {
					if(recv(sock, &c, 1, 0) < 0){
						c = '\n';	
					}
				} else {
					c = '\n';
				}
			}
			buf[i] = c;
			i++;
		} else {
			c = '\n';
		}
	}

	buf[i] = '\0';

	return i;
}

/**
 * HTTPD send basic headers with 200 OK
 */
static void httpd_send_headers(DCB *dcb, int final)
{
	char date[64] = "";
	const char *fmt = "%a, %d %b %Y %H:%M:%S GMT";
	time_t httpd_current_time = time(NULL);

        struct tm tm;
        localtime_r(&httpd_current_time, &tm);
	strftime(date, sizeof(date), fmt, &tm);

	dcb_printf(dcb, "HTTP/1.1 200 OK\r\nDate: %s\r\nServer: %s\r\nConnection: close\r\nContent-Type: application/json\r\n", date, HTTP_SERVER_STRING);

	/* close the headers */
	if (final) {
 		dcb_printf(dcb, "\r\n");
	}
}
