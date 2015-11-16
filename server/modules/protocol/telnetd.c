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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dcb.h>
#include <buffer.h>
#include <service.h>
#include <session.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <router.h>
#include <poll.h>
#include <atomic.h>
#include <gw.h>
#include <telnetd.h>
#include <adminusers.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <modinfo.h>

MODULE_INFO info = {
	MODULE_API_PROTOCOL,
	MODULE_GA,
	GWPROTOCOL_VERSION,
	"A telnet deamon protocol for simple administration interface"
};

/**
 * @file telnetd.c - telnet daemon protocol module
 *
 * The telnetd protocol module is intended as a mechanism to allow connections
 * into the gateway for the purpsoe of accessing debugging information within
 * the gateway rather than a protocol to be used to send queries to backend
 * databases.
 *
 * In the first instance it is intended to allow a debug connection to access
 * internal data structures, however it may also be used to manage the 
 * configuration of the gateway.
 *
 * @verbatim
 * Revision History
 * Date		Who			Description
 * 17/06/2013	Mark Riddoch		Initial version
 * 17/07/2013	Mark Riddoch		Addition of login phase
 * 07/07/2015   Martin Brampton         Call unified dcb_close on error
 *
 * @endverbatim
 */

static char *version_str = "V1.0.1";

static int telnetd_read_event(DCB* dcb);
static int telnetd_write_event(DCB *dcb);
static int telnetd_write(DCB *dcb, GWBUF *queue);
static int telnetd_error(DCB *dcb);
static int telnetd_hangup(DCB *dcb);
static int telnetd_accept(DCB *dcb);
static int telnetd_close(DCB *dcb);
static int telnetd_listen(DCB *dcb, char *config);

/**
 * The "module object" for the telnetd protocol module.
 */
static GWPROTOCOL MyObject = { 
	telnetd_read_event,		/**< Read - EPOLLIN handler	 */
	telnetd_write,			/**< Write - data from gateway	 */
	telnetd_write_event,		/**< WriteReady - EPOLLOUT handler */
	telnetd_error,			/**< Error - EPOLLERR handler	 */
	telnetd_hangup,			/**< HangUp - EPOLLHUP handler	 */
	telnetd_accept,			/**< Accept			 */
	NULL,				/**< Connect			 */
	telnetd_close,			/**< Close			 */
	telnetd_listen,			/**< Create a listener		 */
	NULL,				/**< Authentication		 */
	NULL				/**< Session			 */
	};

static void 	telnetd_command(DCB *, unsigned char *cmd);
static void 	telnetd_echo(DCB *dcb, int enable);

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
    MXS_INFO("Initialise Telnetd Protocol module.");
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
 * Read event for EPOLLIN on the telnetd protocol module.
 *
 * @param dcb	The descriptor control block
 * @return
 */
static int
telnetd_read_event(DCB* dcb)
{
int		n;
GWBUF		*head = NULL;
SESSION		*session = dcb->session;
TELNETD		*telnetd = (TELNETD *)dcb->protocol;
char		*password, *t;

	if ((n = dcb_read(dcb, &head, 0)) != -1)
	{

		if (head)
		{
			unsigned char *ptr = GWBUF_DATA(head);
			ptr = GWBUF_DATA(head);
			while (GWBUF_LENGTH(head) && *ptr == TELNET_IAC)
			{
				telnetd_command(dcb, ptr + 1);
				GWBUF_CONSUME(head, 3);
				ptr = GWBUF_DATA(head);
			}
			if (GWBUF_LENGTH(head))
			{
				switch (telnetd->state)
				{
				case TELNETD_STATE_LOGIN:
					telnetd->username = strndup(GWBUF_DATA(head), GWBUF_LENGTH(head));
					/* Strip the cr/lf from the username */
				        t = strstr(telnetd->username, "\r\n");
				        if (t)
                				*t = 0;
					telnetd->state = TELNETD_STATE_PASSWD;
					dcb_printf(dcb, "Password: ");
					telnetd_echo(dcb, 0);
					gwbuf_consume(head, GWBUF_LENGTH(head));
					break;
				case TELNETD_STATE_PASSWD:
					password = strndup(GWBUF_DATA(head), GWBUF_LENGTH(head));
					/* Strip the cr/lf from the username */
				        t = strstr(password, "\r\n");
				        if (t)
                				*t = 0;
					if (admin_verify(telnetd->username, password))
					{
						telnetd_echo(dcb, 1);
						telnetd->state = TELNETD_STATE_DATA;
						dcb_printf(dcb, "\n\nMaxScale> ");
					}
					else
					{
						dcb_printf(dcb, "\n\rLogin incorrect\n\rLogin: ");
						telnetd_echo(dcb, 1);
						telnetd->state = TELNETD_STATE_LOGIN;
						free(telnetd->username);
					}
					gwbuf_consume(head, GWBUF_LENGTH(head));
					free(password);
					break;
				case TELNETD_STATE_DATA:
					SESSION_ROUTE_QUERY(session, head);
					break;
				}
			}
			else
			{
				// Force the free of the buffer header
				gwbuf_consume(head, 0);
			}
		}
	}
	return n;
}

/**
 * EPOLLOUT handler for the telnetd protocol module.
 *
 * @param dcb	The descriptor control block
 * @return
 */
static int
telnetd_write_event(DCB *dcb)
{
	return dcb_drain_writeq(dcb);
}

/**
 * Write routine for the telnetd protocol module.
 *
 * Writes the content of the buffer queue to the socket
 * observing the non-blocking principles of the gateway.
 *
 * @param dcb	Descriptor Control Block for the socket
 * @param queue	Linked list of buffes to write
 */
static int
telnetd_write(DCB *dcb, GWBUF *queue)
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
telnetd_error(DCB *dcb)
{
	return 0;
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb	The descriptor control block
 */
static int
telnetd_hangup(DCB *dcb)
{
	return 0;
}

/**
 * Handler for the EPOLLIN event when the DCB refers to the listening
 * socket for the protocol.
 *
 * @param dcb	The descriptor control block
 * @return The number of new connections created
 */
static int
telnetd_accept(DCB *dcb)
{
int	n_connect = 0;

	while (1)
	{
		int			so;
		struct sockaddr_in	addr;
		socklen_t		addrlen = sizeof(struct sockaddr);
		DCB			*client_dcb;
                TELNETD*                telnetd_pr = NULL;

                so = accept(dcb->fd, (struct sockaddr *)&addr, &addrlen);
                
		if (so == -1)
			return n_connect;
		else
		{
			atomic_add(&dcb->stats.n_accepts, 1);
                        client_dcb = dcb_alloc(DCB_ROLE_REQUEST_HANDLER);

			if (client_dcb == NULL)

			{
				close(so);
				return n_connect;
			}
                        client_dcb->fd = so;
			client_dcb->remote = strdup(inet_ntoa(addr.sin_addr));
			memcpy(&client_dcb->func, &MyObject, sizeof(GWPROTOCOL));
			client_dcb->session =
                                session_alloc(dcb->session->service, client_dcb);
                        if (NULL == client_dcb->session)
                        {
                            dcb_close(client_dcb);
                            return n_connect;
                        }
                        telnetd_pr = (TELNETD *)malloc(sizeof(TELNETD));
                        client_dcb->protocol = (void *)telnetd_pr;

                        if (telnetd_pr == NULL)
                        {
                                dcb_close(client_dcb);
				return n_connect;
			}

			if (poll_add_dcb(client_dcb))
			{
                                dcb_close(dcb);
				return n_connect;
			}
			n_connect++;
			telnetd_pr->state = TELNETD_STATE_LOGIN;
			telnetd_pr->username = NULL;
			dcb_printf(client_dcb, "MaxScale login: ");
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
telnetd_close(DCB *dcb)
{
TELNETD *telnetd = dcb->protocol;

	if (telnetd && telnetd->username)
		free(telnetd->username);

        return 0;
}

/**
 * Telnet daemon listener entry point
 *
 * @param	listener	The Listener DCB
 * @param	config		Configuration (ip:port)
 */
static int
telnetd_listen(DCB *listener, char *config)
{
struct sockaddr_in	addr;
int			one = 1;
int         rc;
int			syseno = 0;

	memcpy(&listener->func, &MyObject, sizeof(GWPROTOCOL));

	if (!parse_bindconfig(config, 4442, &addr))
		return 0;


	if ((listener->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		return 0;
	}

        // socket options
	syseno = setsockopt(listener->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));
	
	if(syseno != 0){
                char errbuf[STRERROR_BUFLEN];
		MXS_ERROR("Failed to set socket options. Error %d: %s",
                          errno, strerror_r(errno, errbuf, sizeof(errbuf)));
		return 0;
	}
        // set NONBLOCKING mode
        setnonblocking(listener->fd);
        // bind address and port
        if (bind(listener->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
        	return 0;
	}

        rc = listen(listener->fd, SOMAXCONN);
        
        if (rc == 0) {
            MXS_NOTICE("Listening telnet connections at %s", config);
        } else {
            int eno = errno;
            errno = 0;
            char errbuf[STRERROR_BUFLEN];
            fprintf(stderr,
                    "\n* Failed to start listening telnet due error %d, %s\n\n",
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
 * Telnet command implementation
 *
 * Called for each command in the telnet stream.
 *
 * Currently we do no command execution
 *
 * @param	dcb	The client DCB
 * @param	cmd	The command stream
 */
static void
telnetd_command(DCB *dcb, unsigned char *cmd)
{
}

/**
 * Enable or disable telnet protocol echo
 *
 * @param dcb		DCB of the telnet connection
 * @param enable	Enable or disable echo functionality
 */
static void
telnetd_echo(DCB *dcb, int enable)
{
GWBUF	*gwbuf;
char	*buf;

	if ((gwbuf = gwbuf_alloc(3)) == NULL)
		return;
	buf = GWBUF_DATA(gwbuf);
	buf[0] = TELNET_IAC;
	buf[1] = enable ? TELNET_WONT : TELNET_WILL;
	buf[2] = TELNET_ECHO;
	dcb_write(dcb, gwbuf);
}
