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
extern char *gw_strend(register const char *s);
int gw_mysql_connect(char *host, int port, char *dbname, char *user, uint8_t *passwd, MySQLProtocol *conn);
static int gw_create_backend_connection(DCB *client_dcb, SERVER *server, SESSION *in_session);
static int gw_read_backend_event(DCB* dcb);
static int gw_write_backend_event(DCB *dcb);
static int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
static int gw_error_backend_event(DCB *dcb);
static int gw_backend_close(DCB *dcb);

static GWPROTOCOL MyObject = { 
	gw_read_backend_event,			/* Read - EPOLLIN handler	 */
	gw_MySQLWrite_backend,			/* Write - data from gateway	 */
	gw_write_backend_event,			/* WriteReady - EPOLLOUT handler */
	gw_error_backend_event,			/* Error - EPOLLERR handler	 */
	NULL,					/* HangUp - EPOLLHUP handler	 */
	NULL,					/* Accept			 */
	gw_create_backend_connection,		/* Connect			 */
	gw_backend_close,			/* Close			 */
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
static int gw_read_backend_event(DCB *dcb) {
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

	return 0;
}

//////////////////////////////////////////
//backend write event triggered by EPOLLOUT
//////////////////////////////////////////
static int gw_write_backend_event(DCB *dcb) {
	//fprintf(stderr, ">>> gw_write_backend_event for %i\n", dcb->fd);
        return 0;
}

/*
 * Write function for backend DCB
 *
 * @param dcb	The DCB of the client
 * @param queue	Queue of buffers to write
 */
static int
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

static int gw_error_backend_event(DCB *dcb) {

        fprintf(stderr, "#### Handle Backend error function for %i\n", dcb->fd);

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

	return 1;
}

/*
 * Create a new MySQL backend connection.
 *
 * This routine performs the MySQL connection to the backend and fills the session->backends of the callier dcb
 * with the new allocatetd dcb and adds the new socket to the poll set
 *
 * - backend dcb allocation
 * - MySQL session data fetch
 * - backend connection using data in MySQL session
 *
 * @param client_dcb The client DCB struct
 * @return 0 on Success or 1 on Failure.
 */

static int gw_create_backend_connection(DCB *backend, SERVER *server, SESSION *session) {
	MySQLProtocol *ptr_proto = NULL;
	MYSQL_session *s_data = NULL;

	fprintf(stderr, "HERE, the server to connect is [%s]:[%i]\n", server->name, server->port);

	backend->protocol = (MySQLProtocol *) calloc(1, sizeof(MySQLProtocol));

	ptr_proto = (MySQLProtocol *)backend->protocol;
	s_data = (MYSQL_session *)session->client->data;

//	fprintf(stderr, "HERE before connect, s_data is [%p]\n", s_data);
//	fprintf(stderr, "HERE before connect, username is [%s]\n", s_data->user);

	// this is blocking until auth done
	if (gw_mysql_connect(server->name, server->port, s_data->db, s_data->user, s_data->client_sha1, backend->protocol) == 0) {
		memcpy(&backend->fd,  &ptr_proto->fd, sizeof(backend->fd));

		setnonblocking(backend->fd);
		fprintf(stderr, "Connected to backend mysql server. fd is %i\n", backend->fd);
	} else {
		fprintf(stderr, "<<<< NOT Connected to backend mysql server!!!\n");
		backend->fd = -1;
		return -1;
	}

	// if connected, it will be addeed to the epoll from the caller of connect()

	if (backend->fd <= 0) {
		perror("ERROR: epoll_ctl: backend sock");
		backend->fd = -1;
		return -1;
	} else {
		fprintf(stderr, "--> Backend conn added, bk_fd [%i], scramble [%s], is session with client_fd [%i]\n", backend->fd, ptr_proto->scramble, session->client->fd);
		backend->state = DCB_STATE_POLLING;

		return backend->fd;
	}
	return -1;
}

static int
gw_backend_close(DCB *dcb)
{
        dcb_close(dcb);
	return 1;
}

/*
 * Create a new MySQL connection.
 *
 * This routine performs the full MySQL connection to the specified server.
 * It does 
 * - socket init
 * - socket connect
 * - server handshake parsing
 * - authenticatio reply
 * - the Auth ack receive
 * 
 * Please note, all socket operation are in blocking state
 * Status: work in progress.
 *
 * @param host The TCP/IP host address to connect to
 * @param port The TCP/IP host port to connect to
 * @param dbname The optional database name. Use NULL if not interested in
 * @param user The MySQL database Username: required
 * @param passwd The MySQL database Password: required
 * @param conn The MySQLProtocol structure to be filled: must be preallocated with gw_mysql_init()
 * @return 0 on Success or 1 on Failure.
 */
int gw_mysql_connect(char *host, int port, char *dbname, char *user, uint8_t *passwd, MySQLProtocol *conn) {

        struct sockaddr_in serv_addr;
	int compress = 0;
	int rv;
	int so = 0;
	int ciclo = 0;
	uint8_t buffer[SMALL_CHUNK];
	uint8_t packet_buffer[SMALL_CHUNK];
	uint8_t *payload = NULL;
	int server_protocol;
	uint8_t *server_version_end = NULL;
	uint16_t mysql_server_capabilities_one;
	uint16_t mysql_server_capabilities_two;
	unsigned long tid =0;
	long bytes;
	uint8_t scramble_data_1[8 + 1] = "";
	uint8_t scramble_data_2[12 + 1] = "";
	uint8_t capab_ptr[4];
	int scramble_len;
	uint8_t scramble[GW_MYSQL_SCRAMBLE_SIZE + 1];
	uint8_t client_scramble[GW_MYSQL_SCRAMBLE_SIZE + 1];
	uint8_t client_capabilities[4];
	uint32_t server_capabilities;
	uint32_t final_capabilities;
	char dbpass[129]="";

	char *curr_db = NULL;
	uint8_t *curr_passwd = NULL;
	
	if (strlen(dbname))
		curr_db = dbname;

	if (strlen((char *)passwd))
		curr_passwd = passwd;

	conn->state = MYSQL_ALLOC;
	conn->fd = -1;

#ifdef MYSQL_CONN_DEBUG
	//fprintf(stderr, ")))) Connect to MySQL: user[%s], SHA1(passwd)[%s], db [%s]\n", user, passwd, dbname);
#endif

        memset(&serv_addr, 0, sizeof serv_addr);
        serv_addr.sin_family = AF_INET;

	so = socket(AF_INET,SOCK_STREAM,0);
	if (so < 0) {
		fprintf(stderr, "Errore creazione socket: [%s] %i\n", strerror(errno), errno);
		return 1;
	}

	conn->fd = so;

	setipaddress(&serv_addr.sin_addr, host);
	serv_addr.sin_port = htons(port);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Socket initialized\n");
	fflush(stderr);
#endif

	while(1) {
		if ((rv = connect(so, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0) {
			fprintf(stderr, "Errore connect %i, %s: RV = [%i]\n", errno, strerror(errno), rv);
			
			if (errno == EINPROGRESS) {
				continue;
			} else {
				close(so);
				return -1;
			}
		} else {
			break;
		}
	}

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "CONNECT is DONE\n");
	fprintf(stderr, "Socket FD is %i\n", so);
	fflush(stderr);
#endif


	memset(&buffer, '\0', sizeof(buffer));

	bytes = SMALL_CHUNK;

	rv = read(so, buffer, bytes);

	if ( rv >0 ) {
#ifdef MYSQL_CONN_DEBUG
		fprintf(stderr, "RESPONSE ciclo %i HO letto [%s] bytes %li\n",ciclo, buffer, bytes);
		fflush(stderr);
#endif
		ciclo++;
	} else {
		if (rv == 0 && errno == EOF) {
#ifdef MYSQL_CONN_DEBUG
			fprintf(stderr, "EOF reached. Bytes = %li\n", bytes);
			fflush(stderr);
#endif
		} else {
#ifdef MYSQL_CONN_DEBUG
			fprintf(stderr, "###### Receive error FINAL : connection not completed %i %s:  RV = [%i]\n", errno, strerror(errno), rv);
#endif
			close(so);

			return -1;
		}
	}

#ifdef MYSQL_CONN_DEBUG
	fwrite(buffer, bytes, 1, stderr);
	fflush(stderr);
#endif

	//decode mysql handshake

	payload = buffer + 4;
	server_protocol= payload[0];

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Server Protocol [%i]\n", server_protocol);

#endif
	payload++;

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Protocol Version [%s]\n", payload);
	fflush(stderr);
#endif

	server_version_end = (uint8_t *) gw_strend((char*) payload);
	payload = server_version_end + 1;

	// TID
	tid = gw_mysql_get_byte4(payload);
	memcpy(&conn->tid, &tid, 4);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Thread ID is %lu\n", conn->tid);
	fflush(stderr);
#endif

	payload +=4;

	// scramble_part 1
	memcpy(scramble_data_1, payload, 8);
	payload += 8;

	// 1 filler
	payload++;

	mysql_server_capabilities_one = gw_mysql_get_byte2(payload);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Capab_1[\n");
	fwrite(&mysql_server_capabilities_one, 2, 1, stderr);
	fflush(stderr);
#endif

	//2 capab_part 1 + 1 language + 2 server_status
	payload +=5;

	mysql_server_capabilities_two = gw_mysql_get_byte2(payload);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "]Capab_2[\n");
	fwrite(&mysql_server_capabilities_two, 2, 1, stderr);
	fprintf(stderr, "]\n");
	fflush(stderr);
#endif

	memcpy(&capab_ptr, &mysql_server_capabilities_one, 2);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Capab_1[\n");
	fwrite(capab_ptr, 2, 1, stderr);
	fflush(stderr);
#endif

	memcpy(&(capab_ptr[2]), &mysql_server_capabilities_two, 2);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Capab_2[\n");
	fwrite(capab_ptr, 2, 1, stderr);
	fflush(stderr);
#endif

	// 2 capab_part 2
	payload+=2;

	scramble_len = payload[0] -1;

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Scramble_len  [%i]\n", scramble_len);
	fflush(stderr);
#endif

	payload += 11;

	memcpy(scramble_data_2, payload, scramble_len - 8);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Scramble_buff1[");
	fwrite(scramble_data_1, 8, 1, stderr);
	fprintf(stderr, "]\nScramble_buff2  [");
	fwrite(scramble_data_2, scramble_len - 8, 1, stderr);
	fprintf(stderr, "]\n");
	fflush(stderr);
#endif

	memcpy(scramble, scramble_data_1, 8);
	memcpy(scramble + 8, scramble_data_2, scramble_len - 8);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Full Scramble 20 bytes is  [\n");
	fwrite(scramble, GW_MYSQL_SCRAMBLE_SIZE, 1, stderr);
	fprintf(stderr, "\n]\n");
	fflush(stderr);
#endif

	memcpy(conn->scramble, scramble, GW_MYSQL_SCRAMBLE_SIZE);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Scramble from MYSQL_Conn is  [\n");
	fwrite(scramble, GW_MYSQL_SCRAMBLE_SIZE, 1, stderr);
	fprintf(stderr, "\n]\n");
	fflush(stderr);
	fprintf(stderr, "Now sending user, pass & db\n[");
	fwrite(&server_capabilities, 4, 1, stderr);
	fprintf(stderr, "]\n");
#endif

	final_capabilities = gw_mysql_get_byte4((uint8_t *)&server_capabilities);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "CAPABS [%u]\n", final_capabilities);
	fflush(stderr);
#endif
	memset(packet_buffer, '\0', sizeof(packet_buffer));
	//packet_header(byte3 +1 pack#)
	packet_buffer[3] = '\x01';

	final_capabilities |= GW_MYSQL_CAPABILITIES_PROTOCOL_41;
	final_capabilities |= GW_MYSQL_CAPABILITIES_CLIENT;
	if (compress) {
		final_capabilities |= GW_MYSQL_CAPABILITIES_COMPRESS;
		fprintf(stderr, "Backend Connection with compression\n");
		fflush(stderr);
	}

	if (curr_passwd != NULL) {
		uint8_t hash1[GW_MYSQL_SCRAMBLE_SIZE]="";
		uint8_t hash2[GW_MYSQL_SCRAMBLE_SIZE]="";
		uint8_t new_sha[GW_MYSQL_SCRAMBLE_SIZE]="";


		memcpy(hash1, passwd, GW_MYSQL_SCRAMBLE_SIZE);
		gw_sha1_str(hash1, GW_MYSQL_SCRAMBLE_SIZE, hash2);
		gw_bin2hex(dbpass, hash2, GW_MYSQL_SCRAMBLE_SIZE);
		gw_sha1_2_str(scramble, GW_MYSQL_SCRAMBLE_SIZE, hash2, GW_MYSQL_SCRAMBLE_SIZE, new_sha);
		gw_str_xor(client_scramble, new_sha, hash1, GW_MYSQL_SCRAMBLE_SIZE);

#ifdef MYSQL_CONN_DEBUG
		fprintf(stderr, "Hash1 [%s]\n", hash1);
		fprintf(stderr, "Hash2 [%s]\n", hash2);
		fprintf(stderr, "SHA1(SHA1(password in hex)\n");
		fprintf(stderr, "PAss [%s]\n", dbpass);
		fflush(stderr);
		fprintf(stderr, "newsha [%s]\n", new_sha);
		fprintf(stderr, "Client send scramble 20 [\n");
		fwrite(client_scramble, GW_MYSQL_SCRAMBLE_SIZE, 1, stderr);
		fprintf(stderr, "\n]\n");
		fflush(stderr);
#endif
	}

	if (curr_db == NULL) {
		// now without db!!
		final_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
	} else {
		final_capabilities |= GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
	}

	payload = packet_buffer + 4;

	final_capabilities |= GW_MYSQL_CAPABILITIES_PLUGIN_AUTH;

	gw_mysql_set_byte4(client_capabilities, final_capabilities);
	memcpy(payload, client_capabilities, 4);

	//packet_buffer[4] = '\x8d';
	//packet_buffer[5] = '\xa6';
	//packet_buffer[6] = '\x0f';
	//packet_buffer[7] = '\x00';

	// set now the max-packet size
	payload += 4;
	gw_mysql_set_byte4(payload, 16777216);

	// set the charset
	payload += 4;
	*payload = '\x08';

	payload++;

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "User is [%s]\n", user);
	fflush(stderr);
#endif

	
        // 4 + 4 + 4 + 1 + 23 = 36
	payload += 23;
	memcpy(payload, user, strlen(user));

        // 4 + 4 + 1 + 23  = 32 + 1 (scramble_len) + 20 (fixed_scramble) + 1 (user NULL term) + 1 (db NULL term) = 55
	bytes = 32;

	bytes += strlen(user);
	// the NULL
	bytes++;

	payload += strlen(user);
	payload++;

	if (curr_passwd != NULL) {
		// set the auth-length
		*payload = GW_MYSQL_SCRAMBLE_SIZE;
		payload++;
		bytes++;
	
		//copy the 20 bytes scramble data after packet_buffer+36+user+NULL+1 (byte of auth-length)
		memcpy(payload, client_scramble, GW_MYSQL_SCRAMBLE_SIZE);
		
		payload += GW_MYSQL_SCRAMBLE_SIZE;
		bytes += GW_MYSQL_SCRAMBLE_SIZE;

	} else {
		// skip the auth-length and write a NULL
		payload++;
		bytes++;
	}

	// if the db is not NULL append it
	if (curr_db) {
		memcpy(payload, curr_db, strlen(curr_db));
		payload += strlen(curr_db);
		payload++;
		bytes += strlen(curr_db);
		// the NULL
		bytes++;
	}

	memcpy(payload, "mysql_native_password", strlen("mysql_native_password"));

	payload += strlen("mysql_native_password");
	payload++;

	bytes +=strlen("mysql_native_password");
	bytes++;

	gw_mysql_set_byte3(packet_buffer, bytes);

	// the packet header
	bytes += 4;

	rv = write(so, packet_buffer, bytes);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Sent [%s], [%i] bytes\n", packet_buffer, bytes);
	fflush(stderr);
#endif

	if (rv == -1) {
		fprintf(stderr, "CONNECT Error in send auth\n");
	}

	bytes = SMALL_CHUNK;

	memset(buffer, '\0', sizeof (buffer));

	rv = read(so, buffer, SMALL_CHUNK);

	if (rv == -1) {
		fprintf(stderr, "CONNCET Error in recv OK for auth\n");
	}

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "ok packet\[");
	fwrite(buffer, bytes, 1, stderr);
	fprintf(stderr, "]\n");
	fflush(stderr);
#endif
	if (buffer[4] == '\x00') {
#ifdef MYSQL_CONN_DEBUG
		fprintf(stderr, "OK packet received, packet # %i\n", buffer[3]);
		fflush(stderr);
#endif
		conn->state = MYSQL_IDLE;

		return 0;
	} else {

		close(so);
	}

	return 1;

}

