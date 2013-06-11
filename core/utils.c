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

/*
 * Revision History
 *
 * Date		Who			Description
 * 10/06/13	Massimiliano Pinto	Initial implementation
 *
 */


#include <gw.h>
#include <dcb.h>
#include <session.h>
#include <mysql_protocol.h>
#include <openssl/sha.h>

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

int gw_read_backend_event(DCB *dcb, int epfd) {
	int n;
	MySQLProtocol *client_protocol = NULL;

	if (dcb)
		if(dcb->session)
			client_protocol = SESSION_PROTOCOL(dcb->session, MySQLProtocol);

#ifdef GW_DEBUG_READ_EVENT
	fprintf(stderr, "Backend ready! Read from Backend %i, write to client %i, client state %i\n", dcb->fd, dcb->session->client->fd, client_protocol->state);
#endif

	if ((client_protocol->state == MYSQL_WAITING_RESULT) || (client_protocol->state == MYSQL_IDLE)) {
		struct epoll_event new_event;
		int w;
		int count_reads = 0;
		int count_writes = 0;
		uint8_t buffer[MAX_BUFFER_SIZE];
		int b = -1;
		int tot_b = -1;
		uint8_t *ptr_buffer;

		if (ioctl(dcb->fd, FIONREAD, &b)) {
			fprintf(stderr, "Backend Ioctl FIONREAD error %i, %s\n", errno , strerror(errno));
		} else {
			fprintf(stderr, "Backend IOCTL FIONREAD bytes to read = %i\n", b);
		}

		// detect pending data in the buffer for client write
		if (dcb->session->client->buff_bytes > 0) {

			fprintf(stderr, "*********** Read backend says there are pending writes for %i bytes\n", dcb->session->client->buff_bytes);
			// read data from socket, assume no error here (quick) and put it into the DCB second buffer
			GW_NOINTR_CALL(n = read(dcb->fd, buffer, b < MAX_BUFFER_SIZE ? b : MAX_BUFFER_SIZE); count_reads++);

			memcpy(&dcb->session->client->second_buff_bytes, &n, sizeof(int));
			fprintf(stderr, "#### second buff_bytes set to %i\n", dcb->session->client->second_buff_bytes);

			memcpy(dcb->session->client->second_buffer, buffer, n);
			fprintf(stderr, "#### filled memory second buffer!\n");
			dcb->session->client->second_buffer_ptr = dcb->session->client->second_buffer;

			return 1;
		}

		// else, no pending data
		tot_b = b;
	
		// read all the data, without using multiple buffers, only one
		while (b > 0) {

			GW_NOINTR_CALL(n = read(dcb->fd, buffer, b < MAX_BUFFER_SIZE ? b : MAX_BUFFER_SIZE); count_reads++);

			fprintf (stderr, "Read %i/%i bytes done, n. reads = %i, %i. Next could be write\n", n, b, count_reads, errno);

			if (n < 0) {
				if ((errno != EAGAIN) || (errno != EWOULDBLOCK))
					return 0;
				else
					return 1;
			} else {
				b = b - n;
			}
		}

		ptr_buffer = buffer;
	

		if(n >2)
			fprintf(stderr, ">>> The READ BUFFER last 2 byte [%i][%i]\n", buffer[n-2], buffer[n-1]);


		// write all the data
		// if write fails for EAGAIN or EWOULDBLOCK, copy the data into the dcb buffer
		while (n >0) {
				GW_NOINTR_CALL(w = write(dcb->session->client->fd, ptr_buffer, n); count_writes++);

				fprintf (stderr, "Write Cycle %i,  %i of %i bytes done, errno %i\n", count_writes, w, n, errno);
				if (w > 2)
					fprintf(stderr, "<<< writed BUFFER last 2 byte [%i][%i]\n", ptr_buffer[w-2], ptr_buffer[w-1]);

				if (w < 0) {
					if ((errno != EAGAIN) || (errno != EWOULDBLOCK)) {
						break;
					} else {
						fprintf(stderr, "<<<< Write to client not completed for %i bytes!\n", n);
						if (dcb->session->client) {
							fprintf(stderr, "<<<< Try to set buff_bytes!\n");
							memcpy(&dcb->session->client->buff_bytes, &n, sizeof(int));
							fprintf(stderr, "<<<< buff_bytes set to %i\n", dcb->session->client->buff_bytes);

							fprintf(stderr, "<<<< Try to fill memory buffer!\n");
							memcpy(dcb->session->client->buffer, ptr_buffer, n);
							dcb->session->client->buffer_ptr = dcb->session->client->buffer;

							fprintf(stderr, "<<<< Buffer Write to client has %i bytes\n", dcb->session->client->buff_bytes);
							if (n > 1) {
							fprintf(stderr, "<<<< Buffer bytes last 2 [%i]\n", ptr_buffer[0]);
							fprintf(stderr, "<<<< Buffer bytes last [%i]\n", ptr_buffer[1]);
							}
						} else {
							fprintf(stderr, "<<< do data in memory for Buffer Write to client\n");
						}
						return 0;
					}
				} else {
					n = n - w;
					ptr_buffer = ptr_buffer + w;
					fprintf(stderr, "<<< Write: pointer to buffer %i bytes shifted\n", w);

					memset(&dcb->session->client->buff_bytes, '\0', sizeof(int));
				}
			}
		return 1;
	}
#ifdef GW_DEBUG_READ_EVENT
	fprintf(stderr, "The backend says that Client Protocol state is %i\n", client_protocol->state);
#endif

	return 1;
}

//////////////////////////////////////////
//backend write event triggered by EPOLLOUT
//////////////////////////////////////////

int gw_write_backend_event(DCB *dcb, int epfd) {

	fprintf(stderr, ">>> gw_write_backend_event for %i\n", dcb->fd);

	return 0;
}

//////////////////////////////////////////
//client read event triggered by EPOLLIN
//////////////////////////////////////////
int gw_route_read_event(DCB* dcb, int epfd) {
	MySQLProtocol *protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
	uint8_t buffer[MAX_BUFFER_SIZE] = "";
	int n;
	int b = -1;


	if (ioctl(dcb->fd, FIONREAD, &b)) {
		fprintf(stderr, "Client Ioctl FIONREAD error %i, %s\n", errno , strerror(errno));
	} else {
		fprintf(stderr, "Client IOCTL FIONREAD bytes to read = %i\n", b);
	}

//#ifdef GW_DEBUG_READ_EVENT
	fprintf(stderr, "Client DCB [%s], EPOLLIN Protocol state [%i] for socket %i, scramble [%s]\n", gw_dcb_state2string(dcb->state), protocol->state, dcb->fd, protocol->scramble);
//#endif
	switch (protocol->state) {
		case MYSQL_AUTH_SENT:
			// read client auth
			n = read(dcb->fd, buffer, MAX_BUFFER_SIZE);

			fprintf(stderr, "Client DCB [%s], EPOLLIN Protocol state [%i] for socket %i, bytes read %i\n", gw_dcb_state2string(dcb->state), protocol->state, dcb->fd, n);

			if (n < 0) {
				if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
					fprintf(stderr, "Client connection %i: continue for %i, %s\n", dcb->fd, errno, strerror(errno));
					break;
				} else {
				
					fprintf(stderr, "Client connection %i error: %i, %s\n", dcb->fd, errno, strerror(errno));;

					if (dcb->session->backends) {
						(dcb->session->backends->func).error(dcb->session->backends, -1);
					}
					(dcb->func).error(dcb, -1);
					
					break;
				}
			}

			if (n == 0) {
				// EOF
				fprintf(stderr, "Client connection %i closed: %i, %s\n", dcb->fd, errno, strerror(errno));

				if (dcb->session->backends) {
					(dcb->session->backends->func).error(dcb->session->backends, -1);
				}
				(dcb->func).error(dcb, -1);

				return 1;
			}
		
			// handle possible errors:
			// 0 connection close
			// -1, error: not EAGAIN or EWOULDBLOCK

			protocol->state = MYSQL_AUTH_RECV;


#ifdef GW_DEBUG_READ_EVENT
			fprintf(stderr, "DCB [%i], EPOLLIN Protocol next state MYSQL_AUTH_RECV [%i], Packet #%i for socket %i, scramble [%s]\n", dcb->state, dcb->proto_state, 1, dcb->fd, protocol->scramble);
#endif

			// check authentication
			// if OK return mysql_ok
			// else return error
			//protocol->state = MYSQL_AUTH_FAILED;
			
			break;

		case MYSQL_IDLE:
		case MYSQL_WAITING_RESULT:
			n = read(dcb->fd, buffer, MAX_BUFFER_SIZE);
			if (n < 0) {
				if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
					fprintf(stderr, "WAITING RESULT connection %i: continue for %i, %s\n", dcb->fd, errno, strerror(errno));
					break;
				} else {
				
					fprintf(stderr, "connection %i error: %i, %s\n", dcb->fd, errno, strerror(errno));;

					(dcb->session->backends->func).error(dcb->session->backends, -1);
					(dcb->func).error(dcb, -1);
					
					return 1;
				}
			}

			if (n == 0) {
				fprintf(stderr, "connection %i closed: %i, %s\n", dcb->fd, errno, strerror(errno));
				if (dcb->session->backends) {
					(dcb->session->backends->func).error(dcb->session->backends, -1);
				}
				(dcb->func).error(dcb, -1);

				return 1;
			}

#ifdef GW_DEBUG_READ_EVENT
			fprintf(stderr, "Client DCB [%s], EPOLLIN Protocol state [%i] for fd %i has read %i bytes\n", gw_dcb_state2string(dcb->state), protocol->state, dcb->fd, n);
#endif

			if (buffer[4] == '\x01') {
					fprintf(stderr, "COM_QUIT received\n");
					if (dcb->session->backends) {
						write(dcb->session->backends->fd, buffer, n);
						(dcb->session->backends->func).error(dcb->session->backends, -1);
					}
					(dcb->func).error(dcb, -1);
				
					return 1;
			}
#ifdef GW_DEBUG_READ_EVENT
			fprintf(stderr, "DCB [%i], EPOLLIN Protocol next state MYSQL_ROUTING [%i], Packet #%i for socket %i, scramble [%s]\n", dcb->state, protocol->state, 1, dcb->fd, protocol->scramble);
#endif
			protocol->state = MYSQL_ROUTING;

			write(dcb->session->backends->fd, buffer, n);
#ifdef GW_DEBUG_READ_EVENT
			fprintf(stderr, "Client %i, has written to backend %i, btytes %i [%s]\n", dcb->fd, dcb->session->backends->fd, n, buffer);
#endif
			protocol->state = MYSQL_WAITING_RESULT;

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
int gw_handle_write_event(DCB *dcb, int epfd) {
	MySQLProtocol *protocol = NULL;
	int n;
        struct epoll_event new_event;
        n = dcb->buff_bytes;


	if (dcb == NULL) {
		fprintf(stderr, "DCB is NULL, return\n");
		return 1;
	}

	fprintf(stderr, "DCB is ok, continue state [%i] is [%s]\n", dcb->state, gw_dcb_state2string(dcb->state));

	if (dcb->state == DCB_STATE_DISCONNECTED) {
		return 1;
	}

	fprintf(stderr, "DCB is connected, continue\n");

	if (dcb->protocol) {
		fprintf(stderr, "DCB protocol is OK, continue\n");
		protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
	} else {
		fprintf(stderr, "DCB protocol is NULL, return\n");
		return 1;
	}

	if (dcb->session) {
		fprintf(stderr, "DCB session is OK, continue\n");
	} else {
		fprintf(stderr, "DCB session is NULL, return\n");
		return 1;
	}

	if (dcb->session->backends) {
		fprintf(stderr, "DCB backend is OK, continue\n");
	} else {
		fprintf(stderr, "DCB backend is NULL, continue\n");
	}

	if (dcb->session->backends) {
		fprintf(stderr, "CLIENT WRITE READY State [%i], FIRST bytes left to write %i from back %i to client %i\n", dcb->state, dcb->buff_bytes, dcb->session->backends->fd, dcb->fd);
		fprintf(stderr, "CLIENT WRITE READY, SECOND bytes left to write %i from back %i to client %i\n", dcb->second_buff_bytes, dcb->session->backends->fd, dcb->fd);
	}

//#ifdef GW_DEBUG_WRITE_EVENT
	fprintf(stderr, "$$$$$ DCB [%i], EPOLLOUT Protocol state is [%i], Packet #%i for socket %i, scramble [%s]\n", dcb->state, protocol->state, 1, dcb->fd, protocol->scramble);
//#endif

	if(protocol->state == MYSQL_AUTH_RECV) {

        	//write to client mysql AUTH_OK packet, packet n. is 2
		mysql_send_ok(dcb->fd, 2, 0, NULL);

        	protocol->state = MYSQL_IDLE;

//#ifdef GW_DEBUG_WRITE_EVENT
		fprintf(stderr, "DCB [%i], EPOLLIN Protocol next state MYSQL_IDLE [%i], Packet #%i for socket %i, scramble [%s]\n", dcb->state, protocol->state, 2, dcb->fd, protocol->scramble);
//#endif

		return 0;
	}

	if (protocol->state == MYSQL_AUTH_FAILED) {
		// still to implement
		mysql_send_ok(dcb->fd, 2, 0, NULL);

		return 0;
	}

	if ((protocol->state == MYSQL_IDLE) || (protocol->state == MYSQL_WAITING_RESULT)) {
		int w;
		int m;

		if (dcb->buff_bytes > 0) {
			fprintf(stderr, "<<< Writing unsent data for state [%i], bytes %i\n", protocol->state, dcb->buff_bytes);

			if (dcb->buff_bytes > 2)
				fprintf(stderr, "READ BUFFER last 2 byte [%i%i]\n", dcb->buffer[dcb->buff_bytes-2], dcb->buffer[dcb->buff_bytes-1]);

			w = write(dcb->fd, dcb->buffer_ptr, dcb->buff_bytes);

			if (w < 0) {
				if ((w != EAGAIN) || (w!= EWOULDBLOCK)) {
					return 1;
				} else
					return 0;
			}
			fprintf(stderr, "<<<<< Written %i bytes, left %i\n", w, dcb->buff_bytes - w);
			n = n-w;
			memcpy(&dcb->buff_bytes, &n, sizeof(int));

			dcb->buffer_ptr = dcb->buffer_ptr + w;

			fprintf(stderr, "<<<<< Next time write %i bytes, buffer_ptr is %i bytes shifted\n", n, w);
		} else {
			fprintf(stderr, "<<<< Nothing to do with FIRST buffer left bytes\n");
		}
		m = dcb->second_buff_bytes;

		if (dcb->second_buff_bytes) {
			fprintf(stderr, "<<<< Now use the SECOND buffer left %i bytes\n", m);
			w = write(dcb->fd, dcb->second_buffer_ptr, dcb->second_buff_bytes);
			if (w < 0) {
				if ((w != EAGAIN) || (w!= EWOULDBLOCK)) {
					return 1;
				} else
					return 0;
			}
			fprintf(stderr, "<<<<< second Written %i bytes, left %i\n", w, dcb->second_buff_bytes - w);
			m = m-w;
			memcpy(&dcb->second_buff_bytes, &m, sizeof(int));

			dcb->second_buffer_ptr = dcb->second_buffer_ptr + w;
		}

		fprintf(stderr, "<<<< Nothing to do with SECOND buffer left bytes\n");

		return 1;
	}

//#ifdef GW_DEBUG_WRITE_EVENT
	fprintf(stderr, "$$$$$ DCB [%i], EPOLLOUT Protocol state is [%i] did nothing !!!!\n", dcb->state, protocol->state);
//#endif

	return 1;
}

///
// set listener for mysql protocol
///
void MySQLListener(int epfd, char *config_bind) {
	DCB *listener;
	int l_so;
	int fl;
	struct sockaddr_in serv_addr;
	struct sockaddr_in local;
	socklen_t addrlen;
	char *bind_address_and_port = NULL;
	char *p;
	char address[1024]="";
	int port=0;
	int one;
	struct epoll_event ev;

	// this gateway, as default, will bind on port 4404 for localhost only
	(config_bind != NULL) ? (bind_address_and_port = config_bind) : (bind_address_and_port = "127.0.0.1:4404");

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

	// register events, don't add EPOLLET for now
	ev.events = EPOLLIN;

	// set user data to dcb struct
        ev.data.ptr = listener;

        // add listening socket to epoll structure
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, l_so, &ev) == -1) {
                perror("epoll_ctl: listen_sock");
                exit(EXIT_FAILURE);
        }

	listener->func.accept = MySQLAccept;

	listener->state = DCB_STATE_LISTENING;
}


int MySQLAccept(DCB *listener, int efd) {
	int accept_counter = 0;

	fprintf(stderr, "MySQL Listener socket is: %i\n", listener->fd);

	while (1) {
		int c_sock;
		struct sockaddr_in local;
		socklen_t addrlen;
		addrlen = sizeof(local);
		struct epoll_event ee;
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

		accept_counter++;

		fprintf(stderr, "Processing %i connection fd %i for listener %i\n", accept_counter, c_sock, listener->fd);
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

		// edge triggering flag added
		ee.events = EPOLLIN | EPOLLET | EPOLLOUT;
		ee.data.ptr = backend;
	
		// if connected, add it to the epoll
		if (backend->fd > 0) {
			if (epoll_ctl(efd, EPOLL_CTL_ADD, backend->fd, &ee) == -1) {
				perror("epoll_ctl: backend sock");
			} else {
				//fprintf(stderr, "--> Backend conn added, bk_fd [%i], scramble [%s], is session with client_fd [%i]\n", ptr_proto->fd, ptr_proto->scramble, client->fd);
				backend->state = DCB_STATE_POLLING;
				backend->session = session;
				(backend->func).read = gw_read_backend_event;
				(backend->func).write = gw_write_backend_event;
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
		(client->func).write = gw_handle_write_event;

		// edge triggering flag added
		ee.events = EPOLLIN | EPOLLOUT | EPOLLET;
		ee.data.ptr = client;

		client->state = DCB_STATE_IDLE;

		// event install
		if (epoll_ctl(efd, EPOLL_CTL_ADD, c_sock, &ee) == -1) {
			perror("epoll_ctl: conn_sock");
			exit(EXIT_FAILURE);
		} else {
			//fprintf(stderr, "Added fd %i to epoll, protocol state [%i]\n", c_sock , client->state);
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
	srand(time());

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
		register char tmp_ptr = char_val(*in++);
		*out++= (tmp_ptr << 4) | char_val(*in++);
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
