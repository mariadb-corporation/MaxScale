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

/*
 * MySQL Protocol common routines for client to gateway and gateway to backend
 *
 * Revision History
 * Date         Who                     Description
 * 17/06/2013   Massimiliano Pinto      Common MySQL protocol routines
 * 02/06/2013	Massimiliano Pinto	MySQL connect asynchronous phases
 * 04/09/2013	Massimiliano Pinto	Added dcb NULL assert in mysql_send_custom_error
 * 12/09/2013	Massimiliano Pinto	Added checks in gw_decode_mysql_server_handshake and gw_read_backend_handshake
 *
 */

#include "mysql_client_server_protocol.h"
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

extern int gw_read_backend_event(DCB* dcb);
extern int gw_write_backend_event(DCB *dcb);
extern int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
extern int gw_error_backend_event(DCB *dcb);


/** 
 * @node Creates MySQL protocol structure 
 *
 * Parameters:
 * @param dcb - in, use
 *          Must be non-NULL.
 *
 * @return 
 *
 * 
 * @details Protocol structure does not have fd because dcb is not
 * connected yet. 
 *
 */
MySQLProtocol* mysql_protocol_init(
        DCB* dcb,
        int  fd)
{
        MySQLProtocol* p;
        
	p = (MySQLProtocol *) calloc(1, sizeof(MySQLProtocol));
        ss_dassert(p != NULL);
        
        if (p == NULL) {
            int eno = errno;
            errno = 0;
            skygw_log_write_flush(
                    LOGFILE_ERROR,
                    "%lu [mysql_init_protocol] MySQL protocol init failed : "
                    "memory allocation due error  %d, %s.",
                    pthread_self(),
                    eno,
                    strerror(eno));
            goto return_p;
        }
	p->state = MYSQL_ALLOC;
#if defined(SS_DEBUG)
        p->protocol_chk_top = CHK_NUM_PROTOCOL;
        p->protocol_chk_tail = CHK_NUM_PROTOCOL;
#endif
        /** Assign fd with protocol */
        p->fd = fd;
	p->owner_dcb = dcb;
        CHK_PROTOCOL(p);
return_p:
        return p;
}


/**
 * gw_mysql_close
 *
 * close a connection if opened
 * free data scructure for MySQLProtocol
 *
 * @param ptr The MySQLProtocol ** to close/free
 *
 */
void gw_mysql_close(MySQLProtocol **ptr) {
	MySQLProtocol *conn = *ptr;

        ss_dassert(*ptr != NULL);
        
	if (*ptr == NULL)
		return;

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Closing MySQL connection %i, [%s]\n", conn->fd, conn->scramble);
#endif

	if (conn->fd > 0) {
		/* COM_QUIT will not be sent here, but from the caller of this routine! */
#ifdef MYSQL_CONN_DEBUG
		fprintf(stderr, "mysqlgw_mysql_close() called for %i\n", conn->fd);
#endif
		close(conn->fd);
	} else {
#ifdef MYSQL_CONN_DEBUG
		fprintf(stderr, "mysqlgw_mysql_close() called, no socket %i\n", conn->fd);
#endif
	}

	free(*ptr);

	*ptr = NULL;

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "mysqlgw_mysql_close() free(conn) done\n");
#endif
}

/**
 * Read the backend server MySQL handshake  
 *
 * @param conn	MySQL protocol structure
 * @return 0 on success, 1 on failure
 */
int gw_read_backend_handshake(MySQLProtocol *conn) {
	GWBUF *head = NULL;
	DCB *dcb = conn->owner_dcb;
	int n = -1;
	uint8_t *payload = NULL;
	int h_len = 0;
	int  success = 0;
	int packet_len = 0;

	if ((n = dcb_read(dcb, &head)) != -1) {
		if (head) {
			payload = GWBUF_DATA(head);
			h_len = gwbuf_length(head);

			/*
			 * The mysql packets content starts at byte fifth
			 * just return with less bytes
			 */

			if (h_len <= 4) {
				/* log error this exit point */
				conn->state = MYSQL_AUTH_FAILED;
				return 1;
			}

			//get mysql packet size, 3 bytes
			packet_len = gw_mysql_get_byte3(payload);

			if (h_len < (packet_len + 4)) {
				/*
				 * data in buffer less than expected in the
                                 * packet. Log error this exit point
				 */
				conn->state = MYSQL_AUTH_FAILED;
				return 1;
			}

			// skip the 4 bytes header
			payload += 4;

			//Now decode mysql handshake
			success = gw_decode_mysql_server_handshake(conn,
                                                                   payload);

			if (success < 0) {
				/* MySQL handshake has not been properly decoded
				 * we cannot continue
				 * log error this exit point
				 */
				conn->state = MYSQL_AUTH_FAILED;
				return 1;
			}

			conn->state = MYSQL_AUTH_SENT;

			// consume all the data here
			head = gwbuf_consume(head, gwbuf_length(head));

			return 0;
		}
	}
	
	// Nothing done here, log error this
	return 1;
}

/**
 * gw_decode_mysql_server_handshake
 *
 * Decode mysql server handshake
 *
 * @param conn The MySQLProtocol structure
 * @param payload The bytes just read from the net
 * @return 0 on success, < 0 on failure
 * 
 */ 
int gw_decode_mysql_server_handshake(MySQLProtocol *conn, uint8_t *payload) {
	uint8_t *server_version_end = NULL;
	uint16_t mysql_server_capabilities_one = 0;
	uint16_t mysql_server_capabilities_two = 0;
	unsigned long tid =0;
	uint8_t scramble_data_1[GW_SCRAMBLE_LENGTH_323] = "";
	uint8_t scramble_data_2[GW_MYSQL_SCRAMBLE_SIZE - GW_SCRAMBLE_LENGTH_323] = "";
	uint8_t capab_ptr[4] = "";
	int scramble_len = 0;
	uint8_t scramble[GW_MYSQL_SCRAMBLE_SIZE] = "";
	int protocol_version = 0;

        protocol_version = payload[0];

	if (protocol_version != GW_MYSQL_PROTOCOL_VERSION) {
		/* log error for this */
		return -1;
	}

	payload++;

	// Get server version (string)
	server_version_end = (uint8_t *) gw_strend((char*) payload);

	payload = server_version_end + 1;

	// get ThreadID: 4 bytes
	tid = gw_mysql_get_byte4(payload);
	memcpy(&conn->tid, &tid, 4);

	payload +=4;

	// scramble_part 1
	memcpy(scramble_data_1, payload, GW_SCRAMBLE_LENGTH_323);
	payload += GW_SCRAMBLE_LENGTH_323;

	// 1 filler
	payload++;

	mysql_server_capabilities_one = gw_mysql_get_byte2(payload);

	//Get capabilities_part 1 (2 bytes) + 1 language + 2 server_status
	payload +=5;

	mysql_server_capabilities_two = gw_mysql_get_byte2(payload);

	memcpy(&capab_ptr, &mysql_server_capabilities_one, 2);

	// get capabilities part 2 (2 bytes)
	memcpy(&(capab_ptr[2]), &mysql_server_capabilities_two, 2);

	// 2 bytes shift 
	payload+=2;

	// get scramble len
	scramble_len = payload[0] -1;
        ss_dassert(scramble_len > GW_SCRAMBLE_LENGTH_323);
        ss_dassert(scramble_len <= GW_MYSQL_SCRAMBLE_SIZE);

	if ( (scramble_len < GW_SCRAMBLE_LENGTH_323) || scramble_len > GW_MYSQL_SCRAMBLE_SIZE) {
		/* log this */
		return -2;
	}
        
	// skip 10 zero bytes
	payload += 11;
        
	// copy the second part of the scramble
	memcpy(scramble_data_2, payload, scramble_len - GW_SCRAMBLE_LENGTH_323);

	memcpy(scramble, scramble_data_1, GW_SCRAMBLE_LENGTH_323);
	memcpy(scramble + GW_SCRAMBLE_LENGTH_323, scramble_data_2, scramble_len - GW_SCRAMBLE_LENGTH_323);

	// full 20 bytes scramble is ready
	memcpy(conn->scramble, scramble, GW_MYSQL_SCRAMBLE_SIZE);

	return 0;
}


/**
 * Receive the MySQL authentication packet from backend, packet # is 2
 *
 * @param conn The MySQL protocol structure
 * @return -1 in case of failure, 0 if there was nothing to read, 1 if read
 * was successful.
 */
int gw_receive_backend_auth(
        MySQLProtocol *protocol)
{
	int n = -1;
	GWBUF   *head = NULL;
	DCB     *dcb = protocol->owner_dcb;
	uint8_t *ptr = NULL;
        int      rc = 0;

        n = dcb_read(dcb, &head);

        /**
         * Read didn't fail and there is enough data for mysql packet.
         */
        if (n != -1 &&
            head != NULL &&
            GWBUF_LENGTH(head) >= 5)
        {
                ptr = GWBUF_DATA(head);
                /**
                 * 5th byte is 0x0 if successful.
                 */
                if (ptr[4] == '\x00') {
                        rc = 1;
                } else {
                        uint8_t* tmpbuf =
                                (uint8_t *)calloc(1, GWBUF_LENGTH(head)+1);
                        memcpy(tmpbuf, ptr, GWBUF_LENGTH(head));
                        skygw_log_write(
                                LOGFILE_TRACE,
                                "%lu [gw_receive_backend_auth] Invalid "
                                "authentication message from backend dcb %p "
                                "fd %d, ptr[4] = %p, msg %s.",
                                pthread_self(),
                                dcb,
                                dcb->fd,
                                tmpbuf[4],
                                tmpbuf);
                        
                        skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Invalid authentication message from "
                                "backend server. Authentication failed.");
                                free(tmpbuf);
                                rc = -1;
                }
                /**
                 * Remove data from buffer.
                 */
                head = gwbuf_consume(head, gwbuf_length(head));
        }
        else if (n == 0)
        {
                /**
                 * This is considered as success because call didn't fail,
                 * although no bytes was read.
                 */
                rc = 0;
                skygw_log_write(
                        LOGFILE_TRACE,
                        "%lu [gw_receive_backend_auth] Read zero bytes from "
                        "backend dcb %p fd %d in state %s. n %d, head %p, len %d",
                        pthread_self(),
                        dcb,
                        dcb->fd,
                        STRDCBSTATE(dcb->state),
                        n,
                        head,
                        (head == NULL) ? 0 : GWBUF_LENGTH(head));
        }
        else
        {
                ss_dassert(n < 0 && head == NULL);
                rc = -1;
                skygw_log_write(
                        LOGFILE_TRACE,
                        "%lu [gw_receive_backend_auth] Reading from backend dcb %p "
                        "fd %d in state %s failed. n %d, head %p, len %d",
                        pthread_self(),
                        dcb,
                        dcb->fd,
                        STRDCBSTATE(dcb->state),
                        n,
                        head,
                        (head == NULL) ? 0 : GWBUF_LENGTH(head));
        }
        
        return rc;
}

/**
 * Write MySQL authentication packet to backend server
 *
 * @param conn  MySQL protocol structure
 * @param dbname The selected database
 * @param user The selected user
 * @param passwd The SHA1(real_password): Note real_password is unknown
 * @return 0 on success, 1 on failure
 */
int gw_send_authentication_to_backend(
        char *dbname,
        char *user,
        uint8_t *passwd,
        MySQLProtocol *conn)
{
        int compress = 0;
        int rv;
        uint8_t *payload = NULL;
        uint8_t *payload_start = NULL;
        long bytes;
        uint8_t client_scramble[GW_MYSQL_SCRAMBLE_SIZE];
        uint8_t client_capabilities[4];
        uint32_t server_capabilities;
        uint32_t final_capabilities;
        char dbpass[129]="";
	GWBUF *buffer;
	DCB *dcb;

        char *curr_db = NULL;
        uint8_t *curr_passwd = NULL;

        if (strlen(dbname))
                curr_db = dbname;

        if (strlen((char *)passwd))
                curr_passwd = passwd;

	dcb = conn->owner_dcb;

#ifdef DEBUG_MYSQL_CONN
	fprintf(stderr, ">> Sending credentials %s, %s, db %s\n", user, passwd, dbname);
#endif

	// Zero the vars
	memset(&server_capabilities, '\0', sizeof(server_capabilities));
	memset(&final_capabilities, '\0', sizeof(final_capabilities));

        final_capabilities = gw_mysql_get_byte4((uint8_t *)&server_capabilities);

        final_capabilities |= GW_MYSQL_CAPABILITIES_PROTOCOL_41;
        final_capabilities |= GW_MYSQL_CAPABILITIES_CLIENT;

        if (compress) {
                final_capabilities |= GW_MYSQL_CAPABILITIES_COMPRESS;
#ifdef DEBUG_MYSQL_CONN
                fprintf(stderr, ">>>> Backend Connection with compression\n");
#endif
        }

        if (curr_passwd != NULL) {
                uint8_t hash1[GW_MYSQL_SCRAMBLE_SIZE]="";
                uint8_t hash2[GW_MYSQL_SCRAMBLE_SIZE]="";
                uint8_t new_sha[GW_MYSQL_SCRAMBLE_SIZE]="";

		// hash1 is the function input, SHA1(real_password)
                memcpy(hash1, passwd, GW_MYSQL_SCRAMBLE_SIZE);

		// hash2 is the SHA1(input data), where input_data = SHA1(real_password)
                gw_sha1_str(hash1, GW_MYSQL_SCRAMBLE_SIZE, hash2);

		// dbpass is the HEX form of SHA1(SHA1(real_password))
                gw_bin2hex(dbpass, hash2, GW_MYSQL_SCRAMBLE_SIZE);

		// new_sha is the SHA1(CONCAT(scramble, hash2)
                gw_sha1_2_str(conn->scramble, GW_MYSQL_SCRAMBLE_SIZE, hash2, GW_MYSQL_SCRAMBLE_SIZE, new_sha);

		// compute the xor in client_scramble
                gw_str_xor(client_scramble, new_sha, hash1, GW_MYSQL_SCRAMBLE_SIZE);

        }

        if (curr_db == NULL) {
                // without db
                final_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
        } else {
                final_capabilities |= GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
        }

        final_capabilities |= GW_MYSQL_CAPABILITIES_PLUGIN_AUTH;

        gw_mysql_set_byte4(client_capabilities, final_capabilities);

	// Protocol MySQL HandshakeResponse for CLIENT_PROTOCOL_41
	// 4 bytes capabilities + 4 bytes max packet size + 1 byte charset + 23 '\0' bytes
        // 4 + 4 + 1 + 23  = 32
        bytes = 32;

        bytes += strlen(user);
        // the NULL
        bytes++;

	// next will be + 1 (scramble_len) + 20 (fixed_scramble) + 1 (user NULL term) + 1 (db NULL term)

        if (curr_passwd != NULL) {
                bytes++;
                bytes += GW_MYSQL_SCRAMBLE_SIZE;
	} else {
                bytes++;
	}	

        if (curr_db != NULL) {
                bytes += strlen(curr_db);
        	bytes++;
	}

        bytes +=strlen("mysql_native_password");
        bytes++;

        // the packet header
        bytes += 4;

	// allocating the GWBUF
	buffer = gwbuf_alloc(bytes);
	payload = GWBUF_DATA(buffer);

	// clearing data
	memset(payload, '\0', bytes);
	
	// save the start pointer
	payload_start = payload;

	// set packet # = 1
        payload[3] = '\x01';
        payload += 4;

	// set client capabilities
        memcpy(payload, client_capabilities, 4);

        // set now the max-packet size
        payload += 4;
        gw_mysql_set_byte4(payload, 16777216);

        // set the charset
        payload += 4;
        *payload = '\x08';

        payload++;

	// 23 bytes of 0
        payload += 23;

        // 4 + 4 + 4 + 1 + 23 = 36, this includes the 4 bytes packet header

	memcpy(payload, user, strlen(user));
        payload += strlen(user);
        payload++;

        if (curr_passwd != NULL) {
                // set the auth-length
                *payload = GW_MYSQL_SCRAMBLE_SIZE;
                payload++;

                //copy the 20 bytes scramble data after packet_buffer+36+user+NULL+1 (byte of auth-length)
                memcpy(payload, client_scramble, GW_MYSQL_SCRAMBLE_SIZE);

                payload += GW_MYSQL_SCRAMBLE_SIZE;

        } else {
                // skip the auth-length and write a NULL
                payload++;
        }

        // if the db is not NULL append it
        if (curr_db != NULL) {
                memcpy(payload, curr_db, strlen(curr_db));
                payload += strlen(curr_db);
                payload++;
        }

        memcpy(payload,
               "mysql_native_password",
               strlen("mysql_native_password"));
        payload += strlen("mysql_native_password");
        payload++;

	// put here the paylod size: bytes to write - 4 bytes packet header
        gw_mysql_set_byte3(payload_start, (bytes-4));

        rv = dcb_write(dcb, buffer);

        if (rv < 0) {
                return rv;
        } else {
                return 0;
        }
}

/**
 * gw_do_connect_to_backend
 *
 * This routine creates socket and connects to a backend server.
 * Connect it non-blocking operation. If connect fails, socket is closed.
 *
 * @param host The host to connect to
 * @param port The host TCP/IP port 
 * @param *fd where connected fd is copied
 * @return 0/1 on success and -1 on failure
 * If succesful, fd has file descriptor to socket which is connected to
 * backend server. In failure, fd == -1 and socket is closed.
 *
 */
int gw_do_connect_to_backend(
        char          *host,
        int           port,
        int*          fd)
{
	struct sockaddr_in serv_addr;
	int rv;
	int so = 0;
        
	memset(&serv_addr, 0, sizeof serv_addr);
	serv_addr.sin_family = AF_INET;
	so = socket(AF_INET,SOCK_STREAM,0);
        
	if (so < 0) {
                int eno = errno;
                errno = 0;
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "%lu [gw_do_connect_to_backend] Establishing connection "
                        "to backend server failed. Socket creation failed due "
                        "%d, %s.",
                        pthread_self(),
                        eno,
                        strerror(eno));
                rv = -1;
                goto return_rv;
	}
	/* prepare for connect */
	setipaddress(&serv_addr.sin_addr, host);
	serv_addr.sin_port = htons(port);
	/* set socket to as non-blocking here */
	setnonblocking(so);
        rv = connect(so, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

        if (rv != 0) {
                int eno = errno;
                errno = 0;
                
                if (eno == EINPROGRESS) {
                        rv = 1;
                } else {
                        int rc;
                        int oldfd = so;
                        
                        skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "%lu [gw_do_connect_to_backend] Failed to "
                                "connect backend server %s:%d, "
                                "due %d, %s.",
                                pthread_self(),
                                host,
                                port,
                                eno,
                                strerror(eno));
                        /** Close newly created socket. */
                        rc = close(so);

                        if (rc != 0) {
                                int eno = errno;
                                errno = 0;
                                skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "%lu [gw_do_connect_to_backend] Failed to "
                                        "close socket %d due %d, %s.",
                                        pthread_self(),
                                        oldfd,
                                        eno,
                                        strerror(eno));
                        }
                        goto return_rv;
                }
	}
        *fd = so;
        skygw_log_write_flush(
                LOGFILE_TRACE,
                "%lu [gw_do_connect_to_backend] Connected to backend server "
                "%s:%d, fd %d.",
                pthread_self(),
                host,
                port,
                so);
#if defined(SS_DEBUG)
        conn_open[so] = true;
#endif
return_rv:
	return rv;
}

/**
 * Return a string representation of a MySQL protocol state.
 *
 * @param state The protocol state
 * @return String representation of the state
 *
 */
const char *
gw_mysql_protocol_state2string (int state) {
        switch(state) {
                case MYSQL_ALLOC:
                        return "MySQL Protocl struct allocated";
                case MYSQL_PENDING_CONNECT:
                        return "MySQL Backend socket PENDING connect";
                case MYSQL_CONNECTED:
                        return "MySQL Backend socket CONNECTED";
                case MYSQL_AUTH_SENT:
                        return "MySQL Authentication handshake has been sent";
                case MYSQL_AUTH_RECV:
                        return "MySQL Received user, password, db and capabilities";
                case MYSQL_AUTH_FAILED:
                        return "MySQL Authentication failed";
                case MYSQL_IDLE:
                        return "MySQL Auth done. Protocol is idle, waiting for statements";
                case MYSQL_ROUTING:
                        return "MySQL received command has been routed to backend(s)";
                case MYSQL_WAITING_RESULT:
                        return "MySQL Waiting for result set";
		case MYSQL_SESSION_CHANGE:
			return "MySQL change session";
                default:
                        return "MySQL (unknown protocol state)";
        }
}

/**
 * mysql_send_custom_error
 *
 * Send a MySQL protocol Generic ERR message, to the dcb
 * Note the errno and state are still fixed now
 *
 * @param dcb Owner_Dcb Control Block for the connection to which the OK is sent
 * @param packet_number
 * @param in_affected_rows
 * @param mysql_message
 * @return packet length
 *
 */
int
mysql_send_custom_error (DCB *dcb, int packet_number, int in_affected_rows, const char* mysql_message) {
        uint8_t *outbuf = NULL;
        uint8_t mysql_payload_size = 0;
        uint8_t mysql_packet_header[4];
        uint8_t *mysql_payload = NULL;
        uint8_t field_count = 0;
        uint8_t mysql_err[2];
        uint8_t mysql_statemsg[6];
        unsigned int mysql_errno = 0;
        const char *mysql_error_msg = NULL;
        const char *mysql_state = NULL;

        GWBUF   *buf = NULL;

	ss_dassert(dcb != NULL);

        mysql_errno = 2003;
        mysql_error_msg = "An errorr occurred ...";
        mysql_state = "HY000";

        field_count = 0xff;
        gw_mysql_set_byte2(mysql_err, mysql_errno);
        mysql_statemsg[0]='#';
        memcpy(mysql_statemsg+1, mysql_state, 5);

        if (mysql_message != NULL) {
                mysql_error_msg = mysql_message;
        }

        mysql_payload_size = sizeof(field_count) + sizeof(mysql_err) + sizeof(mysql_statemsg) + strlen(mysql_error_msg);

        // allocate memory for packet header + payload
        buf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
        ss_dassert(buf != NULL);
        
        if (buf == NULL)
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

        // write field
        memcpy(mysql_payload, &field_count, sizeof(field_count));
        mysql_payload = mysql_payload + sizeof(field_count);

        // write errno
        memcpy(mysql_payload, mysql_err, sizeof(mysql_err));
        mysql_payload = mysql_payload + sizeof(mysql_err);

        // write sqlstate
        memcpy(mysql_payload, mysql_statemsg, sizeof(mysql_statemsg));
        mysql_payload = mysql_payload + sizeof(mysql_statemsg);

        // write err messg
        memcpy(mysql_payload, mysql_error_msg, strlen(mysql_error_msg));

        // writing data in the Client buffer queue
        dcb->func.write(dcb, buf);

        return sizeof(mysql_packet_header) + mysql_payload_size;
}

/**
 * Write a MySQL CHANGE_USER packet to backend server
 *
 * @param conn  MySQL protocol structure
 * @param dbname The selected database
 * @param user The selected user
 * @param passwd The SHA1(real_password): Note real_password is unknown
 * @return 1 on success, 0 on failure
 */
int gw_send_change_user_to_backend(char *dbname, char *user, uint8_t *passwd, MySQLProtocol *conn) {
        int compress = 0;
        int rv;
        uint8_t *payload = NULL;
        uint8_t *payload_start = NULL;
        long bytes;
        uint8_t client_scramble[GW_MYSQL_SCRAMBLE_SIZE];
        uint8_t client_capabilities[4];
        uint32_t server_capabilities;
        uint32_t final_capabilities;
        char dbpass[129]="";
	GWBUF *buffer;
	DCB *dcb;

        char *curr_db = NULL;
        uint8_t *curr_passwd = NULL;

        if (strlen(dbname))
                curr_db = dbname;

        if (strlen((char *)passwd))
                curr_passwd = passwd;

	dcb = conn->owner_dcb;

#ifdef DEBUG_MYSQL_CONN
	fprintf(stderr, ">> Sending credentials %s, %s, db %s\n", user, passwd, dbname);
#endif

	// Zero the vars
	memset(&server_capabilities, '\0', sizeof(server_capabilities));
	memset(&final_capabilities, '\0', sizeof(final_capabilities));

        final_capabilities = gw_mysql_get_byte4((uint8_t *)&server_capabilities);

        final_capabilities |= GW_MYSQL_CAPABILITIES_PROTOCOL_41;
        final_capabilities |= GW_MYSQL_CAPABILITIES_CLIENT;

        if (compress) {
                final_capabilities |= GW_MYSQL_CAPABILITIES_COMPRESS;
#ifdef DEBUG_MYSQL_CONN
                fprintf(stderr, ">>>> Backend Connection with compression\n");
#endif
        }

        if (curr_passwd != NULL) {
                uint8_t hash1[GW_MYSQL_SCRAMBLE_SIZE]="";
                uint8_t hash2[GW_MYSQL_SCRAMBLE_SIZE]="";
                uint8_t new_sha[GW_MYSQL_SCRAMBLE_SIZE]="";

		// hash1 is the function input, SHA1(real_password)
                memcpy(hash1, passwd, GW_MYSQL_SCRAMBLE_SIZE);

		// hash2 is the SHA1(input data), where input_data = SHA1(real_password)
                gw_sha1_str(hash1, GW_MYSQL_SCRAMBLE_SIZE, hash2);

		// dbpass is the HEX form of SHA1(SHA1(real_password))
                gw_bin2hex(dbpass, hash2, GW_MYSQL_SCRAMBLE_SIZE);

		// new_sha is the SHA1(CONCAT(scramble, hash2)
                gw_sha1_2_str(conn->scramble, GW_MYSQL_SCRAMBLE_SIZE, hash2, GW_MYSQL_SCRAMBLE_SIZE, new_sha);

		// compute the xor in client_scramble
                gw_str_xor(client_scramble, new_sha, hash1, GW_MYSQL_SCRAMBLE_SIZE);

        }

        if (curr_db == NULL) {
                // without db
                final_capabilities &= ~GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
        } else {
                final_capabilities |= GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB;
        }

        final_capabilities |= GW_MYSQL_CAPABILITIES_PLUGIN_AUTH;

        gw_mysql_set_byte4(client_capabilities, final_capabilities);

	// Protocol MySQL COM_CHANGE_USER for CLIENT_PROTOCOL_41
	// 1 byte COMMAND
        bytes = 1;

	// add the user
        bytes += strlen(user);
        // the NULL
        bytes++;

	// next will be + 1 (scramble_len) + 20 (fixed_scramble) + (dbname + NULL term) + 2 bytes charset 

        if (curr_passwd != NULL) {
                bytes += GW_MYSQL_SCRAMBLE_SIZE;
                bytes++;
	} else {
                bytes++;
	}	

        if (curr_db != NULL) {
                bytes += strlen(curr_db);
        	bytes++;
	}

	// the charset
	bytes += 2;
        bytes += strlen("mysql_native_password");
        bytes++;

        // the packet header
        bytes += 4;

	// allocating the GWBUF
	buffer = gwbuf_alloc(bytes);
	payload = GWBUF_DATA(buffer);

	// clearing data
	memset(payload, '\0', bytes);
	
	// save the start pointer
	payload_start = payload;

	// set packet # = 1
        payload[3] = 0x00;
        payload += 4;

	// set the command COM_CHANGE_USER \x11
	payload[0] = 0x11;
        payload++;

	memcpy(payload, user, strlen(user));
        payload += strlen(user);
        payload++;

        if (curr_passwd != NULL) {
                // set the auth-length
                *payload = GW_MYSQL_SCRAMBLE_SIZE;
                payload++;

                //copy the 20 bytes scramble data after packet_buffer+36+user+NULL+1 (byte of auth-length)
                memcpy(payload, client_scramble, GW_MYSQL_SCRAMBLE_SIZE);

                payload += GW_MYSQL_SCRAMBLE_SIZE;

        } else {
                // skip the auth-length and write a NULL
                payload++;
        }

        // if the db is not NULL append it
        if (curr_db != NULL) {
                memcpy(payload, curr_db, strlen(curr_db));
                payload += strlen(curr_db);
                payload++;
        }

        // set the charset, 2 bytes!!!!
        *payload = '\x08';
        payload++;
        *payload = '\x00';
        payload++;

        memcpy(payload, "mysql_native_password", strlen("mysql_native_password"));

        payload += strlen("mysql_native_password");
        payload++;

	// put here the paylod size: bytes to write - 4 bytes packet header
        gw_mysql_set_byte3(payload_start, (bytes-4));

	rv = dcb->func.write(dcb, buffer);

	if (rv == 0)
		return 0;
	else
		return 1;
}

/**
 * gw_check_mysql_scramble_data
 *
 * Check authentication token received against stage1_hash and scramble
 *
 * @param dcb The current dcb
 * @param token The token sent by the client in the authentication request
 * @param token_len The token size in bytes
 * @param scramble The scramble data sent by the server during handshake
 * @param scramble_len The scrable size in bytes
 * @param username The current username in the authentication request
 * @param stage1_hash The SHA1(candidate_password) decoded by this routine
 * @return 0 on succesful check or != 0 on failure
 *
 */
int gw_check_mysql_scramble_data(DCB *dcb, uint8_t *token, unsigned int token_len, uint8_t *scramble, unsigned int scramble_len, char *username, uint8_t *stage1_hash) {
	uint8_t step1[GW_MYSQL_SCRAMBLE_SIZE]="";
	uint8_t step2[GW_MYSQL_SCRAMBLE_SIZE +1]="";
	uint8_t check_hash[GW_MYSQL_SCRAMBLE_SIZE]="";
	char hex_double_sha1[2 * GW_MYSQL_SCRAMBLE_SIZE + 1]="";
	uint8_t password[GW_MYSQL_SCRAMBLE_SIZE]="";
	int ret_val = 1;

	if ((username == NULL) || (scramble == NULL) || (stage1_hash == NULL)) {
		return 1;
	}

	/**
	 * get the user's password from repository in SHA1(SHA1(real_password));
	 * please note 'real_password' is unknown!
	 */

	ret_val = gw_find_mysql_user_password_sha1(username, password, (DCB *) dcb);

	if (ret_val) {
		fprintf(stderr, "<<<< User [%s] was not found\n", username);
		return 1;
	}

	if (token && token_len) {
		/**
		 * convert in hex format: this is the content of mysql.user table.
		 * The field password is without the '*' prefix and it is 40 bytes long
		 */

		gw_bin2hex(hex_double_sha1, password, SHA_DIGEST_LENGTH);
	} else {
		/* check if the password is not set in the user table */
		if (!strlen((char *)password)) {
			/* Username without password */
			//fprintf(stderr, ">>> continue WITHOUT auth, no password\n");
			return 0;
		} else {
			return 1;
		}
	}

	/**
	 * Auth check in 3 steps
	 *
	 * Note: token = XOR (SHA1(real_password), SHA1(CONCAT(scramble, SHA1(SHA1(real_password)))))
	 * the client sends token
	 *
	 * Now, server side:
	 *
	 *
	 * step 1: compute the STEP1 = SHA1(CONCAT(scramble, gateway_password))
	 * the result in step1 is SHA_DIGEST_LENGTH long
	 */

	gw_sha1_2_str(scramble, scramble_len, password, SHA_DIGEST_LENGTH, step1);

	/**
	 * step2: STEP2 = XOR(token, STEP1)
	 *
	 * token is trasmitted form client and it's based on the handshake scramble and SHA1(real_passowrd)
	 * step1 has been computed in the previous step
	 * the result STEP2 is SHA1(the_password_to_check) and is SHA_DIGEST_LENGTH long
	 */

	gw_str_xor(step2, token, step1, token_len);

	/**
	 * copy the stage1_hash back to the caller
	 * stage1_hash will be used for backend authentication
	 */
	
	memcpy(stage1_hash, step2, SHA_DIGEST_LENGTH);

	/**
	 * step 3: prepare the check_hash
	 *	
	 * compute the SHA1(STEP2) that is SHA1(SHA1(the_password_to_check)), and is SHA_DIGEST_LENGTH long
	 */
	
	gw_sha1_str(step2, SHA_DIGEST_LENGTH, check_hash);


#ifdef GW_DEBUG_CLIENT_AUTH
	{
		char inpass[128]="";
		gw_bin2hex(inpass, check_hash, SHA_DIGEST_LENGTH);
		
		fprintf(stderr, "The CLIENT hex(SHA1(SHA1(password))) for \"%s\" is [%s]", username, inpass);
	}
#endif

	/* now compare SHA1(SHA1(gateway_password)) and check_hash: return 0 is MYSQL_AUTH_OK */
	return memcmp(password, check_hash, SHA_DIGEST_LENGTH);
}

/**
 * gw_find_mysql_user_password_sha1
 *
 * The routine fetches look for an user int he Gateway users' tableg
 * If found the HEX passwotd, representing sha1(sha1(password)), is converted in binary data and
 * copied into gateway_password 
 *
 * @param username The user to look for
 * @param gateway_password The related SHA1(SHA1(password)), the pointer must be preallocated
 * @param repository The pointer to users' table data, passed as void *
 * @return 1 if user is not found or 0 if the user exists
 *
 */

int gw_find_mysql_user_password_sha1(char *username, uint8_t *gateway_password, void *repository) {
        SERVICE *service = NULL;
        char *user_password = NULL;

        if (strcmp(username , "root") == 0) {
                return 1;
        }

        service = (SERVICE *) ((DCB *)repository)->service;

        user_password = (char *)users_fetch(service->users, username);

        if (!user_password) {
                return 1;
        }

        /**
	 * Convert now the hex data (40 bytes) to binary (20 bytes).
         * The gateway_password represents the SHA1(SHA1(real_password)).
         * Please not real_password is unknown and SHA1(real_password) is unknown as well
	 */

        if (strlen(user_password))
                gw_hex2bin(gateway_password, user_password, SHA_DIGEST_LENGTH * 2);
        return 0;
}

/**
 * mysql_send_auth_error
 *
 * Send a MySQL protocol ERR message, for gateway authentication error to the dcb
 *
 * @param dcb descriptor Control Block for the connection to which the OK is sent
 * @param packet_number
 * @param in_affected_rows
 * @param mysql_message
 * @return packet length
 *
 */
int
mysql_send_auth_error (DCB *dcb, int packet_number, int in_affected_rows, const char* mysql_message) {
        uint8_t *outbuf = NULL;
        uint8_t mysql_payload_size = 0;
        uint8_t mysql_packet_header[4];
        uint8_t *mysql_payload = NULL;
        uint8_t field_count = 0;
        uint8_t mysql_err[2];
        uint8_t mysql_statemsg[6];
        unsigned int mysql_errno = 0;
        const char *mysql_error_msg = NULL;
        const char *mysql_state = NULL;

        GWBUF   *buf;

        mysql_errno = 1045;
        mysql_error_msg = "Access denied!";
        mysql_state = "2800";

        field_count = 0xff;
        gw_mysql_set_byte2(mysql_err, mysql_errno);
        mysql_statemsg[0]='#';
        memcpy(mysql_statemsg+1, mysql_state, 5);

        if (mysql_message != NULL) {
                mysql_error_msg = mysql_message;
        }

        mysql_payload_size = sizeof(field_count) + sizeof(mysql_err) + sizeof(mysql_statemsg) + strlen(mysql_error_msg);

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

        // write field
        memcpy(mysql_payload, &field_count, sizeof(field_count));
        mysql_payload = mysql_payload + sizeof(field_count);

        // write errno
        memcpy(mysql_payload, mysql_err, sizeof(mysql_err));
        mysql_payload = mysql_payload + sizeof(mysql_err);

        // write sqlstate
        memcpy(mysql_payload, mysql_statemsg, sizeof(mysql_statemsg));
        mysql_payload = mysql_payload + sizeof(mysql_statemsg);

        // write err messg
        memcpy(mysql_payload, mysql_error_msg, strlen(mysql_error_msg));

        // writing data in the Client buffer queue
        dcb->func.write(dcb, buf);

        return sizeof(mysql_packet_header) + mysql_payload_size;
}
