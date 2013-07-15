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
 * @file mysql_client.c
 *
 * MySQL Protocol module for handling the protocol between the gateway
 * and the client.
 *
 * Revision History
 * Date		Who			Description
 * 14/06/2013	Mark Riddoch		Initial version
 * 17/06/2013	Massimiliano Pinto	Added Client To Gateway routines
 * 24/06/2013	Massimiliano Pinto	Added: fetch passwords from service users' hashtable
 */

#include <mysql_client_server_protocol.h>

static char *version_str = "V1.0.0";

static int gw_MySQLAccept(DCB *listener);
static int gw_MySQLListener(DCB *listener, char *config_bind);
static int gw_read_client_event(DCB* dcb);
static int gw_write_client_event(DCB *dcb);
static int gw_MySQLWrite_client(DCB *dcb, GWBUF *queue);
static int gw_error_client_event(DCB *dcb);
static int gw_client_close(DCB *dcb);
static int gw_client_hangup_event(DCB *dcb);

int mysql_send_ok(DCB *dcb, int packet_number, int in_affected_rows, const char* mysql_message);
int MySQLSendHandshake(DCB* dcb);
static int gw_mysql_do_authentication(DCB *dcb, GWBUF *queue);

/*
 * The "module object" for the mysqld client protocol module.
 */
static GWPROTOCOL MyObject = { 
	gw_read_client_event,			/* Read - EPOLLIN handler	 */
	gw_MySQLWrite_client,			/* Write - data from gateway	 */
	gw_write_client_event,			/* WriteReady - EPOLLOUT handler */
	gw_error_client_event,			/* Error - EPOLLERR handler	 */
	gw_client_hangup_event,			/* HangUp - EPOLLHUP handler	 */
	gw_MySQLAccept,				/* Accept			 */
	NULL,					/* Connect			 */
	gw_client_close,			/* Close			 */
	gw_MySQLListener,			/* Listen			 */
	NULL,					/* Authentication		 */
	NULL					/* Generic			 */
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
	fprintf(stderr, "Initial MySQL Client Protcol module.\n");
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
 * mysql_send_ok
 *
 * Send a MySQL protocol OK message to the dcb (client)
 *
 * @param dcb Descriptor Control Block for the connection to which the OK is sent
 * @param packet_number
 * @param in_affected_rows
 * @param mysql_message
 * @return packet length
 *
 */
int
mysql_send_ok(DCB *dcb, int packet_number, int in_affected_rows, const char* mysql_message) {
        uint8_t *outbuf = NULL;
        uint8_t mysql_payload_size = 0;
        uint8_t mysql_packet_header[4];
        uint8_t *mysql_payload = NULL;
        uint8_t field_count = 0;
        uint8_t affected_rows = 0;
        uint8_t insert_id = 0;
        uint8_t mysql_server_status[2];
        uint8_t mysql_warning_count[2];
	GWBUF	*buf;

        affected_rows = in_affected_rows;
	
	mysql_payload_size = sizeof(field_count) + sizeof(affected_rows) + sizeof(insert_id) + sizeof(mysql_server_status) + sizeof(mysql_warning_count);

        if (mysql_message != NULL) {
                mysql_payload_size += strlen(mysql_message);
        }

        // allocate memory for packet header + payload
        if ((buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size)) == NULL)
	{
		return 0;
	}
	outbuf = GWBUF_DATA(buf);

        // write packet header with packet number
        gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
        mysql_packet_header[3] = packet_number;

        // write header
        memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

        mysql_payload = outbuf + sizeof(mysql_packet_header);

        mysql_server_status[0] = 2;
        mysql_server_status[1] = 0;
        mysql_warning_count[0] = 0;
        mysql_warning_count[1] = 0;

        // write data
        memcpy(mysql_payload, &field_count, sizeof(field_count));
        mysql_payload = mysql_payload + sizeof(field_count);

        memcpy(mysql_payload, &affected_rows, sizeof(affected_rows));
        mysql_payload = mysql_payload + sizeof(affected_rows);

        memcpy(mysql_payload, &insert_id, sizeof(insert_id));
        mysql_payload = mysql_payload + sizeof(insert_id);

        memcpy(mysql_payload, mysql_server_status, sizeof(mysql_server_status));
        mysql_payload = mysql_payload + sizeof(mysql_server_status);

        memcpy(mysql_payload, mysql_warning_count, sizeof(mysql_warning_count));
        mysql_payload = mysql_payload + sizeof(mysql_warning_count);

        if (mysql_message != NULL) {
                memcpy(mysql_payload, mysql_message, strlen(mysql_message));
        }

	// writing data in the Client buffer queue
	dcb->func.write(dcb, buf);

	return sizeof(mysql_packet_header) + mysql_payload_size;
}

/**
 * MySQLSendHandshake
 *
 * @param dcb The descriptor control block to use for sending the handshake request
 * @return	The packet length sent
 */
int
MySQLSendHandshake(DCB* dcb)
{
        uint8_t *outbuf = NULL;
        uint8_t mysql_payload_size = 0;
        uint8_t mysql_packet_header[4];
        uint8_t mysql_packet_id = 0;
        uint8_t mysql_filler = GW_MYSQL_HANDSHAKE_FILLER;
        uint8_t mysql_protocol_version = GW_MYSQL_PROTOCOL_VERSION;
        uint8_t *mysql_handshake_payload = NULL;
        uint8_t mysql_thread_id[4];
        uint8_t mysql_scramble_buf[9] = "";
        uint8_t mysql_plugin_data[13] = "";
        uint8_t mysql_server_capabilities_one[2];
        uint8_t mysql_server_capabilities_two[2];
        uint8_t mysql_server_language = 8;
        uint8_t mysql_server_status[2];
        uint8_t mysql_scramble_len = 21;
        uint8_t mysql_filler_ten[10];
        uint8_t mysql_last_byte = 0x00;
	char server_scramble[GW_MYSQL_SCRAMBLE_SIZE + 1]="";

	MySQLProtocol *protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
	GWBUF		*buf;

	gw_generate_random_str(server_scramble, GW_MYSQL_SCRAMBLE_SIZE);
	
	// copy back to the caller
	memcpy(protocol->scramble, server_scramble, GW_MYSQL_SCRAMBLE_SIZE);

	// fill the handshake packet

	memset(&mysql_filler_ten, 0x00, sizeof(mysql_filler_ten));

        // thread id, now put thePID
        gw_mysql_set_byte4(mysql_thread_id, getpid() + dcb->fd);
	
        memcpy(mysql_scramble_buf, server_scramble, 8);

        memcpy(mysql_plugin_data, server_scramble + 8, 12);

        mysql_payload_size  = sizeof(mysql_protocol_version) + (strlen(GW_MYSQL_VERSION) + 1) + sizeof(mysql_thread_id) + 8 + sizeof(mysql_filler) + sizeof(mysql_server_capabilities_one) + sizeof(mysql_server_language) + sizeof(mysql_server_status) + sizeof(mysql_server_capabilities_two) + sizeof(mysql_scramble_len) + sizeof(mysql_filler_ten) + 12 + sizeof(mysql_last_byte) + strlen("mysql_native_password") + sizeof(mysql_last_byte);

        // allocate memory for packet header + payload
        if ((buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size)) == NULL)
	{
		return 0;
	}
	outbuf = GWBUF_DATA(buf);

        // write packet heder with mysql_payload_size
        gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
        //mysql_packet_header[0] = mysql_payload_size;

        // write packent number, now is 0
        mysql_packet_header[3]= mysql_packet_id;
        memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

        // current buffer pointer
        mysql_handshake_payload = outbuf + sizeof(mysql_packet_header);

        // write protocol version
        memcpy(mysql_handshake_payload, &mysql_protocol_version, sizeof(mysql_protocol_version));
        mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_protocol_version);

        // write server version plus 0 filler
        strcpy((char *)mysql_handshake_payload, GW_MYSQL_VERSION);
        mysql_handshake_payload = mysql_handshake_payload + strlen(GW_MYSQL_VERSION);
        *mysql_handshake_payload = 0x00;

	mysql_handshake_payload++;

        // write thread id
        memcpy(mysql_handshake_payload, mysql_thread_id, sizeof(mysql_thread_id));
        mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_thread_id);

        // write scramble buf
        memcpy(mysql_handshake_payload, mysql_scramble_buf, 8);
        mysql_handshake_payload = mysql_handshake_payload + 8;
        *mysql_handshake_payload = GW_MYSQL_HANDSHAKE_FILLER;
        mysql_handshake_payload++;

        // write server capabilities part one
        mysql_server_capabilities_one[0] = GW_MYSQL_SERVER_CAPABILITIES_BYTE1;
        mysql_server_capabilities_one[1] = GW_MYSQL_SERVER_CAPABILITIES_BYTE2;


        mysql_server_capabilities_one[0] &= ~GW_MYSQL_CAPABILITIES_COMPRESS;
        mysql_server_capabilities_one[0] &= ~GW_MYSQL_CAPABILITIES_SSL;

        memcpy(mysql_handshake_payload, mysql_server_capabilities_one, sizeof(mysql_server_capabilities_one));
        mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_capabilities_one);

        // write server language
        memcpy(mysql_handshake_payload, &mysql_server_language, sizeof(mysql_server_language));
        mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_language);

        //write server status
        mysql_server_status[0] = 2;
        mysql_server_status[1] = 0;
        memcpy(mysql_handshake_payload, mysql_server_status, sizeof(mysql_server_status));
        mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_status);

        //write server capabilities part two
        mysql_server_capabilities_two[0] = 15;
        mysql_server_capabilities_two[1] = 128;

        memcpy(mysql_handshake_payload, mysql_server_capabilities_two, sizeof(mysql_server_capabilities_two));
        mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_server_capabilities_two);

        // write scramble_len
        memcpy(mysql_handshake_payload, &mysql_scramble_len, sizeof(mysql_scramble_len));
        mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_scramble_len);

        //write 10 filler
        memcpy(mysql_handshake_payload, mysql_filler_ten, sizeof(mysql_filler_ten));
        mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_filler_ten);

        // write plugin data
        memcpy(mysql_handshake_payload, mysql_plugin_data, 12);
        mysql_handshake_payload = mysql_handshake_payload + 12;

        //write last byte, 0
        *mysql_handshake_payload = 0x00;
        mysql_handshake_payload++;

        // to be understanded ????
        memcpy(mysql_handshake_payload, "mysql_native_password", strlen("mysql_native_password"));
        mysql_handshake_payload = mysql_handshake_payload + strlen("mysql_native_password");

        //write last byte, 0
        *mysql_handshake_payload = 0x00;

        mysql_handshake_payload++;

	// writing data in the Client buffer queue
	dcb->func.write(dcb, buf);

	return sizeof(mysql_packet_header) + mysql_payload_size;
}

/**
 * gw_mysql_do_authentication
 *
 * Performs the MySQL protocol 4.1 authentication, using data in GWBUF *queue
 *
 * The useful data: user, db, client_sha1 are copied into the MYSQL_session * dcb->session->data
 * client_capabilitiesa are copied into the dcb->protocol
 *
 * @param dcb Descriptor Control Block of the client
 * @param queue The GWBUF with data from client
 * @return 0 for Authentication ok, !=1 for failed autht
 *
 */

static int gw_mysql_do_authentication(DCB *dcb, GWBUF *queue) {
	MySQLProtocol *protocol = NULL;
	int compress = -1;
	int connect_with_db = -1;
	uint8_t *client_auth_packet = GWBUF_DATA(queue);
	char *username =  NULL;
	char *database = NULL;
	unsigned int auth_token_len = 0;
	uint8_t *auth_token = NULL;
	uint8_t *stage1_hash = NULL;
	int auth_ret = -1;
	MYSQL_session *client_data = NULL;

	if (dcb)
		protocol = DCB_PROTOCOL(dcb, MySQLProtocol);

	client_data = (MYSQL_session *)calloc(1, sizeof(MYSQL_session));
	dcb->data = client_data; 

	stage1_hash = client_data->client_sha1;
	username = client_data->user;

	memcpy(&protocol->client_capabilities, client_auth_packet + 4, 4);

	connect_with_db = GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB & gw_mysql_get_byte4(&protocol->client_capabilities);
	compress =  GW_MYSQL_CAPABILITIES_COMPRESS & gw_mysql_get_byte4(&protocol->client_capabilities);

	// now get the user
	strcpy(username,  (char *)(client_auth_packet + 4 + 4 + 4 + 1 + 23));
	fprintf(stderr, "<<< Client username is [%s]\n", username);

	// get the auth token len
	memcpy(&auth_token_len, client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(username) + 1, 1);

	if (connect_with_db) {
		database = client_data->db;
    		strcpy(database, (char *)(client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(username) + 1 + 1 + auth_token_len));
		fprintf(stderr, "<<< Client selected db is [%s]\n", database);
	} else {
		fprintf(stderr, "<<< Client is NOT connected with db\n");
	}

	// allocate memory for token only if auth_token_len > 0
	if (auth_token_len) {
		auth_token = (uint8_t *)malloc(auth_token_len);
		memcpy(auth_token,  client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(username) + 1 + 1, auth_token_len);
	}

	// decode the token and check the password
	// Note: if auth_token_len == 0 && auth_token == NULL, user is without password
	auth_ret = gw_check_mysql_scramble_data(dcb, auth_token, auth_token_len, protocol->scramble, sizeof(protocol->scramble), username, stage1_hash);

	// let's free the auth_token now
	if (auth_token)
		free(auth_token);

	if (auth_ret != 0) {
		fprintf(stderr, "<<< CLIENT AUTH FAILEDi for user [%s]\n", username);
	}

	return auth_ret;
}

/**
 * Write function for client DCB: writes data from Gateway to Client
 *
 * @param dcb	The DCB of the client
 * @param queue	Queue of buffers to write
 */
int
gw_MySQLWrite_client(DCB *dcb, GWBUF *queue)
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

/**
 * Client read event triggered by EPOLLIN
 *
 * @param dcb	Descriptor control block
 * @return TRUE on error
 */
int gw_read_client_event(DCB* dcb) {
	SESSION         *session = NULL;
	ROUTER_OBJECT   *router = NULL;
	ROUTER          *router_instance = NULL;
	void            *rsession = NULL;
	MySQLProtocol *protocol = NULL;
	int b = -1;

	if (dcb) {
		protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
	}

	if (ioctl(dcb->fd, FIONREAD, &b)) {
		fprintf(stderr, "Client Ioctl FIONREAD error for %i: errno %i, %s\n", dcb->fd, errno , strerror(errno));
		return 1;
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
				{
					SESSION *session = NULL;

					protocol->state = MYSQL_AUTH_RECV;

					//write to client mysql AUTH_OK packet, packet n. is 2
					mysql_send_ok(dcb, 2, 0, NULL);

					// start a new session, and connect to backends
					session = session_alloc(dcb->service, dcb);

					protocol->state = MYSQL_IDLE;

					session->data = (MYSQL_session *)dcb->data;
				}
				else 
				{
					protocol->state = MYSQL_AUTH_FAILED;

					mysql_send_auth_error(dcb, 2, 0, "Authorization failed");

					dcb->func.close(dcb);
				}
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

				session = dcb->session;

				// get the backend session, if available
				if (session) {
					router = session->service->router;
					router_instance = session->service->router_instance;
					rsession = session->router_session;
				}
	
				//////////////////////////////////////////////////////
				// read and handle errors & close, or return if busy
				//////////////////////////////////////////////////////
				if ((ret = gw_read_gwbuff(dcb, &gw_buffer, b)) != 0)
					return ret;

				/* Now, we are assuming in the first buffer there is the information form mysql command */

				queue = gw_buffer;
				len = GWBUF_LENGTH(queue);
			
				ptr_buff = GWBUF_DATA(queue);

				/* get mysql commang at fourth byte */
				if (ptr_buff)
					mysql_command = ptr_buff[4];

				if (mysql_command  == '\x03') {
					/// this is a standard MySQL query !!!!
				}

				/**
				* Routing Client input to Backend
				*/

				/* Do not route the query without session! */
				if(!rsession) {
					if (mysql_command == '\x01') {
						/* COM_QUIT handling */
						//fprintf(stderr, "COM_QUIT received with no connected backends from %i\n", dcb->fd);
                                        	(dcb->func).close(dcb);

						return 1;
					} else {
						/* Send a custom error as MySQL command reply */	
						mysql_send_custom_error(dcb, 1, 0, "Connection to backend lost");

						protocol->state = MYSQL_IDLE;

						return 1;
					}
				}
			
				/* We can route the query */		

				/* COM_QUIT handling */
				if (mysql_command == '\x01') {
					//fprintf(stderr, "COM_QUIT received from %i and passed to backed\n", dcb->fd);

					/* this will propagate COM_QUIT to backend(s) */
					//fprintf(stderr, "<<< Routing the COM_QUIT ...\n");
					router->routeQuery(router_instance, rsession, queue);

					/* close client connection */
                                        (dcb->func).close(dcb);
				
					return 1;
				}

				/* MySQL Command Routing */

				protocol->state = MYSQL_ROUTING;

				/* writing in the backend buffer queue, via routeQuery */

				//fprintf(stderr, "<<< Routing the Query ...\n");
				router->routeQuery(router_instance, rsession, queue);

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
// client write event to Client triggered by EPOLLOUT
//////////////////////////////////////////////
int gw_write_client_event(DCB *dcb) {
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

	if ((protocol->state == MYSQL_IDLE) || (protocol->state == MYSQL_WAITING_RESULT)) {
		int w;

		w = dcb_drain_writeq(dcb);

		return 1;
	}

	return 1;
}

///
// set listener for mysql protocol, retur 1 on success and 0 in failure
///
int gw_MySQLListener(DCB *listener, char *config_bind) {
	int l_so;
	struct sockaddr_in serv_addr;
	char *bind_address_and_port = NULL;
	char *p;
	char address[1024]="";
	int port=0;
	int one = 1;

	// this gateway, as default, will bind on port 4404 for localhost only
	(config_bind != NULL) ? (bind_address_and_port = config_bind) : (bind_address_and_port = "127.0.0.1:4406");

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
		fprintf(stderr, ">>> Error: can't open listening socket. Errno %i, %s\n", errno, strerror(errno));
		return 0;
	}

	// socket options
	setsockopt(l_so, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));

	// set NONBLOCKING mode
        setnonblocking(l_so);

	// bind address and port
        if (bind(l_so, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		fprintf(stderr, ">>> Bind failed !!! %i, [%s]\n", errno, strerror(errno));
                fprintf(stderr, ">>> can't bind to address and port");
		return 0;
        }

        fprintf(stderr, ">> GATEWAY bind is: %s:%i. FD is %i\n", address, port, l_so);

        listen(l_so, 10 * SOMAXCONN);

        fprintf(stderr, ">> GATEWAY listen backlog queue is %i\n", 10 * SOMAXCONN);

        listener->state = DCB_STATE_IDLE;

	// assign l_so to dcb
	listener->fd = l_so;

        // add listening socket to poll structure
        if (poll_add_dcb(listener) == -1) {
                fprintf(stderr, ">>> poll_add_dcb: can't add the listen_sock! Errno %i, %s\n", errno, strerror(errno));
		return 0;
        }

	listener->func.accept = gw_MySQLAccept;

	listener->state = DCB_STATE_LISTENING;

	return 1;
}


int gw_MySQLAccept(DCB *listener) {

	fprintf(stderr, "MySQL Listener socket is: %i\n", listener->fd);

	while (1) {
		int c_sock;
		struct sockaddr_in local;
		socklen_t addrlen;
		addrlen = sizeof(local);
		DCB *client;
		MySQLProtocol *protocol;
		int sendbuf = GW_BACKEND_SO_SNDBUF;
		socklen_t optlen = sizeof(sendbuf);

		// new connection from client
		c_sock = accept(listener->fd, (struct sockaddr *) &local, &addrlen);

		if (c_sock == -1) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
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

		client = dcb_alloc();
		client->service = listener->session->service;
		client->fd = c_sock;
		client->remote = strdup(inet_ntoa(local.sin_addr));

		protocol = (MySQLProtocol *) calloc(1, sizeof(MySQLProtocol));
		client->protocol = (void *)protocol;


		protocol->state = MYSQL_ALLOC;
		protocol->descriptor = client;
		protocol->fd = c_sock;

		// assign function poiters to "func" field
		memcpy(&client->func, &MyObject, sizeof(GWPROTOCOL));

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

/*
*/
static int gw_error_client_event(DCB *dcb) {
	//fprintf(stderr, "#### Handle error function gw_error_client_event, for [%i] is [%s]\n", dcb->fd, gw_dcb_state2string(dcb->state));
        //dcb_close(dcb);

	return 1;
}

static int
gw_client_close(DCB *dcb)
{
        dcb_close(dcb);
	return 1;
}

/**
 * Handle a hangup event on the client side descriptor.
 *
 * We simply close the DCB, this will propogate the closure to any
 * backend descriptors and perform the session cleanup.
 *
 * @param dcb		The DCB of the connection
 */
static int
gw_client_hangup_event(DCB *dcb)
{
        dcb_close(dcb);
	return 1;
}
