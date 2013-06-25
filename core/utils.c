/*
 * This file is distributed as part of the SkySQL Gateway. It is free
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
 * 
 */

/**
 * @file utils.c - General utility functions
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 10-06-2013	Massimiliano Pinto	Initial implementation
 * 12-06-2013	Massimiliano Pinto	Read function trought 
 * 					the gwbuff strategy
 * 13-06-2013	Massimiliano Pinto	Gateway local authentication
 *					basics
 *
 * @endverbatim
 */


#include <gw.h>
#include <dcb.h>
#include <session.h>
#include <mysql_protocol.h>
#include <openssl/sha.h>
#include <poll.h>

// used in the hex2bin function
#define char_val(X) (X >= '0' && X <= '9' ? X-'0' :\
                     X >= 'A' && X <= 'Z' ? X-'A'+10 :\
                     X >= 'a' && X <= 'z' ? X-'a'+10 :\
                     '\177')

// used in the bin2hex function
char hex_upper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char hex_lower[] = "0123456789abcdefghijklmnopqrstuvwxyz";

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
		int b = -1;
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

	return 1;
}

/**
 * Write function for client DCB
 *
 * @param dcb	The DCB of the client
 * @param queue	Queue of buffers to write
 */
int
MySQLWrite(DCB *dcb, GWBUF *queue)
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
		return 0;
	}

	return 1;
}

//////////////////////////////////////////
//backend write event triggered by EPOLLOUT
//////////////////////////////////////////

int gw_write_backend_event(DCB *dcb) {

	//fprintf(stderr, ">>> gw_write_backend_event for %i\n", dcb->fd);

	return 0;
}

//////////////////////////////////////////
//client read event triggered by EPOLLIN
//////////////////////////////////////////
int gw_route_read_event(DCB* dcb) {
	MySQLProtocol *protocol = NULL;
	int b = -1;

	if (dcb) {
		protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
	}


	if (ioctl(dcb->fd, FIONREAD, &b)) {
		fprintf(stderr, "Client Ioctl FIONREAD error %i, %s\n", errno , strerror(errno));
	} else {
		//fprintf(stderr, "Client IOCTL FIONREAD bytes to read = %i\n", b);
	}

	switch (protocol->state) {
		case MYSQL_AUTH_SENT:
		
			/*
			* Read all the data that is available into a chain of buffers
			*/
			{
				int len = -1;
				int ret = -1;
				GWBUF *queue = NULL;
				GWBUF *gw_buffer = NULL;
				int auth_val = -1;
				//////////////////////////////////////////////////////
				// read and handle errors & close, or return if busyA
				// note: if b == 0 error handling is not triggered, just return
				// without closing
				//////////////////////////////////////////////////////

                                if ((ret = gw_read_gwbuff(dcb, &gw_buffer, b)) != 0)
                                        return ret;

				// example with consume, assuming one buffer only ...
				queue = gw_buffer;
				len = GWBUF_LENGTH(queue);

				//fprintf(stderr, "<<< Reading from Client %i bytes: [%s]\n", len, GWBUF_DATA(queue));
		
				auth_val = gw_mysql_do_authentication(dcb, queue);

				// Data handled withot the dcb->func.write
				// so consume it now
				// be sure to consume it all
				queue = gwbuf_consume(queue, len);

				if (auth_val == 0)
					protocol->state = MYSQL_AUTH_RECV;
				else 
					protocol->state = MYSQL_AUTH_FAILED;
			}

			break;

		case MYSQL_IDLE:
		case MYSQL_WAITING_RESULT:
			/*
			* Read all the data that is available into a chain of buffers
			*/
			{
				int len;
				GWBUF *queue = NULL;
				GWBUF *gw_buffer = NULL;
				uint8_t *ptr_buff = NULL;
				int mysql_command = -1;
				int ret = -1;
	
				//////////////////////////////////////////////////////
				// read and handle errors & close, or return if busy
				//////////////////////////////////////////////////////
				if ((ret = gw_read_gwbuff(dcb, &gw_buffer, b)) != 0)
					return ret;

				// Now assuming in the first buffer there is the information form mysql command

				// following code is only for debug now
				queue = gw_buffer;
				len = GWBUF_LENGTH(queue);
			
				ptr_buff = GWBUF_DATA(queue);

				// get mysql commang
				if (ptr_buff)
					mysql_command = ptr_buff[4];

				if (mysql_command  == '\x03') {
					/// this is a query !!!!
					//fprintf(stderr, "<<< MySQL Query from Client %i bytes: [%s]\n", len, ptr_buff+5);
				//else
					//fprintf(stderr, "<<< Reading from Client %i bytes: [%s]\n", len, ptr_buff);
				}
		
				///////////////////////////
				// Handling the COM_QUIT
				//////////////////////////
				if (mysql_command == '\x01') {
					fprintf(stderr, "COM_QUIT received\n");
					if (dcb->session->backends) {
						dcb->session->backends->func.write(dcb, queue);
						(dcb->session->backends->func).error(dcb->session->backends);
					}
					(dcb->func).error(dcb);
				
					return 1;
				}

				protocol->state = MYSQL_ROUTING;

				///////////////////////////////////////
				// writing in the backend buffer queue
				///////////////////////////////////////
				dcb->session->backends->func.write(dcb->session->backends, queue);

				protocol->state = MYSQL_WAITING_RESULT;

			}
			break;

		default:
			// todo
			break;
	}
	
	return 0;
}

///////////////////////////////////////////////
// client write event triggered by EPOLLOUT
//////////////////////////////////////////////
int gw_handle_write_event(DCB *dcb) {
	MySQLProtocol *protocol = NULL;

	if (dcb == NULL) {
		fprintf(stderr, "DCB is NULL, return\n");
		return 1;
	}

	if (dcb->state == DCB_STATE_DISCONNECTED) {
		return 1;
	}

	if (dcb->protocol) {
		protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
	} else {
		fprintf(stderr, "DCB protocol is NULL, return\n");
		return 1;
	}

	if (dcb->session) {
	} else {
		fprintf(stderr, "DCB session is NULL, return\n");
		return 1;
	}

	if (dcb->session->backends) {
	} else {
		fprintf(stderr, "DCB backend is NULL, continue\n");
	}

	if(protocol->state == MYSQL_AUTH_RECV) {

        	//write to client mysql AUTH_OK packet, packet n. is 2
		mysql_send_ok(dcb, 2, 0, NULL);

        	protocol->state = MYSQL_IDLE;

		return 0;
	}

	if (protocol->state == MYSQL_AUTH_FAILED) {
		// still to implement
		mysql_send_auth_error(dcb, 2, 0, "Authorization failed");

		dcb->func.error(dcb);
		if (dcb->session->backends)
			dcb->session->backends->func.error(dcb->session->backends);

		return 0;
	}

	if ((protocol->state == MYSQL_IDLE) || (protocol->state == MYSQL_WAITING_RESULT)) {
		int w;
		int saved_errno = 0;

		spinlock_acquire(&dcb->writeqlock);
		if (dcb->writeq)
		{
			int	len;

			/*
			 * Loop over the buffer chain in the pendign writeq
			 * Send as much of the data in that chain as possible and
			 * leave any balance on the write queue.
			 */
			while (dcb->writeq != NULL)
			{
				len = GWBUF_LENGTH(dcb->writeq);
				GW_NOINTR_CALL(w = write(dcb->fd, GWBUF_DATA(dcb->writeq), len););
				saved_errno = errno;
				if (w < 0)
				{
					break;
				}

				/*
				 * Pull the number of bytes we have written from
				 * queue with have.
				 */
				dcb->writeq = gwbuf_consume(dcb->writeq, w);
				if (w < len)
				{
					/* We didn't write all the data */
				}
			}
		}
		spinlock_release(&dcb->writeqlock);

		return 1;
	}

	return 1;
}

///
// set listener for mysql protocol
///
void MySQLListener(int epfd, char *config_bind) {
	DCB *listener;
	int l_so;
	struct sockaddr_in serv_addr;
	char *bind_address_and_port = NULL;
	char *p;
	char address[1024]="";
	int port=0;
	int one;

	// this gateway, as default, will bind on port 4404 for localhost only
	(config_bind != NULL) ? (bind_address_and_port = config_bind) : (bind_address_and_port = "127.0.0.1:4406");

	listener = (DCB *) calloc(1, sizeof(DCB));

	listener->state = DCB_STATE_ALLOC;
	listener->fd = -1;
	
        memset(&serv_addr, 0, sizeof serv_addr);
        serv_addr.sin_family = AF_INET;

        p = strchr(bind_address_and_port, ':');
        if (p) {
                strncpy(address, bind_address_and_port, sizeof(address));
                address[sizeof(address)-1] = '\0';
                p = strchr(address, ':');
                *p = '\0';
                port = atoi(p+1);
                setipaddress(&serv_addr.sin_addr, address);

                snprintf(address, (sizeof(address) - 1), "%s", inet_ntoa(serv_addr.sin_addr));
        } else {
                port = atoi(bind_address_and_port);
                serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
                sprintf(address, "0.0.0.0");
        }

	serv_addr.sin_port = htons(port);

	// socket create
	if ((l_so = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		error("can't open listening socket");
	}

	// socket options
	setsockopt(l_so, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));

	// set NONBLOCKING mode
        setnonblocking(l_so);

	// bind address and port
        if (bind(l_so, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		fprintf(stderr, ">>>> Bind failed !!! %i, [%s]\n", errno, strerror(errno));
                error("can't bind to address and port");
		exit(1);
        }

        fprintf(stderr, ">> GATEWAY bind is: %s:%i. FD is %i\n", address, port, l_so);

        listen(l_so, 10 * SOMAXCONN);

        fprintf(stderr, ">> GATEWAY listen backlog queue is %i\n", 10 * SOMAXCONN);

        listener->state = DCB_STATE_IDLE;

	// assign l_so to dcb
	listener->fd = l_so;

        // add listening socket to poll structure
        if (poll_add_dcb(listener) == -1) {
                perror("poll_add_dcb: listen_sock");
                exit(EXIT_FAILURE);
        }

	listener->func.accept = MySQLAccept;

	listener->state = DCB_STATE_LISTENING;
}


int MySQLAccept(DCB *listener) {

	fprintf(stderr, "MySQL Listener socket is: %i\n", listener->fd);

	while (1) {
		int c_sock;
		struct sockaddr_in local;
		socklen_t addrlen;
		addrlen = sizeof(local);
		DCB *client;
		DCB *backend;
		SESSION *session;
		MySQLProtocol *protocol;
		MySQLProtocol *ptr_proto;
		int sendbuf = GW_BACKEND_SO_SNDBUF;
		socklen_t optlen = sizeof(sendbuf);

		// new connection from client
		c_sock = accept(listener->fd, (struct sockaddr *) &local, &addrlen);

		if (c_sock == -1) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				fprintf(stderr, ">>>> NO MORE conns for MySQL Listener: errno is %i for %i\n", errno, listener->fd);
				/* We have processed all incoming connections. */
				break;
			} else {
				fprintf(stderr, "Accept error for %i, Err: %i, %s\n", listener->fd, errno, strerror(errno));
				// what else to do? 
				return 1;
			}
		}

		listener->stats.n_accepts++;

		fprintf(stderr, "Processing %i connection fd %i for listener %i\n", listener->stats.n_accepts, c_sock, listener->fd);
		// set nonblocking 

		setsockopt(c_sock, SOL_SOCKET, SO_SNDBUF, &sendbuf, optlen);
		setnonblocking(c_sock);

		client = (DCB *) calloc(1, sizeof(DCB));
		backend = (DCB *) calloc(1, sizeof(DCB));
		session = (SESSION *) calloc(1, sizeof(SESSION));
		protocol = (MySQLProtocol *) calloc(1, sizeof(MySQLProtocol));

		client->fd = c_sock;
		client->state = DCB_STATE_ALLOC;
		client->session = session;
		client->protocol = (void *)protocol;

		session->state = SESSION_STATE_ALLOC;
		session->client = client;
		session->backends = NULL;

		protocol->state = MYSQL_ALLOC;
		protocol->descriptor = client;
		protocol->fd = c_sock;

		backend->state = DCB_STATE_ALLOC;
		backend->session = NULL;
		backend->protocol = (MySQLProtocol *)gw_mysql_init(NULL);

		ptr_proto = (MySQLProtocol *)backend->protocol;

		// sha1(password) from client non yet handled
		// this is blocking until auth done
		if (gw_mysql_connect("127.0.0.1", 3306, "test", "massi", "massi", backend->protocol, 0) == 0) {
			fprintf(stderr, "Connected to backend mysql server\n");
			backend->fd = ptr_proto->fd;
			setnonblocking(backend->fd);
		} else {
			backend->fd = -1;
		}

		// if connected, add it to the poll
		if (backend->fd > 0) {
			if (poll_add_dcb(backend) == -1) {
				perror("poll_add_dcb: backend sock");
			} else {
				//fprintf(stderr, "--> Backend conn added, bk_fd [%i], scramble [%s], is session with client_fd [%i]\n", ptr_proto->fd, ptr_proto->scramble, client->fd);
				backend->state = DCB_STATE_POLLING;
				backend->session = session;
				(backend->func).read = gw_read_backend_event;
				(backend->func).write = MySQLWrite;
				(backend->func).write_ready = gw_write_backend_event;
				(backend->func).error = handle_event_errors_backend;
		
				// assume here one backend only.
				// in session.h
				// struct dcb      *backends;
				// instead of a list **backends;
				session->backends = backend;
			}
		}

		// assign function poiters to "func" field
		(client->func).error = handle_event_errors;
		(client->func).read = gw_route_read_event;
		(client->func).write = MySQLWrite;
		(client->func).write_ready = gw_handle_write_event;

		client->state = DCB_STATE_IDLE;

		// event install
		if (poll_add_dcb(client) == -1) {
			perror("poll_add_dcb: conn_sock");
			exit(EXIT_FAILURE);
		} else {
			//fprintf(stderr, "Added fd %i to poll, protocol state [%i]\n", c_sock , client->state);
			client->state = DCB_STATE_POLLING;
		}

		client->state = DCB_STATE_PROCESSING;

		//send handshake to the client
		MySQLSendHandshake(client);

		// client protocol state change
		protocol->state = MYSQL_AUTH_SENT;
	}

	return 0;
}


int setnonblocking(int fd) {
	int fl;

	if ((fl = fcntl(fd, F_GETFL, 0)) == -1) {
		fprintf(stderr, "Can't GET fcntli for %i, errno = %d, %s", fd, errno, strerror(errno));
		return 1;
	}

	if (fcntl(fd, F_SETFL, fl | O_NONBLOCK) == -1) {
		fprintf(stderr, "Can't SET fcntl for %i, errno = %d, %s", fd, errno, strerror(errno));
		return 1;
	}

	return 0;
}

char *gw_strend(register const char *s) {
	while (*s++);
	return (char*) (s-1);
}

///////////////////////////////
// generate a random char 
//////////////////////////////
static char gw_randomchar() {
   return (char)((rand() % 78) + 30);
}

/////////////////////////////////
// generate a random string
// output must be pre allocated
/////////////////////////////////
int gw_generate_random_str(char *output, int len) {

	int i;
	srand(time(0L));

	for ( i = 0; i < len; ++i ) {
		output[i] = gw_randomchar();
	}

	output[len]='\0';

	return 0;
}

/////////////////////////////////
// hex string to binary data
// output must be pre allocated
/////////////////////////////////
int gw_hex2bin(uint8_t *out, const char *in, unsigned int len) {
	const char *in_end= in + len;

	if (len == 0 || in == NULL) {
		return 1;
	}

	while (in < in_end) {
		register unsigned char b1 = char_val(*in);
		uint8_t b2 = 0;
		in++;
		b2 =  (b1 << 4) | char_val(*in);
		*out++ = b2;

		in++;
	}

	return 0;
}

/////////////////////////////////
// binary data to hex string
// output must be pre allocated
/////////////////////////////////
char *gw_bin2hex(char *out, const uint8_t *in, unsigned int len) {
	const uint8_t *in_end= in + len;
	if (len == 0 || in == NULL) {
		return NULL;
	}

	for (; in != in_end; ++in) {
		*out++= hex_upper[((uint8_t) *in) >> 4];
		*out++= hex_upper[((uint8_t) *in) & 0x0F];
	}
	*out= '\0';

	return out;
}

///////////////////////////////////////////////////////
// fill a preallocated buffer with XOR(str1, str2)
// XOR between 2 equal len strings
// note that XOR(str1, XOR(str1 CONCAT str2)) == str2
// and that  XOR(str1, str2) == XOR(str2, str1)
///////////////////////////////////////////////////////
void gw_str_xor(char *output, const uint8_t *input1, const uint8_t *input2, unsigned int len) {
	const uint8_t *input1_end = NULL;
	input1_end = input1 + len;

	while (input1 < input1_end)
		*output++= *input1++ ^ *input2++;

	*output = '\0';
}

/////////////////////////////////////////////////////////////
// fill a 20 bytes preallocated with SHA1 digest (160 bits)
// for one input on in_len bytes
/////////////////////////////////////////////////////////////
void gw_sha1_str(const uint8_t *in, int in_len, uint8_t *out) {
	unsigned char hash[SHA_DIGEST_LENGTH];

	SHA1(in, in_len, hash);
	memcpy(out, hash, SHA_DIGEST_LENGTH);
}

/////////////////////////////////////////////////////////////
// fill 20 bytes preallocated with SHA1 digest (160 bits)
// for two inputs, in_len and in2_len bytes
/////////////////////////////////////////////////////////////
void gw_sha1_2_str(const uint8_t *in, int in_len, const uint8_t *in2, int in2_len, uint8_t *out) {
	SHA_CTX context;
	unsigned char hash[SHA_DIGEST_LENGTH];

	SHA1_Init(&context);
	SHA1_Update(&context, in, in_len);
	SHA1_Update(&context, in2, in2_len);
	SHA1_Final(hash, &context);

	memcpy(out, hash, SHA_DIGEST_LENGTH);
}
