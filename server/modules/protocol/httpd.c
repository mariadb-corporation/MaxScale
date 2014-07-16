/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
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
 * 11/07/2014	Mark Riddoch		Recoded as more generic protocol module
 *					removing hardcoded example
 *
 * @endverbatim
 */

#include <httpd.h>
#include <gw.h>
#include <modinfo.h>
#include <skygw_utils.h>
#include <log_manager.h>

extern int lm_enabled_logfiles_bitmask;

MODULE_INFO info = {
	MODULE_API_PROTOCOL,
	MODULE_IN_DEVELOPMENT,
	GWPROTOCOL_VERSION,
	"An experimental HTTPD implementation for use in admnistration"
};

#define HTTP_SERVER_STRING "Gateway(c) v.1.0.0"
static char *version_str = "V1.0.1";

static int httpd_read_event(DCB* dcb);
static int httpd_write_event(DCB *dcb);
static int httpd_write(DCB *dcb, GWBUF *queue);
static int httpd_error(DCB *dcb);
static int httpd_hangup(DCB *dcb);
static int httpd_accept(DCB *dcb);
static int httpd_close(DCB *dcb);
static int httpd_listen(DCB *dcb, char *config);
static char *httpd_nextline(GWBUF *buf, char *ptr);
static void httpd_process_header(GWBUF *buf, char *sol, HTTPD_session *client_data);

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
httpd_read_event(DCB *dcb)
{
SESSION		*session = dcb->session;
GWBUF		*buf = NULL;
char		*ptr, *sol;
HTTPD_session	*client_data = NULL;
int 		n;

	// Read all the available data
	if ((n = dcb_read(dcb, &buf)) != -1)
	{
		client_data = dcb->data;

		if (client_data->saved)
		{
			buf = gwbuf_append(client_data->saved, buf);
			client_data->saved = NULL;
		}
		buf = gwbuf_make_contiguous(buf);

		ptr = GWBUF_DATA(buf);

		if (strncasecmp(ptr, "POST", 4))
		{
			client_data->method = METHOD_POST;
			gwbuf_add_property(buf, "Method", "POST");
			ptr = ptr + 4;
		}
		else if (strncasecmp(ptr, "PUT", 3))
		{
			client_data->method = METHOD_PUT;
			gwbuf_add_property(buf, "Method", "PUT");
			ptr = ptr + 3;
		}
		else if (strncasecmp(ptr, "GET", 3))
		{
			client_data->method = METHOD_GET;
			gwbuf_add_property(buf, "Method", "GET");
			ptr = ptr + 3;
		}
		else if (strncasecmp(ptr, "HEAD", 4))
		{
			client_data->method = METHOD_HEAD;
			gwbuf_add_property(buf, "Method", "HEAD");
			ptr = ptr + 4;
		}
		while (ptr < (char *)(buf->end) && isspace(*ptr))
			ptr++;
		sol = ptr;
		while (ptr < (char *)(buf->end) && isspace(*ptr) == 0)
			ptr++;
		client_data->url = strndup(sol, ptr - sol);
		gwbuf_add_property(buf, "URL", client_data->url);
		while ((sol = httpd_nextline(buf, ptr)) != NULL && 
				*sol != '\n' && *sol != '\r')
		{
			httpd_process_header(buf, sol, client_data);
			ptr = sol;
		}

		/*
		 * We have read all the headers, or run out of data to 
		 * examine.
		 */
		if (sol == NULL)
		{
			client_data->saved = buf;
			return 0;
		}
		else
		{
			if (((char *)(buf->end) - sol)
					< client_data->request_len)
			{
				client_data->saved = buf;
			}
			else
			{
				LOGIF(LT, (skygw_log_write(
		                           LOGFILE_TRACE,
               		            "HTTPD: request %s.\n", client_data->url)));
				SESSION_ROUTE_QUERY(session, buf);
				if (client_data->url)
				{
					free(client_data->url);
					client_data->url = NULL;
				}
			}
		}



	}
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
HTTPD_session	*client_data = NULL;
	if (dcb->data)
	{
		client_data = dcb->data;
		if (client_data->url)
		{
			free(client_data->url);
			client_data->url = NULL;
		}
		free(dcb->data);
		dcb->data = NULL;
	}
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
		socklen_t		addrlen = 0;
		DCB			*client = NULL;
		HTTPD_session		*client_data = NULL;

		if ((so = accept(dcb->fd, (struct sockaddr *)&addr, &addrlen)) == -1)
			return n_connect;
		else
		{
			atomic_add(&dcb->stats.n_accepts, 1);
			client = dcb_alloc(DCB_ROLE_REQUEST_HANDLER);
			client->fd = so;
			client->remote = strdup(inet_ntoa(addr.sin_addr));
			memcpy(&client->func, &MyObject, sizeof(GWPROTOCOL));

			/* we don't need the session */
			client->session = session_alloc(dcb->session->service, client);

			/* create the session data for HTTPD */
			client_data = (HTTPD_session *)calloc(1, sizeof(HTTPD_session));
			memset(client_data, 0, sizeof(HTTPD_session));
			client->data = client_data;

			if (poll_add_dcb(client) == -1)
			{
				return n_connect;
			}
			n_connect++;
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
int                     rc;

	memcpy(&listener->func, &MyObject, sizeof(GWPROTOCOL));
	if (!parse_bindconfig(config, 6442, &addr))
		return 0;

	if ((listener->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		return 0;
	}

        /* socket options */
	setsockopt(listener->fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   (char *)&one,
                   sizeof(one));

        /* set NONBLOCKING mode */
        setnonblocking(listener->fd);

        /* bind address and port */
        if (bind(listener->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
        	return 0;
	}

        rc = listen(listener->fd, SOMAXCONN);
        
        if (rc == 0) {
            fprintf(stderr,
                    "Listening http connections at %s\n",
                    config);
        } else {
            int eno = errno;
            errno = 0;
            fprintf(stderr,
                    "\n* Failed to start listening http due error %d, %s\n\n",
                    eno,
                    strerror(eno));
            return 0;
        }

        
        if (poll_add_dcb(listener) == -1)
	{
		return 0;
	}
	return 1;
}

/**
 * Return the start of the next line int the buffer.
 *
 * @param buf	The GWBUF chain
 * @param ptr	Start point within the buffer
 *
 * @return the start of the next line or NULL if there are no more lines
 */
static char *
httpd_nextline(GWBUF *buf, char *ptr)
{
	while (ptr < (char *)(buf->end) && *ptr != '\n' && *ptr != '\r')
		ptr++;
	if (ptr >= (char *)(buf->end))
		return NULL;

	/* Skip prcisely one CR/LF */
	if (*ptr == '\r')
		ptr++;
	if (*ptr == '\n')
		ptr++;
	return ptr;
}

/**
 * The headers to extract from the HTTP request and add as properties to the
 * GWBUF structure.
 */
static char *headers[] = {
	"Content-Type",
	"User-Agent",
	"From",
	"Date",
	NULL
};

/**
 * Process a single header line
 *
 * @param buf	The GWBUF that contains the request
 * @param sol	The current start of line
 * @param client_data	The client data structure for this request
 */
static void
httpd_process_header(GWBUF *buf, char *sol, HTTPD_session *client_data)
{
char	*ptr = sol;
char	cbuf[300];
int	len, i;

	/* Find the end of the line */
	while (ptr < (char *)(buf->end) && *ptr != '\n' && *ptr != '\r')
		ptr++;

	if (strncmp(sol, "Content-Length:", strlen("Content-Length:")) == 0)
	{
		char *p1 = sol + strlen("Content-Length:");
		while (isspace(*p1))
			p1++;
		len = ptr - p1;
		strncpy(cbuf, p1, len);
		cbuf[len] = 0;
		client_data->request_len = atoi(cbuf);
		gwbuf_add_property(buf, "Content-Length", cbuf);
	}
	else
	{ 
		for (i = 0; headers[i]; i++)
		{
			if (strncmp(sol, headers[i], strlen(headers[i])) == 0)
			{
				char *p1 = sol + strlen(headers[i]) + 1;
				while (isspace(*p1))
					p1++;
				len = ptr - p1;
				strncpy(cbuf, p1, len);
				cbuf[len] = 0;
				gwbuf_add_property(buf, headers[i], cbuf);
			}
		}
	}
}
