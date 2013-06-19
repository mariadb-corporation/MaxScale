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
#include <session.h>
#include <server.h>
#include "mysql_client_server_protocol.h"

/*
 * MySQL Protocol module for handling the protocol between the gateway
 * and the backend MySQL database.
 *
 * Revision History
 * Date		Who			Description
 * 14/06/2013	Mark Riddoch		Initial version
 * 17/06/2013	Massimiliano Pinto	Added Gateway To Backends routines
 */

static char *version_str = "V1.0.0";

int gw_read_backend_event(DCB* dcb);
int gw_write_backend_event(DCB *dcb);
int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
int gw_error_backend_event(DCB *dcb);
static int	mysql_backend_connect(DCB *dcb, SERVER *server, SESSION *session);

static GWPROTOCOL MyObject = { 
	gw_read_backend_event,			/* Read - EPOLLIN handler	 */
	gw_MySQLWrite_backend,			/* Write - data from gateway	 */
	gw_write_backend_event,			/* WriteReady - EPOLLOUT handler */
	gw_error_backend_event,			/* Error - EPOLLERR handler	 */
	NULL,					/* HangUp - EPOLLHUP handler	 */
	NULL,					/* Accept			 */
	NULL,					/* Connect			 */
	NULL,					/* Close			 */
	NULL					/* Listen			 */
	};

/*
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/*
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
	fprintf(stderr, "Initial MySQL Client Protcol module.\n");
}

/*
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


//////////////////////////////////////////
//backend read event triggered by EPOLLIN
//////////////////////////////////////////
int gw_read_backend_event(DCB *dcb) {
	int n;
	MySQLProtocol *client_protocol = NULL;

	if (dcb)
		if(dcb->session)
			client_protocol = SESSION_PROTOCOL(dcb->session, MySQLProtocol);

#ifdef GW_DEBUG_READ_EVENT
	fprintf(stderr, "Backend ready! Read from Backend %i, write to client %i, client state %i\n", dcb->fd, dcb->session->client->fd, client_protocol->state);
#endif

	if ((client_protocol->state == MYSQL_WAITING_RESULT) || (client_protocol->state == MYSQL_IDLE)) {
		int w;
		int b = -1;
		int tot_b = -1;
		uint8_t *ptr_buffer;
		GWBUF	*buffer, *head;

		if (ioctl(dcb->fd, FIONREAD, &b)) {
			fprintf(stderr, "Backend Ioctl FIONREAD error %i, %s\n", errno , strerror(errno));
		} else {
			//fprintf(stderr, "Backend IOCTL FIONREAD bytes to read = %i\n", b);
		}

		/*
		 * Read all the data that is available into a chain of buffers
		 */
		head = NULL;
		while (b > 0)
		{
			int bufsize = b < MAX_BUFFER_SIZE ? b : MAX_BUFFER_SIZE;
			if ((buffer = gwbuf_alloc(bufsize)) == NULL)
			{
				/* Bad news, we have run out of memory */
				return 0;
			}
			GW_NOINTR_CALL(n = read(dcb->fd, GWBUF_DATA(buffer), bufsize); dcb->stats.n_reads++);
			if (n < 0)
			{
				// if eerno == EAGAIN || EWOULDBLOCK is missing
				// do the right task, not just break
				break;
			}

			head = gwbuf_append(head, buffer);

			// how many bytes left
			b -= n;
		}

		// write the gwbuffer to client
		dcb->session->client->func.write(dcb->session->client, head);

		return 1;
	}

	return 0;
}

//////////////////////////////////////////
//backend write event triggered by EPOLLOUT
//////////////////////////////////////////
int gw_write_backend_event(DCB *dcb) {
	//fprintf(stderr, ">>> gw_write_backend_event for %i\n", dcb->fd);
        return 0;
}

/*
 * Write function for backend DCB
 *
 * @param dcb	The DCB of the client
 * @param queue	Queue of buffers to write
 */
int
gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue)
{
int	w, saved_errno = 0;

	spinlock_acquire(&dcb->writeqlock);
	if (dcb->writeq)
	{
		/*
		 * We have some queued data, so add our data to
		 * the write queue and return.
		 * The assumption is that there will be an EPOLLOUT
		 * event to drain what is already queued. We are protected
		 * by the spinlock, which will also be acquired by the
		 * the routine that drains the queue data, so we should
		 * not have a race condition on the event.
		 */
		dcb->writeq = gwbuf_append(dcb->writeq, queue);
		dcb->stats.n_buffered++;
	}
	else
	{
		int	len;

		/*
		 * Loop over the buffer chain that has been passed to us
		 * from the reading side.
		 * Send as much of the data in that chain as possible and
		 * add any balance to the write queue.
		 */
		while (queue != NULL)
		{
			len = GWBUF_LENGTH(queue);
			GW_NOINTR_CALL(w = write(dcb->fd, GWBUF_DATA(queue), len); dcb->stats.n_writes++);
			saved_errno = errno;
			if (w < 0)
			{
				break;
			}

			/*
			 * Pull the number of bytes we have written from
			 * queue with have.
			 */
			queue = gwbuf_consume(queue, w);
			if (w < len)
			{
				/* We didn't write all the data */
			}
		}
		/* Buffer the balance of any data */
		dcb->writeq = queue;
		if (queue)
		{
			dcb->stats.n_buffered++;
		}
	}
	spinlock_release(&dcb->writeqlock);

	if (queue && (saved_errno != EAGAIN || saved_errno != EWOULDBLOCK))
	{
		/* We had a real write failure that we must deal with */
		return 1;
	}

	return 0;
}

int gw_error_backend_event(DCB *dcb) {
        MySQLProtocol *protocol = DCB_PROTOCOL(dcb, MySQLProtocol);

        fprintf(stderr, "#### Handle Backend error function for %i\n", dcb->fd);

#ifdef GW_EVENT_DEBUG
        if (event != -1) {
                fprintf(stderr, ">>>>>> Backend DCB state %i, Protocol State %i: event %i, %i\n", dcb->state, dcb->proto_state, event & EPOLLERR, event & EPOLLHUP);
                if(event & EPOLLHUP)
                        fprintf(stderr, "EPOLLHUP\n");

                if(event & EPOLLERR)
                        fprintf(stderr, "EPOLLERR\n");

                if(event & EPOLLPRI)
                        fprintf(stderr, "EPOLLPRI\n");
        }
#endif

        if (dcb->state != DCB_STATE_LISTENING) {
                if (poll_remove_dcb(dcb) == -1) {
                                fprintf(stderr, "Backend poll_remove_dcb: from events check failed to delete %i, [%i]:[%s]\n", dcb->fd, errno, strerror(errno));
                }

#ifdef GW_EVENT_DEBUG
                fprintf(stderr, "Backend closing fd [%i]=%i, from events check\n", dcb->fd, protocol->fd);
#endif
                if (dcb->fd) {
                        dcb->state = DCB_STATE_DISCONNECTED;
                        fprintf(stderr, "Freeing backend MySQL conn %p, %p\n", dcb->protocol, &dcb->protocol);
                        gw_mysql_close((MySQLProtocol **)&dcb->protocol);
                        fprintf(stderr, "Freeing backend MySQL conn %p, %p\n", dcb->protocol, &dcb->protocol);
                }
        }
}

/**
 * Connect to a database server
 *
 * @param dcb		The DCB for the new connection
 * @param server	The server we are connecting to
 * @param session	The client session
 * @return The file descriptor we conencted with
 */
static int
mysql_backend_connect(DCB *dcb, SERVER *server, SESSION *session)
{
MySQLProtocol *ptr_proto = NULL;
MySQLProtocol *client_protocol = NULL;
MYSQL_session *s_data = NULL;

	dcb->protocol = (MySQLProtocol *)gw_mysql_init(NULL);

	ptr_proto = (MySQLProtocol *)dcb->protocol;

	s_data = (MYSQL_session *)session->data;

	// this is blocking until auth done
	if (gw_mysql_connect(server->name, server->port, s_data->db, s_data->user, s_data->client_sha1, dcb->protocol) == 0) {
		fprintf(stderr, "Connected to backend mysql server\n");
		dcb->fd = ptr_proto->fd;
		setnonblocking(dcb->fd);
	} else {
		fprintf(stderr, "<<<< NOT Connected to backend mysql server!!!\n");
		dcb->fd = -1;
	}

	return dcb->fd;
}
