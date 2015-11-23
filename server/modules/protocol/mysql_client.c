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
 * @file mysql_client.c
 *
 * MySQL Protocol module for handling the protocol between the gateway
 * and the client.
 *
 * Revision History
 * Date		Who			Description
 * 14/06/2013	Mark Riddoch		Initial version
 * 17/06/2013	Massimiliano Pinto	Added Client To MaxScale routines
 * 24/06/2013	Massimiliano Pinto	Added: fetch passwords from service users' hashtable
 * 02/09/2013	Massimiliano Pinto	Added: session refcount
 * 16/12/2013	Massimiliano Pinto	Added: client closed socket detection with recv(..., MSG_PEEK)
 * 24/02/2014	Massimiliano Pinto	Added: on failed authentication a new users' table is loaded with time and frequency limitations
 * 					If current user is authenticated the new users' table will replace the old one
 * 28/02/2014   Massimiliano Pinto	Added: client IPv4 in dcb->ipv4 and inet_ntop for string representation
 * 11/03/2014   Massimiliano Pinto	Added: Unix socket support
 * 07/05/2014   Massimiliano Pinto	Added: specific version string in server handshake
 * 09/09/2014	Massimiliano Pinto	Added: 777 permission for socket path
 * 13/10/2014	Massimiliano Pinto	Added: dbname authentication check
 * 10/11/2014	Massimiliano Pinto	Added: client charset added to protocol struct
 * 29/05/2015   Markus Makela           Added SSL support
 * 11/06/2015   Martin Brampton		COM_QUIT suppressed for persistent connections
 * 04/09/2015   Martin Brampton         Introduce DUMMY session to fulfill guarantee DCB always has session
 * 09/09/2015   Martin Brampton         Modify error handler calls 
 */
#include <skygw_utils.h>
#include <log_manager.h>
#include <mysql_client_server_protocol.h>
#include <gw.h>
#include <modinfo.h>
#include <sys/stat.h>
#include <modutil.h>
#include <netinet/tcp.h>

MODULE_INFO info = {
	MODULE_API_PROTOCOL,
	MODULE_GA,
	GWPROTOCOL_VERSION,
	"The client to MaxScale MySQL protocol implementation"
};

static char *version_str = "V1.0.0";

static int gw_MySQLAccept(DCB *listener);
static int gw_MySQLListener(DCB *listener, char *config_bind);
static int gw_read_client_event(DCB* dcb);
static int gw_write_client_event(DCB *dcb);
static int gw_MySQLWrite_client(DCB *dcb, GWBUF *queue);
static int gw_error_client_event(DCB *dcb);
static int gw_client_close(DCB *dcb);
static int gw_client_hangup_event(DCB *dcb);
int gw_read_client_event_SSL(DCB* dcb);
int gw_MySQLWrite_client_SSL(DCB *dcb, GWBUF *queue);
int gw_write_client_event_SSL(DCB *dcb);
int mysql_send_ok(DCB *dcb, int packet_number, int in_affected_rows, const char* mysql_message);
int MySQLSendHandshake(DCB* dcb);
static int gw_mysql_do_authentication(DCB *dcb, GWBUF **queue);
static int route_by_statement(SESSION *, GWBUF **);
extern char* get_username_from_auth(char* ptr, uint8_t* data);
extern int check_db_name_after_auth(DCB *, char *, int);
extern char* create_auth_fail_str(char *username, char *hostaddr, char *sha1, char *db,int);

int do_ssl_accept(MySQLProtocol* protocol);

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
        uint32_t mysql_payload_size = 0;
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
        uint32_t mysql_payload_size = 0;
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
	char *version_string;
	int len_version_string=0;
	
	MySQLProtocol *protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
	GWBUF		*buf;

	/* get the version string from service property if available*/
	if (dcb->service->version_string != NULL) {
		version_string = dcb->service->version_string;
		len_version_string = strlen(version_string);
	} else {
		version_string = GW_MYSQL_VERSION;
		len_version_string = strlen(GW_MYSQL_VERSION);
	}

	gw_generate_random_str(server_scramble, GW_MYSQL_SCRAMBLE_SIZE);
	
	// copy back to the caller
	memcpy(protocol->scramble, server_scramble, GW_MYSQL_SCRAMBLE_SIZE);

	// fill the handshake packet

	memset(&mysql_filler_ten, 0x00, sizeof(mysql_filler_ten));

        // thread id, now put thePID
        gw_mysql_set_byte4(mysql_thread_id, getpid() + dcb->fd);
	
        memcpy(mysql_scramble_buf, server_scramble, 8);

        memcpy(mysql_plugin_data, server_scramble + 8, 12);

        mysql_payload_size  = sizeof(mysql_protocol_version) + (len_version_string + 1) + sizeof(mysql_thread_id) + 8 + sizeof(mysql_filler) + sizeof(mysql_server_capabilities_one) + sizeof(mysql_server_language) + sizeof(mysql_server_status) + sizeof(mysql_server_capabilities_two) + sizeof(mysql_scramble_len) + sizeof(mysql_filler_ten) + 12 + sizeof(mysql_last_byte) + strlen("mysql_native_password") + sizeof(mysql_last_byte);

        // allocate memory for packet header + payload
        if ((buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size)) == NULL)
	{
                ss_dassert(buf != NULL);
		return 0;
	}
	outbuf = GWBUF_DATA(buf);

        // write packet heder with mysql_payload_size
        gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);

        // write packent number, now is 0
        mysql_packet_header[3]= mysql_packet_id;
        memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

        // current buffer pointer
        mysql_handshake_payload = outbuf + sizeof(mysql_packet_header);

        // write protocol version
        memcpy(mysql_handshake_payload, &mysql_protocol_version, sizeof(mysql_protocol_version));
        mysql_handshake_payload = mysql_handshake_payload + sizeof(mysql_protocol_version);

        // write server version plus 0 filler
       	strcpy((char *)mysql_handshake_payload, version_string);
       	mysql_handshake_payload = mysql_handshake_payload + len_version_string;

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

	if(dcb->service->ssl_mode != SSL_DISABLED)
	{
	   mysql_server_capabilities_one[1] |= GW_MYSQL_CAPABILITIES_SSL >> 8;
	}
	else
	{
	    mysql_server_capabilities_one[0] &= ~GW_MYSQL_CAPABILITIES_SSL;
	}


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
 * Performs the MySQL protocol 4.1 authentication, using data in GWBUF **queue.
 *
 * (MYSQL_session*)client_data including: user, db, client_sha1 are copied into 
 * the dcb->data and later to dcb->session->data. client_capabilities are copied
 * into the dcb->protocol.
 *
 * If SSL is enabled for the service, the SSL handshake will be done before the
 * MySQL authentication.
 *
 * @param	dcb 	Descriptor Control Block of the client
 * @param	queue	Pointer to the location of the GWBUF with data from client
 * @return	0	If succeed, otherwise non-zero value
 *
 * @note in case of failure, dcb->data is freed before returning. If succeed,
 * dcb->data is freed in session.c:session_free.
 */
static int gw_mysql_do_authentication(DCB *dcb, GWBUF **buf) {
    GWBUF* queue = *buf;
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
	int ssl = 0;
        CHK_DCB(dcb);

        protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
        CHK_PROTOCOL(protocol);
	if(dcb->data == NULL)
	{
	    client_data = (MYSQL_session *)calloc(1, sizeof(MYSQL_session));
#if defined(SS_DEBUG)
	    client_data->myses_chk_top = CHK_NUM_MYSQLSES;
	    client_data->myses_chk_tail = CHK_NUM_MYSQLSES;
#endif
	    dcb->data = client_data;
	}
	else
	{
	    client_data = (MYSQL_session *)dcb->data;
	}

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
	if (client_auth_packet_size < (4 + 4 + 4 + 1 + 23)) 
	{
		return MYSQL_FAILED_AUTH;
	}

	memcpy(&protocol->client_capabilities, client_auth_packet + 4, 4);

	connect_with_db =
                GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB & gw_mysql_get_byte4(
                        (uint32_t *)&protocol->client_capabilities);
        /*
	compress =
                GW_MYSQL_CAPABILITIES_COMPRESS & gw_mysql_get_byte4(
                        &protocol->client_capabilities);
        */

	/** Skip this if the SSL handshake is already done.
	 * If not, start the SSL handshake.  */
	if(protocol->protocol_auth_state != MYSQL_AUTH_SSL_HANDSHAKE_DONE)
	{

	    ssl = protocol->client_capabilities & GW_MYSQL_CAPABILITIES_SSL;

	    /** Client didn't requested SSL when SSL mode was required*/
	    if(!ssl && protocol->owner_dcb->service->ssl_mode == SSL_REQUIRED)
	    {
		MXS_INFO("User %s@%s connected to service '%s' without SSL when SSL was required.",
                         protocol->owner_dcb->user,
                         protocol->owner_dcb->remote,
                         protocol->owner_dcb->service->name);
		return MYSQL_FAILED_AUTH_SSL;
	    }

	    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO) && ssl)
	    {
		MXS_INFO("User %s@%s connected to service '%s' with SSL.",
			 protocol->owner_dcb->user,
			 protocol->owner_dcb->remote,
			 protocol->owner_dcb->service->name);
	    }

	    /** Do the SSL Handshake */
	    if(ssl && protocol->owner_dcb->service->ssl_mode != SSL_DISABLED)
	    {
		protocol->protocol_auth_state = MYSQL_AUTH_SSL_REQ;

		if(do_ssl_accept(protocol) < 0)
		{
		    return MYSQL_FAILED_AUTH;
		}
		else
		{
		    return 0;
		}
	    }
	    else if(dcb->service->ssl_mode == SSL_ENABLED)
	    {
		/** This is a non-SSL connection to a SSL enabled service.
		 * We have only read enough of the packet to know that the client
		 * is not requesting SSL and the rest of the auth packet is still
		 * waiting in the socket. We need to read the data from the socket
		 * to find out the username of the connecting client. */
		int bytes = dcb_read(dcb,&queue, 0);
		queue = gwbuf_make_contiguous(queue);
		client_auth_packet = GWBUF_DATA(queue);
		client_auth_packet_size = gwbuf_length(queue);
		*buf = queue;
		MXS_DEBUG("%lu Read %d bytes from fd %d",pthread_self(),bytes,dcb->fd);
	    }
	}

	username = get_username_from_auth(username, client_auth_packet);
	
	if (username == NULL)
	{
		return MYSQL_FAILED_AUTH;
	}

	/* get charset */
	memcpy(&protocol->charset, client_auth_packet + 4 + 4 + 4, sizeof (int));

	/* get the auth token len */
	memcpy(&auth_token_len,
               client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(username) + 1,
               1);

	/* 
	 * Note: some clients may pass empty database, connect_with_db !=0 but database =""
	 */
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

	/* 
	 * Decode the token and check the password
	 * Note: if auth_token_len == 0 && auth_token == NULL, user is without password
	 */
	MXS_DEBUG("Receiving connection from '%s' to database '%s'.",username,database);
	auth_ret = gw_check_mysql_scramble_data(dcb,
                                                auth_token,
                                                auth_token_len,
                                                protocol->scramble, 
						sizeof(protocol->scramble),
                                                username,
                                                stage1_hash);

	/* check for database name match in resource hashtable */
	auth_ret = check_db_name_after_auth(dcb, database, auth_ret);

	/* On failed auth try to load users' table from backend database */
	if (auth_ret != 0) {
		if (!service_refresh_users(dcb->service)) {
			/* Try authentication again with new repository data */
			/* Note: if no auth client authentication will fail */
			auth_ret = gw_check_mysql_scramble_data(
					dcb, 
					auth_token, 
					auth_token_len, 
					protocol->scramble, 
					sizeof(protocol->scramble), 
					username, 
					stage1_hash);

            /* Do again the database check */
            auth_ret = check_db_name_after_auth(dcb, database, auth_ret);
		}
    }

	/* on succesful auth set user into dcb field */
	if (auth_ret == 0) {
		dcb->user = strdup(client_data->user);
    }
    else if (dcb->service->log_auth_warnings)
    {
        MXS_NOTICE("%s: login attempt for user '%s', authentication failed.",
                   dcb->service->name, username);
        if (dcb->ipv4.sin_addr.s_addr == 0x0100007F &&
            !dcb->service->localhost_match_wildcard_host)
        {
            MXS_NOTICE("If you have a wildcard grant that covers"
                       " this address, try adding "
                       "'localhost_match_wildcard_host=true' for "
                       "service '%s'. ", dcb->service->name);
        }
    }

	/* let's free the auth_token now */
	if (auth_token) {
		free(auth_token);
	}
	
	return auth_ret;
}

/**
 * Write function for client DCB: writes data from MaxScale to Client
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
 * Write function for client DCB: writes data from MaxScale to Client using SSL
 * encryption. The SSH handshake must have already been done.
 *
 * @param dcb	The DCB of the client
 * @param queue	Queue of buffers to write
 */
int
gw_MySQLWrite_client_SSL(DCB *dcb, GWBUF *queue)
{
    CHK_DCB(dcb);
#ifdef SS_DEBUG
    MySQLProtocol  *protocol = NULL;
    protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
    CHK_PROTOCOL(protocol);
#endif
    return dcb_write_SSL(dcb, queue);
}

/**
 * Client read event triggered by EPOLLIN
 *
 * @param dcb	Descriptor control block
 * @return 0 if succeed, 1 otherwise
 */
int gw_read_client_event(
        DCB* dcb) 
{
	SESSION        *session = NULL;
	ROUTER_OBJECT  *router = NULL;
	ROUTER         *router_instance = NULL;
	void           *rsession = NULL;
	MySQLProtocol  *protocol = NULL;
        GWBUF          *read_buffer = NULL;
        int             rc = 0;
        int             nbytes_read = 0;
        uint8_t         cap = 0;
        bool            stmt_input = false; /*< router input type */

        CHK_DCB(dcb);
        protocol = DCB_PROTOCOL(dcb, MySQLProtocol);
        CHK_PROTOCOL(protocol);

#ifdef SS_DEBUG
	MXS_DEBUG("[gw_read_client_event] Protocol state: %s",
                  gw_mysql_protocol_state2string(protocol->protocol_auth_state));

#endif

	/** SSL authentication is still going on, we need to call do_ssl_accept
	 * until it return 1 for success or -1 for error */
	if(protocol->protocol_auth_state == MYSQL_AUTH_SSL_HANDSHAKE_ONGOING ||
	 protocol->protocol_auth_state == MYSQL_AUTH_SSL_REQ)
	{

	    switch(do_ssl_accept(protocol))
	    {
	    case 0:
		return 0;
		break;
	    case 1:
	    {
		int b = 0;
		ioctl(dcb->fd,FIONREAD,&b);
		if(b == 0) 
		{
		    MXS_DEBUG("[gw_read_client_event] No data in socket after SSL auth");
		    return 0;
		}
		break;
	    }

	    case -1:
		return 1;
		break;
	    default:
		return 1;
		break;
	    }
	}

	if(protocol->use_ssl)
	{
	    /** SSL handshake is done, communication is now encrypted with SSL */
	    rc = dcb_read_SSL(dcb, &read_buffer);
	}
	else if(dcb->service->ssl_mode != SSL_DISABLED &&
	 protocol->protocol_auth_state == MYSQL_AUTH_SENT)
	{
	    /** The service allows both SSL and non-SSL connections.
	     * read only enough of the auth packet to know if the client is
	     * requesting SSL. If the client is not requesting SSL the rest of
	     the auth packet will be read later. */
	    rc = dcb_read(dcb, &read_buffer,(4 + 4 + 4 + 1 + 23));
	}
	else
	{
	    /** Normal non-SSL connection */
	    rc = dcb_read(dcb, &read_buffer, 0);
	}

        if (rc < 0)
        {
                dcb_close(dcb);
        }
        nbytes_read = gwbuf_length(read_buffer);
       
        if (nbytes_read == 0)
        {
                goto return_rc;
        }

	session = dcb->session;

	if (protocol->protocol_auth_state == MYSQL_IDLE && session != NULL && SESSION_STATE_DUMMY != session->state)
	{
		CHK_SESSION(session);
		router = session->service->router;
		router_instance = session->service->router_instance;
		rsession = session->router_session;

        if (NULL == router_instance || NULL == rsession)
        {
            /** Send ERR 1045 to client */
            mysql_send_auth_error(
				dcb,
				2,
				0,
				"failed to create new session");
            while (read_buffer)
            {
                read_buffer = gwbuf_consume(read_buffer, GWBUF_LENGTH(read_buffer));
            }
            return 0;
        }

        /** Ask what type of input the router expects */
        cap = router->getCapabilities(router_instance, rsession);
        
        if (cap & RCAP_TYPE_STMT_INPUT)
        {
            stmt_input = true;
            /** Mark buffer to as MySQL type */
            gwbuf_set_type(read_buffer, GWBUF_TYPE_MYSQL);
        }
    }

	if (stmt_input) {
                
		/** 
		 * if read queue existed appent read to it.
		 * if length of read buffer is less than 3 or less than mysql packet
		 *  then return.
		 * else copy mysql packets to separate buffers from read buffer and 
		 * continue.
		 * else
		 * if read queue didn't exist, length of read is less than 3 or less 
		 * than mysql packet then 
		 * create read queue and append to it and return.
		 * if length read is less than mysql packet length append to read queue
		 * append to it and return.
		 * else (complete packet was read) continue.
		*/
		if (dcb->dcb_readqueue)
		{
			uint8_t* data;

			dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, read_buffer);
			nbytes_read = gwbuf_length(dcb->dcb_readqueue);
			data = (uint8_t *)GWBUF_DATA(dcb->dcb_readqueue);
			int plen = MYSQL_GET_PACKET_LEN(data);
			if (nbytes_read < 3 || nbytes_read < MYSQL_GET_PACKET_LEN(data) + 4)
			{
				rc = 0;
				goto return_rc;
			}
			else
			{
				/** 
				 * There is at least one complete mysql packet in
				 * read_buffer. 
				*/
				read_buffer = dcb->dcb_readqueue;
				dcb->dcb_readqueue = NULL;                        
			}
		}
		else
		{
			uint8_t* data = (uint8_t *)GWBUF_DATA(read_buffer);

			if (nbytes_read < 3 || nbytes_read < MYSQL_GET_PACKET_LEN(data)+4) 
	                {
				dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, read_buffer);
				rc = 0;
				goto return_rc;
			}
		}

	}

        /**
         * Now there should be at least one complete mysql packet in read_buffer.
         */
	switch (protocol->protocol_auth_state) {

        case MYSQL_AUTH_SENT:
        {
		int auth_val;
		
                auth_val = gw_mysql_do_authentication(dcb, &read_buffer);

		if(protocol->protocol_auth_state == MYSQL_AUTH_SSL_REQ ||
		 protocol->protocol_auth_state == MYSQL_AUTH_SSL_HANDSHAKE_ONGOING ||
		 protocol->protocol_auth_state == MYSQL_AUTH_SSL_HANDSHAKE_DONE ||
		 protocol->protocol_auth_state == MYSQL_AUTH_SSL_HANDSHAKE_FAILED)
		{
		    /** SSL was requested and the handshake is either done or
		     * still ongoing. After the handshake is done, the client
		     * will send another auth packet. */
		    while((read_buffer = gwbuf_consume(read_buffer,GWBUF_LENGTH(read_buffer))));
		    break;
		}
		
		if (auth_val == 0)
		{
			SESSION *session;
			
			protocol->protocol_auth_state = MYSQL_AUTH_RECV;
			/**
			 * Create session, and a router session for it.
			 * If successful, there will be backend connection(s)
			 * after this point.
			 */
			session = session_alloc(dcb->service, dcb);
			
			if (session != NULL) 
			{
				CHK_SESSION(session);
				ss_dassert(session->state != SESSION_STATE_ALLOC && session->state != SESSION_STATE_DUMMY);
				
				protocol->protocol_auth_state = MYSQL_IDLE;
				/** 
				 * Send an AUTH_OK packet to the client, 
				 * packet sequence is # 2 
				 */
				mysql_send_ok(dcb, 2, 0, NULL);
			} 
			else
			{
				protocol->protocol_auth_state = MYSQL_AUTH_FAILED;
				MXS_DEBUG("%lu [gw_read_client_event] session "
                                          "creation failed. fd %d, "
                                          "state = MYSQL_AUTH_FAILED.",
                                          pthread_self(),
                                          protocol->owner_dcb->fd);
				
				/** Send ERR 1045 to client */
				mysql_send_auth_error(
					dcb,
					2,
					0,
					"failed to create new session");
				
				dcb_close(dcb);
			}
		}
		else
		{
			char* fail_str = NULL;
			
			protocol->protocol_auth_state = MYSQL_AUTH_FAILED;
		
			if (auth_val == 2) {
				/** Send error 1049 to client */
				int message_len = 25 + MYSQL_DATABASE_MAXLEN;

				fail_str = calloc(1, message_len+1);
				snprintf(fail_str, message_len, "Unknown database '%s'", 
					 (char*)((MYSQL_session *)dcb->data)->db);

				modutil_send_mysql_err_packet(dcb, 2, 0, 1049, "42000", fail_str);
			} else {
				/** Send error 1045 to client */
				fail_str = create_auth_fail_str((char *)((MYSQL_session *)dcb->data)->user, 
							dcb->remote, 
							(char*)((MYSQL_session *)dcb->data)->client_sha1,
							(char*)((MYSQL_session *)dcb->data)->db,auth_val);
				modutil_send_mysql_err_packet(dcb, 2, 0, 1045, "28000", fail_str);
			}
			if (fail_str)
				free(fail_str);

			MXS_DEBUG("%lu [gw_read_client_event] after "
                                  "gw_mysql_do_authentication, fd %d, "
                                  "state = MYSQL_AUTH_FAILED.",
                                  pthread_self(),
                                  protocol->owner_dcb->fd);
			/**
			 * Release MYSQL_session since it is not used anymore.
			 */
			if (!DCB_IS_CLONE(dcb))
			{
				free(dcb->data);
			}
			dcb->data = NULL;
			
			dcb_close(dcb);
		}
		read_buffer = gwbuf_consume(read_buffer, nbytes_read);			
	}
        break;
        
	case MYSQL_AUTH_SSL_HANDSHAKE_DONE:
	{
	    int auth_val;

	    auth_val = gw_mysql_do_authentication(dcb, &read_buffer);


	    if (auth_val == 0)
	    {
		SESSION *session;

		protocol->protocol_auth_state = MYSQL_AUTH_RECV;
		/**
		 * Create session, and a router session for it.
		 * If successful, there will be backend connection(s)
		 * after this point.
		 */
		session = session_alloc(dcb->service, dcb);

		if (session != NULL)
		{
		    CHK_SESSION(session);
		    ss_dassert(session->state != SESSION_STATE_ALLOC && session->state != SESSION_STATE_DUMMY);

		    protocol->protocol_auth_state = MYSQL_IDLE;
		    /**
		     * Send an AUTH_OK packet to the client,
		     * packet sequence is # 2
		     */
		    mysql_send_ok(dcb, 3, 0, NULL);
		}
		else
		{
		    protocol->protocol_auth_state = MYSQL_AUTH_FAILED;
		    MXS_DEBUG("%lu [gw_read_client_event] session "
                              "creation failed. fd %d, "
                              "state = MYSQL_AUTH_FAILED.",
                              pthread_self(),
                              protocol->owner_dcb->fd);

		    /** Send ERR 1045 to client */
		    mysql_send_auth_error(
			    dcb,
				     3,
				     0,
				     "failed to create new session");

		    dcb_close(dcb);
		}
	    }
	    else
	    {
		char* fail_str = NULL;

		protocol->protocol_auth_state = MYSQL_AUTH_FAILED;

		if (auth_val == 2) {
		    /** Send error 1049 to client */
		    int message_len = 25 + MYSQL_DATABASE_MAXLEN;

		    fail_str = calloc(1, message_len+1);
		    snprintf(fail_str, message_len, "Unknown database '%s'",
			     (char*)((MYSQL_session *)dcb->data)->db);

		    modutil_send_mysql_err_packet(dcb, 3, 0, 1049, "42000", fail_str);
		}else {
		    /** Send error 1045 to client */
		    fail_str = create_auth_fail_str((char *)((MYSQL_session *)dcb->data)->user,
					     dcb->remote,
					     (char*)((MYSQL_session *)dcb->data)->client_sha1,
					     (char*)((MYSQL_session *)dcb->data)->db,auth_val);
		    modutil_send_mysql_err_packet(dcb, 3, 0, 1045, "28000", fail_str);
		}
		if (fail_str)
		    free(fail_str);

		MXS_DEBUG("%lu [gw_read_client_event] after "
                          "gw_mysql_do_authentication, fd %d, "
                          "state = MYSQL_AUTH_FAILED.",
                          pthread_self(),
                          protocol->owner_dcb->fd);
		/**
		 * Release MYSQL_session since it is not used anymore.
		 */
		if (!DCB_IS_CLONE(dcb))
		{
		    free(dcb->data);
		}
		dcb->data = NULL;

		dcb_close(dcb);
	    }
	    read_buffer = gwbuf_consume(read_buffer, nbytes_read);
	}
	break;

        case MYSQL_IDLE:
        {
                uint8_t* payload = NULL; 
		session_state_t ses_state;

                session = dcb->session;
                ss_dassert(session!= NULL && SESSION_STATE_DUMMY != session->state);
                
                if (session != NULL) 
                {
                        CHK_SESSION(session);
                }
		spinlock_acquire(&session->ses_lock);
		ses_state = session->state;
		spinlock_release(&session->ses_lock);
                /* Now, we are assuming in the first buffer there is
                 * the information form mysql command */
                payload = GWBUF_DATA(read_buffer);

		if(ses_state == SESSION_STATE_ROUTER_READY)
		{
		    /** Route COM_QUIT to backend */
		    if (MYSQL_IS_COM_QUIT(payload))
		    {
                        /** 
                         * Sends COM_QUIT packets since buffer is already
                         * created. A BREF_CLOSED flag is set so dcb_close won't
                         * send redundant COM_QUIT.
                         */
                        /* Temporarily suppressed: SESSION_ROUTE_QUERY(session, read_buffer); */
						/* Replaced with freeing the read buffer. */
				gwbuf_free(read_buffer);
                read_buffer = NULL;
            /**
			 * Close router session which causes closing of backends.
			 */
                        dcb_close(dcb);
		    }
		    else
		    {
			/** Reset error handler when routing of the new query begins */
			dcb->dcb_errhandle_called = false;
			
                        if (stmt_input)                                
                        {
			    /**
			     * Feed each statement completely and separately
			     * to router.
			     */
			    rc = route_by_statement(session, &read_buffer);

			    if (read_buffer != NULL)
			    {
				/** add incomplete mysql packet to read queue */
				dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, read_buffer);
                read_buffer = NULL;
			    }
                        }
                        else if (NULL != session->router_session || cap & RCAP_TYPE_NO_RSESSION)
                        {
			    /** Feed whole packet to router */
			    rc = SESSION_ROUTE_QUERY(session, read_buffer);
                read_buffer = NULL;
                        }
                        else
                        {
                            rc = 0;
                        }

                        /** Routing succeed */
                        if (rc)
			{
			    rc = 0; /**< here '0' means success */
                        }
                        else
			{
			    bool   succp;
			    GWBUF* errbuf;
			    /**
			     * Create error to be sent to client if session
			     * can't be continued.
			     */
			    errbuf = mysql_create_custom_error(
				    1,
							     0,
							     "Routing failed. Session is closed.");
			    /**
			     * Ensure that there are enough backends
			     * available.
			     */
			    router->handleError(
			    router_instance,
					     session->router_session,
					     errbuf,
					     dcb,
					     ERRACT_NEW_CONNECTION,
					     &succp);
			    gwbuf_free(errbuf);
			    /**
			     * If there are not enough backends close
			     * session
			     */
			    if (!succp)
			    {
				MXS_ERROR("Routing the query failed. "
                                          "Session will be closed.");
				
			    }
                            while (read_buffer)
                            {
                                read_buffer = gwbuf_consume(read_buffer, GWBUF_LENGTH(read_buffer));
                            }
                        }
		    }
		}
		else
		{
		    MXS_INFO("Session received a query in state %s",
                             STRSESSIONSTATE(ses_state));
		    while((read_buffer = GWBUF_CONSUME_ALL(read_buffer)) != NULL);
		    goto return_rc;
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

        if (protocol->protocol_auth_state == MYSQL_IDLE)
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
 * EPOLLOUT event arrived and as a consequence, client input buffer (writeq) is
 * flushed. The data is encrypted and SSL is used. The SSL handshake must have
 * been successfully completed prior to this function being called.
 * @param client dcb
 * @return constantly 1
 */
int gw_write_client_event_SSL(DCB *dcb)
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

        if (protocol->protocol_auth_state == MYSQL_IDLE)
        {
		dcb_drain_writeq_SSL(dcb);
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
 * Bind the DCB to a network port or a UNIX Domain Socket.
 * @param listen_dcb Listener DCB
 * @param config_bind Bind address in either IP:PORT format for network sockets or PATH for UNIX Domain Sockets
 * @return 1 on success, 0 on error
 */
int gw_MySQLListener(DCB *listen_dcb,
                     char *config_bind)
{
    int l_so;
    struct sockaddr_in serv_addr;
    struct sockaddr_un local_addr;
    struct sockaddr *current_addr;
    int one = 1;
    int rc;
    bool is_tcp = false;
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(&local_addr, 0, sizeof(local_addr));

    if (strchr(config_bind, '/'))
    {
        char *tmp = strrchr(config_bind, ':');
        if (tmp)
            *tmp = '\0';

        // UNIX socket create
        if ((l_so = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
        {
            char errbuf[STRERROR_BUFLEN];
            MXS_ERROR("Can't create UNIX socket: %i, %s",
                      errno,
                      strerror_r(errno, errbuf, sizeof(errbuf)));
            return 0;
        }
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sun_family = AF_UNIX;
        strncpy(local_addr.sun_path, config_bind, sizeof(local_addr.sun_path) - 1);

        current_addr = (struct sockaddr *) &local_addr;

    }
    else
    {
        /* This is partially dead code, MaxScale will never start without explicit
         * ports defined for all listeners. Thus the default port is never used.
         */
        if (!parse_bindconfig(config_bind, 4406, &serv_addr))
        {
            MXS_ERROR("Error in parse_bindconfig for [%s]", config_bind);
            return 0;
        }

        /** Create the TCP socket */
        if ((l_so = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        {
            char errbuf[STRERROR_BUFLEN];
            MXS_ERROR("Can't create socket: %i, %s",
                      errno,
                      strerror_r(errno, errbuf, sizeof(errbuf)));
            return 0;
        }

        current_addr = (struct sockaddr *) &serv_addr;
        is_tcp = true;
    }

    listen_dcb->fd = -1;

    // socket options
    if (setsockopt(l_so, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof(one)) != 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed to set socket options. Error %d: %s",
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
    }

    if (is_tcp)
    {
        char errbuf[STRERROR_BUFLEN];
        if (setsockopt(l_so, IPPROTO_TCP, TCP_NODELAY, (char *) &one, sizeof(one)) != 0)
        {
            MXS_ERROR("Failed to set socket options. Error %d: %s",
                      errno,
                      strerror_r(errno, errbuf, sizeof(errbuf)));
        }
    }
    // set NONBLOCKING mode
    if (setnonblocking(l_so) != 0)
    {
        MXS_ERROR("Failed to set socket to non-blocking mode.");
        close(l_so);
        return 0;
    }

    /* get the right socket family for bind */
    switch (current_addr->sa_family)
    {
        case AF_UNIX:
            rc = unlink(config_bind);
            if ((rc == -1) && (errno != ENOENT))
            {
                char errbuf[STRERROR_BUFLEN];
                MXS_ERROR("Failed to unlink Unix Socket %s: %d %s",
                          config_bind, errno, strerror_r(errno, errbuf, sizeof(errbuf)));
            }

            if (bind(l_so, (struct sockaddr *) &local_addr, sizeof(local_addr)) < 0)
            {
                char errbuf[STRERROR_BUFLEN];
                MXS_ERROR("Failed to bind to UNIX Domain socket '%s': %i, %s",
                          config_bind,
                          errno,
                          strerror_r(errno, errbuf, sizeof(errbuf)));
                close(l_so);
                return 0;
            }

            /* set permission for all users */
            if (chmod(config_bind, 0777) < 0)
            {
                char errbuf[STRERROR_BUFLEN];
                MXS_ERROR("Failed to change permissions on UNIX Domain socket '%s': %i, %s",
                          config_bind,
                          errno,
                          strerror_r(errno, errbuf, sizeof(errbuf)));
            }
            break;

        case AF_INET:
            if (bind(l_so, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
            {
                char errbuf[STRERROR_BUFLEN];
                MXS_ERROR("Failed to bind on '%s': %i, %s",
                          config_bind,
                          errno,
                          strerror_r(errno, errbuf, sizeof(errbuf)));
                close(l_so);
                return 0;
            }
            break;

        default:
            MXS_ERROR("Socket Family %i not supported\n", current_addr->sa_family);
            close(l_so);
            return 0;
    }

    if (listen(l_so, 10 * SOMAXCONN) != 0)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed to start listening on '%s': %d, %s",
                  config_bind,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        close(l_so);
        return 0;
    }

    MXS_NOTICE("Listening MySQL connections at %s", config_bind);

    // assign l_so to dcb
    listen_dcb->fd = l_so;

    // add listening socket to poll structure
    if (poll_add_dcb(listen_dcb) != 0)
    {
        MXS_ERROR("MaxScale encountered system limit while "
                  "attempting to register on an epoll instance.");
        return 0;
    }
#if defined(FAKE_CODE)
    conn_open[l_so] = true;
#endif /* FAKE_CODE */
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
	struct sockaddr    client_conn;
	socklen_t          client_len = sizeof(struct sockaddr_storage);
        int                sendbuf = GW_BACKEND_SO_SNDBUF;
        socklen_t          optlen = sizeof(sendbuf);
        int                eno = 0;
	int		   syseno = 0;
        int                i = 0;
                
        CHK_DCB(listener);
        
	while (1) {

    retry_accept:

#if defined(FAKE_CODE)
                if (fail_next_accept > 0)
                {
                        c_sock = -1;
                        eno = fail_accept_errno;
                        fail_next_accept -= 1;
                } else {
                        fail_accept_errno = 0;          
#endif /* FAKE_CODE */
                        // new connection from client
		        c_sock = accept(listener->fd,
                                        (struct sockaddr *) &client_conn,
                                        &client_len);
                        eno = errno;
                        errno = 0;
#if defined(FAKE_CODE)
                }
#endif /* FAKE_CODE */
                        
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
				struct timespec ts1;
				ts1.tv_sec = 0;				
                                /**
                                 * Exceeded system's (ENFILE) or processes
                                 * (EMFILE) max. number of files limit.
                                 */
                                char errbuf[STRERROR_BUFLEN];
                                MXS_DEBUG("%lu [gw_MySQLAccept] Error %d, %s. ",
                                          pthread_self(),
                                          eno,
                                          strerror_r(eno, errbuf, sizeof(errbuf)));
                                
                                if (i == 0)
                                {
                                        char errbuf[STRERROR_BUFLEN];
                                        MXS_ERROR("Error %d, %s. "
                                                  "Failed to accept new client "
                                                  "connection.",
                                                  eno,
                                                  strerror_r(eno, errbuf, sizeof(errbuf)));
                                }
                                i++;
				ts1.tv_nsec = 100*i*i*1000000;
				nanosleep(&ts1, NULL);
				
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
                                char errbuf[STRERROR_BUFLEN];
                                MXS_DEBUG("%lu [gw_MySQLAccept] Error %d, %s.",
                                          pthread_self(),
                                          eno,
                                          strerror_r(eno, errbuf, sizeof(errbuf)));
                                MXS_ERROR("Failed to accept new client "
                                          "connection due to %d, %s.",
                                          eno,
                                          strerror_r(eno, errbuf, sizeof(errbuf)));
                                rc = 1;
                                goto return_rc;
                        } /* if (eno == ..) */
		} /* if (c_sock == -1) */
                /* reset counter */
                i = 0;
                
                listener->stats.n_accepts++;
#if defined(SS_DEBUG)
                MXS_DEBUG("%lu [gw_MySQLAccept] Accepted fd %d.",
                          pthread_self(),
                          c_sock);
#endif /* SS_DEBUG */
#if defined(FAKE_CODE)
                conn_open[c_sock] = true;
#endif /* FAKE_CODE */
                /* set nonblocking  */
        	sendbuf = GW_CLIENT_SO_SNDBUF;
                char errbuf[STRERROR_BUFLEN];

			if((syseno = setsockopt(c_sock, SOL_SOCKET, SO_SNDBUF, &sendbuf, optlen)) != 0){
                            MXS_ERROR("Failed to set socket options. Error %d: %s",
                                      errno, strerror_r(errno, errbuf, sizeof(errbuf)));
			}

        	sendbuf = GW_CLIENT_SO_RCVBUF;

			if((syseno = setsockopt(c_sock, SOL_SOCKET, SO_RCVBUF, &sendbuf, optlen)) != 0){
                            MXS_ERROR("Failed to set socket options. Error %d: %s",
                                      errno, strerror_r(errno, errbuf, sizeof(errbuf)));
			}
                setnonblocking(c_sock);
                
                client_dcb = dcb_alloc(DCB_ROLE_REQUEST_HANDLER);

		if (client_dcb == NULL) {
                    MXS_ERROR("Failed to create DCB object for client connection.");
                    close(c_sock);
                    rc = 1;
                    goto return_rc;
		}

                client_dcb->service = listener->session->service;
                client_dcb->session = session_set_dummy(client_dcb);
                client_dcb->fd = c_sock;

		// get client address
		if ( client_conn.sa_family == AF_UNIX) 
                {
			// client address
			client_dcb->remote = strdup("localhost_from_socket");
			// set localhost IP for user authentication
  			(client_dcb->ipv4).sin_addr.s_addr = 0x0100007F;
		} 
		else 
                {
  			/* client IPv4 in raw data*/
			memcpy(&client_dcb->ipv4, 
                               (struct sockaddr_in *)&client_conn, 
                               sizeof(struct sockaddr_in));	
			/* client IPv4 in string representation */
			client_dcb->remote = (char *)calloc(INET_ADDRSTRLEN+1, sizeof(char));
                        
			if (client_dcb->remote != NULL) 
                        {
				inet_ntop(AF_INET, 
                                          &(client_dcb->ipv4).sin_addr, 
                                          client_dcb->remote,
                                          INET_ADDRSTRLEN);
			}
		}
                protocol = mysql_protocol_init(client_dcb, c_sock);
                ss_dassert(protocol != NULL);
                
                if (protocol == NULL) {
                        /** delete client_dcb */
                        dcb_close(client_dcb);
                        MXS_ERROR("%lu [gw_MySQLAccept] Failed to create "
                                  "protocol object for client connection.",
                                  pthread_self());
                        rc = 1;
                        goto return_rc;
                }
                client_dcb->protocol = protocol;
                // assign function poiters to "func" field
                memcpy(&client_dcb->func, &MyObject, sizeof(GWPROTOCOL));
                //send handshake to the client_dcb
                MySQLSendHandshake(client_dcb);

                // client protocol state change
                protocol->protocol_auth_state = MYSQL_AUTH_SENT;

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
                                "MaxScale encountered system limit while "
                                "attempting to register on an epoll instance.");
                        
                        /** close client_dcb */
                        dcb_close(client_dcb);

                        /** Previous state is recovered in poll_add_dcb. */
                        MXS_ERROR("%lu [gw_MySQLAccept] Failed to add dcb %p for "
                                  "fd %d to epoll set.",
                                  pthread_self(),
                                  client_dcb,
                                  client_dcb->fd);
                        rc = 1;
                        goto return_rc;
                }
                else
                {
                    MXS_DEBUG("%lu [gw_MySQLAccept] Added dcb %p for fd "
                              "%d to epoll set.",
                              pthread_self(),
                              client_dcb,
                              client_dcb->fd);
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

static int gw_error_client_event(
        DCB* dcb) 
{
        SESSION* session;

        CHK_DCB(dcb);
        
        session = dcb->session;
        
        MXS_DEBUG("%lu [gw_error_client_event] Error event handling for DCB %p "
                  "in state %s, session %p.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state),
                  (session != NULL ? session : NULL));
        
        if (session != NULL && session->state == SESSION_STATE_STOPPING)
        {
                goto retblock;
        }
        
#if defined(SS_DEBUG)
        MXS_DEBUG("Client error event handling.");
#endif
        dcb_close(dcb);
        
retblock:
        return 1;
}

static int
gw_client_close(DCB *dcb)
{
        SESSION*       session;
        ROUTER_OBJECT* router;
        void*          router_instance;
#if defined(SS_DEBUG)
        MySQLProtocol* protocol = (MySQLProtocol *)dcb->protocol;
        if (dcb->state == DCB_STATE_POLLING ||
            dcb->state == DCB_STATE_NOPOLLING ||
            dcb->state == DCB_STATE_ZOMBIE)
        {
		if (!DCB_IS_CLONE(dcb)) CHK_PROTOCOL(protocol);
        }
#endif
	MXS_DEBUG("%lu [gw_client_close]", pthread_self());
	mysql_protocol_done(dcb);
        session = dcb->session;
        /**
         * session may be NULL if session_alloc failed.
         * In that case, router session wasn't created.
         */
        if (session != NULL && SESSION_STATE_DUMMY != session->state)
        {
                CHK_SESSION(session);
                spinlock_acquire(&session->ses_lock);
                
                if (session->state != SESSION_STATE_STOPPING)
                {
			session->state = SESSION_STATE_STOPPING;
		}
                router_instance = session->service->router_instance;		
		router = session->service->router;
		/**
		 * If router session is being created concurrently router 
		 * session might be NULL and it shouldn't be closed.
		 */
		if (session->router_session != NULL)
		{
			spinlock_release(&session->ses_lock);
			/** Close router session and all its connections */
			router->closeSession(router_instance, session->router_session);
		}
		else
		{
			spinlock_release(&session->ses_lock);
		}
        }
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
        SESSION* session;

        CHK_DCB(dcb);
        session = dcb->session;
        
        if (session != NULL && session->state == SESSION_STATE_ROUTER_READY)
        {
                CHK_SESSION(session);
        }
        
        if (session != NULL && session->state == SESSION_STATE_STOPPING)
        {
                goto retblock;
        }

        dcb_close(dcb);
 
retblock:
        return 1;
}


/**
 * Detect if buffer includes partial mysql packet or multiple packets.
 * Store partial packet to dcb_readqueue. Send complete packets one by one
 * to router.
 * 
 * It is assumed readbuf includes at least one complete packet. 
 * Return 1 in success. If the last packet is incomplete return success but
 * leave incomplete packet to readbuf.
 * 
 * @param session	Session pointer
 * @param p_readbuf	Pointer to the address of GWBUF including the query
 * 
 * @return 1 if succeed, 
 */
static int route_by_statement(
        SESSION* session, 
        GWBUF**  p_readbuf)
{
        int            rc;
        GWBUF*         packetbuf;
#if defined(SS_DEBUG)
        GWBUF*         tmpbuf;
        
        tmpbuf = *p_readbuf;
        while (tmpbuf != NULL)
        {
                ss_dassert(GWBUF_IS_TYPE_MYSQL(tmpbuf));
                tmpbuf=tmpbuf->next;
        }
#endif
        do 
        {
                ss_dassert(GWBUF_IS_TYPE_MYSQL((*p_readbuf)));

		/** 
		 * Collect incoming bytes to a buffer until complete packet has 
		 * arrived and then return the buffer.
		 */
                packetbuf = gw_MySQL_get_next_packet(p_readbuf);
                
                if (packetbuf != NULL)
                {
                        CHK_GWBUF(packetbuf);
                        ss_dassert(GWBUF_IS_TYPE_MYSQL(packetbuf));
                        /**
                         * This means that buffer includes exactly one MySQL 
                         * statement.
                         * backend func.write uses the information. MySQL backend
                         * protocol, for example, stores the command identifier 
                         * to protocol structure. When some other thread reads
                         * the corresponding response the command tells how to
                         * handle response.
                         * 
                         * Set it here instead of gw_read_client_event to make 
                         * sure it is set to each (MySQL) packet.
                         */
                        gwbuf_set_type(packetbuf, GWBUF_TYPE_SINGLE_STMT);
                        /** Route query */
                        rc = SESSION_ROUTE_QUERY(session, packetbuf);
                }
                else
                {
                        rc = 1;
                        goto return_rc;
                }
        }
        while (rc == 1 && *p_readbuf != NULL);
        
return_rc:
        return rc;
}

/**
 * Do the SSL authentication handshake.
 * This creates the DCB SSL structure if one has not been created and starts the
 * SSL handshake handling.
 * @param protocol Protocol to connect with SSL
 * @return 1 on success, 0 when the handshake is ongoing or -1 on error
 */
int do_ssl_accept(MySQLProtocol* protocol)
{
    int rval,errnum;
    char errbuf[2014];
    DCB* dcb = protocol->owner_dcb;
    if(dcb->ssl == NULL)
    {
	if(dcb_create_SSL(dcb) != 0)
	{
	    return -1;
	}
    }

    rval = dcb_accept_SSL(dcb);
    
    switch(rval)
    {
    case 0:
	/** Not all of the data has been read. Go back to the poll
	 queue and wait for more.*/

	rval = 0;
	MXS_INFO("SSL_accept ongoing for %s@%s",
                 protocol->owner_dcb->user,
                 protocol->owner_dcb->remote);
	return 0;
	break;
    case 1:
	spinlock_acquire(&protocol->protocol_lock);
	protocol->protocol_auth_state = MYSQL_AUTH_SSL_HANDSHAKE_DONE;
	protocol->use_ssl = true;
	spinlock_release(&protocol->protocol_lock);

	spinlock_acquire(&dcb->authlock);
	dcb->func.write = gw_MySQLWrite_client_SSL;
	dcb->func.write_ready = gw_write_client_event_SSL;
	spinlock_release(&dcb->authlock);

	rval = 1;

	MXS_INFO("SSL_accept done for %s@%s",
                 protocol->owner_dcb->user,
                 protocol->owner_dcb->remote);
	break;

    case -1:

	    spinlock_acquire(&protocol->protocol_lock);
	    protocol->protocol_auth_state = MYSQL_AUTH_SSL_HANDSHAKE_FAILED;
	    spinlock_release(&protocol->protocol_lock);
	    rval = -1;
	    MXS_ERROR("Fatal error in SSL_accept for %s",
                      protocol->owner_dcb->remote);
	break;

    default:
	MXS_ERROR("Fatal error in SSL_accept, returned value was %d.", rval);
	break;
    }
#ifdef SS_DEBUG
    MXS_DEBUG("[do_ssl_accept] Protocol state: %s",
              gw_mysql_protocol_state2string(protocol->protocol_auth_state));
#endif

    return rval;
}
