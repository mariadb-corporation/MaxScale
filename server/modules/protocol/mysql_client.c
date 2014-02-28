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
 * 02/09/2013	Massimiliano Pinto	Added: session refcount
 * 16/12/2013	Massimiliano Pinto	Added: client closed socket detection with recv(..., MSG_PEEK)
 * 24/02/2014	Massimiliano Pinto	Added: on failed authentication a new users' table is loaded with time and frequency limitations
 * 					If current user is authenticated the new users' table will replace the old one
 *
 */

#include <skygw_utils.h>
#include <log_manager.h>
#include <mysql_client_server_protocol.h>
#include <gw.h>

extern int lm_enabled_logfiles_bitmask;

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
	NULL					/* Session			 */
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
	
	mysql_payload_size = sizeof(field_count) +
                sizeof(affected_rows) +
                sizeof(insert_id) +
                sizeof(mysql_server_status) +
                sizeof(mysql_warning_count);

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
                ss_dassert(buf != NULL);
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
 * @return 0 for Authentication ok, !=0 for failed autht
 *
 */

static int gw_mysql_do_authentication(DCB *dcb, GWBUF *queue) {
	MySQLProtocol *protocol = NULL;
	/* int compress = -1; */
	int connect_with_db = -1;
	uint8_t *client_auth_packet = GWBUF_DATA(queue);
	int client_auth_packet_size = 0;
	char *username =  NULL;
	char *database = NULL;
	unsigned int auth_token_len = 0;
	uint8_t *auth_token = NULL;
	uint8_t *stage1_hash = NULL;
	int auth_ret = -1;
	MYSQL_session *client_data = NULL;

        CHK_DCB(dcb);

        protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
        CHK_PROTOCOL(protocol);
	client_data = (MYSQL_session *)calloc(1, sizeof(MYSQL_session));
	dcb->data = client_data; 

	stage1_hash = client_data->client_sha1;
	username = client_data->user;

	client_auth_packet_size = gwbuf_length(queue);

	/* For clients supporting CLIENT_PROTOCOL_41
	 * the Handshake Response Packet is:
	 *
	 * 4		bytes mysql protocol heade
	 * 4		bytes capability flags
	 * 4		max-packet size
	 * 1		byte character set
	 * string[23]	reserved (all [0])
	 * ...
	 * ...
	 */

	/* Detect now if there are enough bytes to continue */
	if (client_auth_packet_size < (4 + 4 + 4 + 1 + 23)) {
		return 1;
	}

	memcpy(&protocol->client_capabilities, client_auth_packet + 4, 4);

	connect_with_db =
                GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB & gw_mysql_get_byte4(
                        &protocol->client_capabilities);
        /*
	compress =
                GW_MYSQL_CAPABILITIES_COMPRESS & gw_mysql_get_byte4(
                        &protocol->client_capabilities);
        */

	/* now get the user */
	strncpy(username,  (char *)(client_auth_packet + 4 + 4 + 4 + 1 + 23), MYSQL_USER_MAXLEN);


	/* the empty username field is not allowed */
	if (!strlen(username)) {
		return 1;
	}

	/* get the auth token len */
	memcpy(&auth_token_len,
               client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(username) + 1,
               1);

	if (connect_with_db) {
		database = client_data->db;
    		strncpy(database,
                       (char *)(client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(username) +
                                1 + 1 + auth_token_len), MYSQL_DATABASE_MAXLEN);
	}

	/* allocate memory for token only if auth_token_len > 0 */
	if (auth_token_len) {
		auth_token = (uint8_t *)malloc(auth_token_len);
		memcpy(auth_token,
                       client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(username) + 1 + 1,
                       auth_token_len);
	}

	/* decode the token and check the password
	 * Note: if auth_token_len == 0 && auth_token == NULL, user is without password
	 */

	auth_ret = gw_check_mysql_scramble_data(dcb,
                                                auth_token,
                                                auth_token_len,
                                                protocol->scramble, sizeof(protocol->scramble),
                                                username,
                                                stage1_hash);

	/* On failed auth try to load users' table from backend database */
	if (auth_ret != 0) {
		if (!service_refresh_users(dcb->service)) {
			/* Try authentication again with new repository data */
			/* Note: if no auth client authentication will fail */
			auth_ret = gw_check_mysql_scramble_data(dcb, auth_token, auth_token_len, protocol->scramble, sizeof(protocol->scramble), username, stage1_hash);
		}
	}

	/* let's free the auth_token now */
	if (auth_token)
		free(auth_token);

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
	return dcb_write(dcb, queue);
}

/**
 * Client read event triggered by EPOLLIN
 *
 * @param dcb	Descriptor control block
 * @return 0 if succeed, 1 otherwise
 */
int gw_read_client_event(DCB* dcb) {
	SESSION        *session = NULL;
	ROUTER_OBJECT  *router = NULL;
	ROUTER         *router_instance = NULL;
	void           *rsession = NULL;
	MySQLProtocol  *protocol = NULL;
	int             b = -1;
        int             rc = 0;

        CHK_DCB(dcb);
        protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
        CHK_PROTOCOL(protocol);
        /**
         * Check how many bytes are readable in dcb->fd.
         */
        if (ioctl(dcb->fd, FIONREAD, &b) != 0) {
                int eno = errno;
                errno = 0;

                LOGIF(LE, (skygw_log_write(
                        LOGFILE_ERROR,
                        "%lu [gw_read_client_event] ioctl FIONREAD for fd "
                        "%d failed. errno %d, %s. dcb->state = %d",
                        pthread_self(),
                        dcb->fd,
                        eno,
                        strerror(eno),
                        dcb->state)));
                rc = 1;
                goto return_rc;
        }
        
	/*
	 * Handle the closed client socket.
	 */

	if (b == 0) {
		char c;
		int l_errno = 0;
		int r = -1;

		rc = 0;

		/* try to read 1 byte, without consuming the socket buffer */
		r = recv(dcb->fd, &c, sizeof(char), MSG_PEEK);
		l_errno = errno;

		if (r <= 0) {
			if ( (l_errno == EAGAIN) || (l_errno == EWOULDBLOCK)) {
				goto return_rc;
			}

			// close client socket and the sessioA too
			dcb->func.close(dcb);
		} else {
			// do nothing if reading 1 byte
		}

		goto return_rc;
	}

	switch (protocol->state) {
        case MYSQL_AUTH_SENT:
                /*
                 * Read all the data that is available into a chain of buffers
                 */
        {
                int    len = -1;
                GWBUF *queue = NULL;
                GWBUF *gw_buffer = NULL;
                int    auth_val = -1;
                //////////////////////////////////////////////////////
                // read and handle errors & close, or return if busy
                // note: if b == 0 error handling is not
                // triggered, just return
                // without closing
                //////////////////////////////////////////////////////
                rc = gw_read_gwbuff(dcb, &gw_buffer, b); 
                
                if (rc != 0) {
                        goto return_rc;
                }
                
                // example with consume, assuming one buffer only ...
                queue = gw_buffer;
                len = GWBUF_LENGTH(queue);

		ss_dassert(len > 0);

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
                        // start a new session, and connect to backends
                        session = session_alloc(dcb->service, dcb);
                        
                        if (session != NULL) {
                                CHK_SESSION(session);
                                ss_dassert(session->state != SESSION_STATE_ALLOC);
                                protocol->state = MYSQL_IDLE;
                                mysql_send_ok(dcb, 2, 0, NULL);
                        } else {
                                protocol->state = MYSQL_AUTH_FAILED;
                                mysql_send_auth_error(
                                        dcb,
                                        2,
                                        0,
                                        "failed to create new session");
                                dcb->func.close(dcb);
                        }
                }
                else 
                {
                        protocol->state = MYSQL_AUTH_FAILED;
                        mysql_send_auth_error(
                                dcb,
                                2,
                                0,
                                "Authorization failed");                        
                        dcb->func.close(dcb);
                }
        }
        break;
        
        case MYSQL_IDLE:
                /*
                 * Read all the data that is available into a chain of buffers
                 */
        {
                int      len = -1;
                GWBUF   *queue = NULL;
                GWBUF   *gw_buffer = NULL;
                uint8_t *ptr_buff = NULL;
                int      mysql_command = -1;
                
                session = dcb->session;
                
                // get the backend session, if available
                if (session != NULL) {
                        CHK_SESSION(session);
                        router = session->service->router;
                        router_instance =
                                session->service->router_instance;
                        rsession = session->router_session;
                }
                
                //////////////////////////////////////////////////////
                // read and handle errors & close, or return if busy
                //////////////////////////////////////////////////////
                rc = gw_read_gwbuff(dcb, &gw_buffer, b); 
                
                if (rc != 0) {
                        goto return_rc;
                }
                /* Now, we are assuming in the first buffer there is
                 * the information form mysql command */
                queue = gw_buffer;
                len = GWBUF_LENGTH(queue);
                ptr_buff = GWBUF_DATA(queue);
                
                /* get mysql commang at fifth byte */
                if (ptr_buff) {
                        mysql_command = ptr_buff[4];
                }
                /**
                 * Without rsession there is no access to backend.
                 * COM_QUIT : close client dcb
                 * else     : write custom error to client dcb.
                 */
                if(rsession == NULL) {
                        /** COM_QUIT */
                        if (mysql_command == '\x01') {
                                LOGIF(LD, (skygw_log_write_flush(
                                        LOGFILE_DEBUG,
                                        "%lu [gw_read_client_event] Client read "
                                        "COM_QUIT and rsession == NULL. Closing "
                                        "client dcb %p.",
                                        pthread_self(),
                                        dcb)));
                                (dcb->func).close(dcb);
                        } else {
                                /* Send a custom error as MySQL command reply */
                                mysql_send_custom_error(
                                        dcb,
                                        1,
                                        0,
                                        "Query routing failed. Connection to "
                                        "backend lost");
                                protocol->state = MYSQL_IDLE;
                        }
                        rc = 1;
                        /** Free buffer */
                        queue = gwbuf_consume(queue, len);                
                        goto return_rc;
                }
                /** Route COM_QUIT to backend */
                if (mysql_command == '\x01') {
                        router->routeQuery(router_instance, rsession, queue);
                        LOGIF(LD, (skygw_log_write_flush(
                                LOGFILE_DEBUG,
                                "%lu [gw_read_client_event] Routed COM_QUIT to "
                                "backend. Close client dcb %p",
                                pthread_self(),
                                dcb)));
                        
                        /** close client connection */
                        (dcb->func).close(dcb);
			/** close backends connection */
                        router->closeSession(router_instance, rsession);
                        rc = 1;
                }
                else
                {
                        /** Route other commands to backend */
                        rc = router->routeQuery(router_instance,
                                                rsession,
                                                queue);
                        /** succeed */
                        if (rc == 1) {
                                rc = 0; /**< here '0' means success */
                        } else {
                                mysql_send_custom_error(dcb,
                                                        1,
                                                        0,
                                                        "Query routing failed. "
                                                        "Connection to backend "
                                                        "lost.");
                                protocol->state = MYSQL_IDLE;
                        }
                }
                goto return_rc;
        } /*  MYSQL_IDLE */
        break;
        
        default:
                break;
	}
        rc = 0;
        
return_rc:
#if defined(SS_DEBUG)
        if (dcb->state == DCB_STATE_POLLING ||
            dcb->state == DCB_STATE_NOPOLLING ||
            dcb->state == DCB_STATE_ZOMBIE)
        {
                CHK_PROTOCOL(protocol);
        }
#endif
	return rc;
}

///////////////////////////////////////////////
// client write event to Client triggered by EPOLLOUT
//////////////////////////////////////////////
/** 
 * @node Client's fd became writable, and EPOLLOUT event
 * arrived. As a consequence, client input buffer (writeq) is flushed. 
 *
 * Parameters:
 * @param dcb - in, use
 *          client dcb
 *
 * @return constantly 1
 *
 * 
 * @details (write detailed description here)
 *
 */
int gw_write_client_event(DCB *dcb)
{
	MySQLProtocol *protocol = NULL;

        CHK_DCB(dcb);

        ss_dassert(dcb->state != DCB_STATE_DISCONNECTED);
        
	if (dcb == NULL) {
		goto return_1;
	}

	if (dcb->state == DCB_STATE_DISCONNECTED) {
		goto return_1;
	}
        
	if (dcb->protocol == NULL) {
	        goto return_1;
	}
        protocol = (MySQLProtocol *)dcb->protocol;
        CHK_PROTOCOL(protocol);
        
	if (protocol->state == MYSQL_IDLE)
        {
		dcb_drain_writeq(dcb);
                goto return_1;
	}

return_1:
#if defined(SS_DEBUG)
        if (dcb->state == DCB_STATE_POLLING ||
            dcb->state == DCB_STATE_NOPOLLING ||
            dcb->state == DCB_STATE_ZOMBIE)
        {
                CHK_PROTOCOL(protocol);
        }
#endif
        return 1;
}

/**
 * set listener for mysql protocol, retur 1 on success and 0 in failure
 */
int gw_MySQLListener(
        DCB  *listen_dcb,
        char *config_bind)
{
	int l_so;
	struct sockaddr_in serv_addr;
	int  one = 1;
        int  rc;

	/* this gateway, as default, will bind on port 4404 for localhost only */
	if (!parse_bindconfig(config_bind, 4406, &serv_addr))
		return 0;
	listen_dcb->fd = -1;
        
	// socket create
	if ((l_so = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                fprintf(stderr,
                        "\n* Error: can't open listening socket due "
                        "error %i, %s.\n\n\t",
                        errno,
                        strerror(errno));
                return 0;
	}
	// socket options
	setsockopt(l_so, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));

	// set NONBLOCKING mode
        setnonblocking(l_so);

	// bind address and port
        if (bind(l_so, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		fprintf(stderr,
                        "\n* Bind failed due error %i, %s.\n",
                        errno,
                        strerror(errno));
                fprintf(stderr, "* Can't bind to %s\n\n",
                        config_bind);
		return 0;
        }
        /*
        fprintf(stderr,
                ">> GATEWAY bind is: %s:%i. FD is %i\n",
                address,
                port,
                l_so);
        */
        
        rc = listen(l_so, 10 * SOMAXCONN);

        if (rc == 0) {
                fprintf(stderr,
                        "Listening MySQL connections at %s\n",
                        config_bind);
        } else {
                int eno = errno;
                errno = 0;
                fprintf(stderr,
                        "\n* Failed to start listening MySQL due error %d, %s\n\n",
                        eno,
                        strerror(eno));
                return 0;
        }
        /*
        fprintf(stderr,
                ">> GATEWAY listen backlog queue is %i\n",
                10 * SOMAXCONN);
        */
	// assign l_so to dcb
	listen_dcb->fd = l_so;

        // add listening socket to poll structure
        if (poll_add_dcb(listen_dcb) == -1) {
            fprintf(stderr,
                    "\n* Failed to start polling the socket due error "
                    "%i, %s.\n\n",
                    errno,
                    strerror(errno));
		return 0;
        }
#if defined(SS_DEBUG)
        conn_open[l_so] = true;
#endif
	listen_dcb->func.accept = gw_MySQLAccept;

	return 1;
}


/** 
 * @node (write brief function description here) 
 *
 * Parameters:
 * @param listener - <usage>
 *          <description>
 *
 * @return 0 in success, 1 in failure
 *
 * 
 * @details (write detailed description here)
 *
 */
int gw_MySQLAccept(DCB *listener)
{
        int                rc = 0;
        DCB                *client_dcb;
        MySQLProtocol      *protocol;
        int                c_sock;
        struct sockaddr_in local;
        socklen_t          addrlen = sizeof(struct sockaddr_in);
        int                sendbuf = GW_BACKEND_SO_SNDBUF;
        socklen_t          optlen = sizeof(sendbuf);
        int                eno = 0;
        int                i = 0;
                
        CHK_DCB(listener);
        
	while (1) {

    retry_accept:

#if defined(SS_DEBUG)
                if (fail_next_accept > 0)
                {
                        c_sock = -1;
                        eno = fail_accept_errno;
                        fail_next_accept -= 1;
                } else {
                        fail_accept_errno = 0;          
#endif /* SS_DEBUG */
                        // new connection from client
		        c_sock = accept(listener->fd,
                                        (struct sockaddr *) &local,
                                        &addrlen);
                        eno = errno;
                        errno = 0;
#if defined(SS_DEBUG)
                }
#endif /* SS_DEBUG */
                        
                if (c_sock == -1) {
                        
                        if (eno == EAGAIN || eno == EWOULDBLOCK)
                        {
                                /**
                                 * We have processed all incoming connections.
                                 */
                                rc = 1;
                                goto return_rc;
                        }
                        else if (eno == ENFILE || eno == EMFILE)
                        {
                                /**
                                 * Exceeded system's (ENFILE) or processes
                                 * (EMFILE) max. number of files limit.
                                 */
                                LOGIF(LD, (skygw_log_write(
                                        LOGFILE_DEBUG,
                                        "%lu [gw_MySQLAccept] Error %d, %s. ",
                                        pthread_self(),
                                        eno,
                                        strerror(eno))));
                                
                                if (i == 0)
                                {
                                        LOGIF(LE, (skygw_log_write_flush(
                                                LOGFILE_ERROR,
                                                "Error %d, %s. "
                                                "Failed to accept new client "
                                                "connection.",
                                                eno,
                                                strerror(eno))));
                                }
                                i++;
                                usleep(100*i*i);
                                
                                if (i<10) {
                                        goto retry_accept;
                                }
                                rc = 1;
                                goto return_rc;
                        }
                        else
                        {
                                /**
                                 * Other error.
                                 */
                                LOGIF(LD, (skygw_log_write(
                                        LOGFILE_DEBUG,
                                        "%lu [gw_MySQLAccept] Error %d, %s.",
                                        pthread_self(),
                                        eno,
                                        strerror(eno))));
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error %d, %s."
                                        "Failed to accept new client connection.",
                                        eno,
                                        strerror(eno))));
                                rc = 1;
                                goto return_rc;
                        } /* if (eno == ..) */
		} /* if (c_sock == -1) */
                /* reset counter */
                i = 0;
                
                listener->stats.n_accepts++;
#if defined(SS_DEBUG)
                LOGIF(LD, (skygw_log_write_flush(
                        LOGFILE_DEBUG,
                        "%lu [gw_MySQLAccept] Accepted fd %d.",
                        pthread_self(),
                        c_sock)));
                conn_open[c_sock] = true;
#endif
                /* set nonblocking  */
                setsockopt(c_sock, SOL_SOCKET, SO_SNDBUF, &sendbuf, optlen);
                setnonblocking(c_sock);
                
                client_dcb = dcb_alloc(DCB_ROLE_REQUEST_HANDLER);
                client_dcb->service = listener->session->service;
                client_dcb->fd = c_sock;
                client_dcb->remote = strdup(inet_ntoa(local.sin_addr));

                protocol = mysql_protocol_init(client_dcb, c_sock);
                ss_dassert(protocol != NULL);
                
                if (protocol == NULL) {
                        /** delete client_dcb */
                        dcb_close(client_dcb);
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "%lu [gw_MySQLAccept] Failed to create "
                                "protocol object for client connection.",
                                pthread_self())));
                        rc = 1;
                        goto return_rc;
                }
                client_dcb->protocol = protocol;
                // assign function poiters to "func" field
                memcpy(&client_dcb->func, &MyObject, sizeof(GWPROTOCOL));
                //send handshake to the client_dcb
                MySQLSendHandshake(client_dcb);

                // client protocol state change
                protocol->state = MYSQL_AUTH_SENT;

                /**
                 * Set new descriptor to event set. At the same time,
                 * change state to DCB_STATE_POLLING so that
                 * thread which wakes up sees correct state.
                 */
                if (poll_add_dcb(client_dcb) == -1)
                {
                        /* Send a custom error as MySQL command reply */
                        mysql_send_custom_error(
                                client_dcb,
                                1,
                                0,
                                "MaxScale internal error.");
                        
                        /** delete client_dcb */
                        dcb_close(client_dcb);

                        /** Previous state is recovered in poll_add_dcb. */
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "%lu [gw_MySQLAccept] Failed to add dcb %p for "
                                "fd %d to epoll set.",
                                pthread_self(),
                                client_dcb,
                                client_dcb->fd)));
                        rc = 1;
                        goto return_rc;
                }
                else
                {
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [gw_MySQLAccept] Added dcb %p for fd "
                                "%d to epoll set.",
                                pthread_self(),
                                client_dcb,
                                client_dcb->fd)));
                }
        } /**< while 1 */
#if defined(SS_DEBUG)
        if (rc == 0) {
                CHK_DCB(client_dcb);
                CHK_PROTOCOL(((MySQLProtocol *)client_dcb->protocol));
        }
#endif
return_rc:
        return rc;
}

static int gw_error_client_event(DCB *dcb) {
        SESSION*       session;
        ROUTER_OBJECT* router;
        void*          router_instance;
        void*          rsession;

#if defined(SS_DEBUG)
        MySQLProtocol* protocol = (MySQLProtocol *)dcb->protocol;
        if (dcb->state == DCB_STATE_POLLING ||
            dcb->state == DCB_STATE_NOPOLLING ||
            dcb->state == DCB_STATE_ZOMBIE)
        {
                CHK_PROTOCOL(protocol);
        }
#endif

        session = dcb->session;

        /**
         * session may be NULL if session_alloc failed.
         * In that case router session was not created.
         */
        if (session != NULL) {
                CHK_SESSION(session);
                router = session->service->router;
                router_instance = session->service->router_instance;
                rsession = session->router_session;

                router->closeSession(router_instance, rsession);
        }
        dcb_close(dcb);
	return 1;
}

static int
gw_client_close(DCB *dcb)
{
        SESSION*       session;
        ROUTER_OBJECT* router;
        void*          router_instance;
        void*          rsession;
#if defined(SS_DEBUG)
        MySQLProtocol* protocol = (MySQLProtocol *)dcb->protocol;
        if (dcb->state == DCB_STATE_POLLING ||
            dcb->state == DCB_STATE_NOPOLLING ||
            dcb->state == DCB_STATE_ZOMBIE)
        {
                CHK_PROTOCOL(protocol);
        }
#endif

        session = dcb->session;
        /**
         * session may be NULL if session_alloc failed.
         * In that case, router session wasn't created.
         */
        if (session != NULL) {
                CHK_SESSION(session);
                router = session->service->router;
                router_instance = session->service->router_instance;
                rsession = session->router_session;
        
                router->closeSession(router_instance, rsession);
        }
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
        SESSION*       session;
        ROUTER_OBJECT* router;
        void*          router_instance;
        void*          rsession;
        int            rc = 1;
#if defined(SS_DEBUG)
        MySQLProtocol* protocol = (MySQLProtocol *)dcb->protocol;
        if (dcb->state == DCB_STATE_POLLING ||
            dcb->state == DCB_STATE_NOPOLLING ||
            dcb->state == DCB_STATE_ZOMBIE)
        {
                CHK_PROTOCOL(protocol);
        }
#endif
        

        CHK_DCB(dcb);
        
        if (dcb->state != DCB_STATE_POLLING) {
                goto return_rc;
        }

        session = dcb->session;
        /**
         * session may be NULL if session_alloc failed.
         * In that case router session was not created.
         */
        if (session != NULL) {
                CHK_SESSION(session);
                router = session->service->router;
                router_instance = session->service->router_instance;
                rsession = session->router_session;

                router->closeSession(router_instance, rsession);
        }

        dcb_close(dcb);
return_rc:
	return rc;
}
