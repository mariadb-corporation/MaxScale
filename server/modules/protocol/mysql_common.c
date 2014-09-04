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
 * 10/02/2014	Massimiliano Pinto	Added MySQL Authentication with user@host
 *
 */

#include "mysql_client_server_protocol.h"
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

extern int lm_enabled_logfiles_bitmask;

extern int gw_read_backend_event(DCB* dcb);
extern int gw_write_backend_event(DCB *dcb);
extern int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
extern int gw_error_backend_event(DCB *dcb);

static server_command_t* server_command_init(server_command_t* srvcmd,
                                             mysql_server_cmd_t cmd);


/** 
 * Creates MySQL protocol structure 
 *
 * @param dcb *          Must be non-NULL.
 * @param fd	
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
            LOGIF(LE, (skygw_log_write_flush(
                    LOGFILE_ERROR,
                    "%lu [mysql_init_protocol] MySQL protocol init failed : "
                    "memory allocation due error  %d, %s.",
                    pthread_self(),
                    eno,
                    strerror(eno))));
            goto return_p;
        }
        p->protocol_state = MYSQL_PROTOCOL_ALLOC;
	p->protocol_auth_state = MYSQL_ALLOC;
        p->protocol_command.scom_cmd = MYSQL_COM_UNDEFINED;
        p->protocol_command.scom_nresponse_packets = 0;
        p->protocol_command.scom_nbytes_to_read = 0;
#if defined(SS_DEBUG)
        p->protocol_chk_top = CHK_NUM_PROTOCOL;
        p->protocol_chk_tail = CHK_NUM_PROTOCOL;
#endif
        /*< Assign fd with protocol */
        p->fd = fd;
	p->owner_dcb = dcb;
        p->protocol_state = MYSQL_PROTOCOL_ACTIVE;
        CHK_PROTOCOL(p);
return_p:
        return p;
}


/**
 * mysql_protocol_done
 * 
 * free protocol allocations.
 * 
 * @param dcb owner DCB
 * 
 */
void mysql_protocol_done (
        DCB* dcb)
{
        MySQLProtocol* p;
        server_command_t* scmd;
        server_command_t* scmd2;
        
        p = (MySQLProtocol *)dcb->protocol;
        
        spinlock_acquire(&p->protocol_lock);

        if (p->protocol_state != MYSQL_PROTOCOL_ACTIVE)
        {
                goto retblock;
        }
        scmd = p->protocol_cmd_history;
        
        while (scmd != NULL)
        {
                scmd2 = scmd->scom_next;
                free(scmd);
                scmd = scmd2;
        }
        p->protocol_state = MYSQL_PROTOCOL_DONE;
        
retblock:
        spinlock_release(&p->protocol_lock);
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


	if (conn->fd > 0) {
		/* COM_QUIT will not be sent here, but from the caller of this routine! */
		close(conn->fd);
	} else {
		// no socket here
	}

	free(*ptr);

	*ptr = NULL;
}

/**
 * Read the backend server MySQL handshake  
 *
 * @param conn	MySQL protocol structure
 * @return 0 on success, 1 on failure
 */
int gw_read_backend_handshake(
        MySQLProtocol *conn) 
{
	GWBUF *head = NULL;
	DCB *dcb = conn->owner_dcb;
	int n = -1;
	uint8_t *payload = NULL;
	int h_len = 0;
	int  success = 0;
	int packet_len = 0;

	if ((n = dcb_read(dcb, &head)) != -1) 
        {
		if (head) 
                {
			payload = GWBUF_DATA(head);
			h_len = gwbuf_length(head);
                        
   			/**
			 * The mysql packets content starts at byte fifth
			 * just return with less bytes
			 */

			if (h_len <= 4) {
				/* log error this exit point */
				conn->protocol_auth_state = MYSQL_AUTH_FAILED;
                                LOGIF(LD, (skygw_log_write(
                                        LOGFILE_DEBUG,
                                        "%lu [gw_read_backend_handshake] after "
                                        "dcb_read, fd %d, "
                                        "state = MYSQL_AUTH_FAILED.",
                                        dcb->fd,
                                        pthread_self())));
                                
				return 1;
			}

			if (payload[4] == 0xff) 
                        {
                                size_t   len = MYSQL_GET_PACKET_LEN(payload);
                                uint16_t errcode = MYSQL_GET_ERRCODE(payload);
                                char*    bufstr = strndup(&((char *)payload)[7], len-3);
                                
                                LOGIF(LD, (skygw_log_write(
                                        LOGFILE_DEBUG,
                                        "%lu [gw_receive_backend_auth] Invalid "
                                        "authentication message from backend dcb %p "
                                        "fd %d, ptr[4] = %p, error code %d, msg %s.",
                                        pthread_self(),
                                        dcb,
                                        dcb->fd,
                                        payload[4],
                                        errcode,
                                        bufstr)));
                                
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Invalid authentication message "
                                        "from backend. Error code: %d, Msg : %s",
                                        errcode,
                                        bufstr)));
                                
                                free(bufstr);
                        }
                        //get mysql packet size, 3 bytes
			packet_len = gw_mysql_get_byte3(payload);

			if (h_len < (packet_len + 4)) {
				/*
				 * data in buffer less than expected in the
                                 * packet. Log error this exit point
				 */
				conn->protocol_auth_state = MYSQL_AUTH_FAILED;
                                LOGIF(LD, (skygw_log_write(
                                        LOGFILE_DEBUG,
                                        "%lu [gw_read_backend_handshake] after "
                                        "gw_mysql_get_byte3, fd %d, "
                                        "state = MYSQL_AUTH_FAILED.",
                                        pthread_self(),
                                        dcb->fd,
                                        pthread_self())));
                                
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
				conn->protocol_auth_state = MYSQL_AUTH_FAILED;
                                LOGIF(LD, (skygw_log_write(
                                        LOGFILE_DEBUG,
                                        "%lu [gw_read_backend_handshake] after "
                                        "gw_decode_mysql_server_handshake, fd %d, "
                                        "state = MYSQL_AUTH_FAILED.",
                                        pthread_self(),
                                        conn->owner_dcb->fd,
                                        pthread_self())));
                                
				return 1;
			}

			conn->protocol_auth_state = MYSQL_AUTH_SENT;

			// consume all the data here
			head = gwbuf_consume(head, GWBUF_LENGTH(head));

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
int gw_decode_mysql_server_handshake(
        MySQLProtocol *conn, 
        uint8_t       *payload) 
{
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

	if (protocol_version != GW_MYSQL_PROTOCOL_VERSION) 
        {
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
	if (payload[0] > 0) 
        {
		scramble_len = payload[0] -1;
		ss_dassert(scramble_len > GW_SCRAMBLE_LENGTH_323);
		ss_dassert(scramble_len <= GW_MYSQL_SCRAMBLE_SIZE);

		if ((scramble_len < GW_SCRAMBLE_LENGTH_323) || 
                        scramble_len > GW_MYSQL_SCRAMBLE_SIZE) 
                {
			/* log this */
                        return -2;
		}
	} 
	else 
        {
		scramble_len = GW_MYSQL_SCRAMBLE_SIZE;
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
 * @param protocol The MySQL protocol structure
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

        /*<
         * Read didn't fail and there is enough data for mysql packet.
         */
        if (n != -1 &&
            head != NULL &&
            GWBUF_LENGTH(head) >= 5)
        {
                ptr = GWBUF_DATA(head);
                /*<
                 * 5th byte is 0x0 if successful.
                 */
                if (ptr[4] == 0x00) 
                {
                        rc = 1;
                } 
                else if (ptr[4] == 0xff) 
                {
                        size_t   len = MYSQL_GET_PACKET_LEN(ptr);
                        char*    err = strndup(&((char *)ptr)[8], 5);
                        char*    bufstr = strndup(&((char *)ptr)[13], len-4-5);
                                        
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [gw_receive_backend_auth] Invalid "
                                "authentication message from backend dcb %p "
                                "fd %d, ptr[4] = %p, error %s, msg %s.",
                                pthread_self(),
                                dcb,
                                dcb->fd,
                                ptr[4],
                                err,
                                bufstr)));
                        
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Invalid authentication message "
                                "from backend. Error : %s, Msg : %s",
                                err,
                                bufstr)));

                        free(bufstr);
                        rc = -1;
                }
                else
                {
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [gw_receive_backend_auth] Invalid "
                                "authentication message from backend dcb %p "
                                "fd %d, ptr[4] = %p",
                                pthread_self(),
                                dcb,
                                dcb->fd,
                                ptr[4])));
                        
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Invalid authentication message "
                                "from backend. Packet type : %p",
                                ptr[4])));
                }
                /*<
                 * Remove data from buffer.
                 */
                while ((head = gwbuf_consume(head, GWBUF_LENGTH(head))) != NULL);
        }
        else if (n == 0)
        {
                /*<
                 * This is considered as success because call didn't fail,
                 * although no bytes was read.
                 */
                rc = 0;
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [gw_receive_backend_auth] Read zero bytes from "
                        "backend dcb %p fd %d in state %s. n %d, head %p, len %d",
                        pthread_self(),
                        dcb,
                        dcb->fd,
                        STRDCBSTATE(dcb->state),
                        n,
                        head,
                        (head == NULL) ? 0 : GWBUF_LENGTH(head))));
        }
        else
        {
                ss_dassert(n < 0 && head == NULL);
                rc = -1;
                LOGIF(LD, (skygw_log_write_flush(
                        LOGFILE_DEBUG,
                        "%lu [gw_receive_backend_auth] Reading from backend dcb %p "
                        "fd %d in state %s failed. n %d, head %p, len %d",
                        pthread_self(),
                        dcb,
                        dcb->fd,
                        STRDCBSTATE(dcb->state),
                        n,
                        head,
                        (head == NULL) ? 0 : GWBUF_LENGTH(head))));
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
        char dbpass[MYSQL_USER_MAXLEN + 1]="";
	GWBUF *buffer;
	DCB *dcb;

        char *curr_db = NULL;
        uint8_t *curr_passwd = NULL;

        if (strlen(dbname))
                curr_db = dbname;

        if (strlen((char *)passwd))
                curr_passwd = passwd;

	dcb = conn->owner_dcb;

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
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error: Establishing connection to backend server "
                        "%s:%d failed.\n\t\t             Socket creation failed "
                        "due %d, %s.",
                        host,
                        port,
                        eno,
                        strerror(eno))));
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
                        
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error:  Failed to connect backend server %s:%d, "
                                "due %d, %s.",
                                host,
                                port,
                                eno,
                                strerror(eno))));
                        /*< Close newly created socket. */
                        rc = close(so);

                        if (rc != 0) {
                                int eno = errno;
                                errno = 0;
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error: Failed to "
                                        "close socket %d due %d, %s.",
                                        oldfd,
                                        eno,
                                        strerror(eno))));
                        }
                        goto return_rv;
                }
	}
        *fd = so;
        LOGIF(LD, (skygw_log_write_flush(
                LOGFILE_DEBUG,
                "%lu [gw_do_connect_to_backend] Connected to backend server "
                "%s:%d, fd %d.",
                pthread_self(),
                host,
                port,
                so)));
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
                        return "MySQL authentication is succesfully done.";
                default:
                        return "MySQL (unknown protocol state)";
        }
}

GWBUF* mysql_create_com_quit(
        GWBUF* bufparam,
        int    packet_number)
{
        uint8_t* data;
        GWBUF*   buf;
        
        if (bufparam == NULL)
        {
                buf = gwbuf_alloc(COM_QUIT_PACKET_SIZE);
        }
        else
        {
                buf = bufparam;
        }
        
        if (buf == NULL)
        {
                return 0;
        }
        ss_dassert(GWBUF_LENGTH(buf) == COM_QUIT_PACKET_SIZE);
        
        data = GWBUF_DATA(buf);
        
        *data++ = 0x1;
        *data++ = 0x0;
        *data++ = 0x0;
        *data++ = packet_number;
        *data   = 0x1;
        
        return buf;
}

int mysql_send_com_quit(
        DCB*   dcb,
        int    packet_number,
        GWBUF* bufparam)
{
        GWBUF   *buf;
        int     nbytes = 0;

        CHK_DCB(dcb);
        ss_dassert(packet_number <= 255);
        
        if (dcb == NULL || dcb->state == DCB_STATE_ZOMBIE)
        {
                return 0;
        }
        if (bufparam == NULL)
        {
                buf = mysql_create_com_quit(NULL, packet_number);
        }
        else
        {
                buf = bufparam;
        }
        
        if (buf == NULL)
        {
                return 0;
        }
        nbytes = dcb->func.write(dcb, buf);
        
        return nbytes;
}


GWBUF* mysql_create_custom_error(
        int         packet_number,
        int         affected_rows,
        const char* msg)
{
        uint8_t*     outbuf = NULL;
        uint8_t      mysql_payload_size = 0;
        uint8_t      mysql_packet_header[4];
        uint8_t*     mysql_payload = NULL;
        uint8_t      field_count = 0;
        uint8_t      mysql_err[2];
        uint8_t      mysql_statemsg[6];
        unsigned int mysql_errno = 0;
        const char*  mysql_error_msg = NULL;
        const char*  mysql_state = NULL;
        
        GWBUF* errbuf = NULL;
        
        mysql_errno = 2003;
        mysql_error_msg = "An errorr occurred ...";
        mysql_state = "HY000";
        
        field_count = 0xff;
        gw_mysql_set_byte2(mysql_err, mysql_errno);
        mysql_statemsg[0]='#';
        memcpy(mysql_statemsg+1, mysql_state, 5);
        
        if (msg != NULL) {
                mysql_error_msg = msg;
        }
        
        mysql_payload_size = sizeof(field_count) + 
                                sizeof(mysql_err) + 
                                sizeof(mysql_statemsg) + 
                                strlen(mysql_error_msg);
        
        /** allocate memory for packet header + payload */
        errbuf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
        ss_dassert(errbuf != NULL);
        
        if (errbuf == NULL)
        {
                return 0;
        }
        outbuf = GWBUF_DATA(errbuf);
        
        /** write packet header and packet number */
        gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
        mysql_packet_header[3] = packet_number;
        
        /** write header */
        memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));
        
        mysql_payload = outbuf + sizeof(mysql_packet_header);
        
        /** write field */
        memcpy(mysql_payload, &field_count, sizeof(field_count));
        mysql_payload = mysql_payload + sizeof(field_count);
        
        /** write errno */
        memcpy(mysql_payload, mysql_err, sizeof(mysql_err));
        mysql_payload = mysql_payload + sizeof(mysql_err);
        
        /** write sqlstate */
        memcpy(mysql_payload, mysql_statemsg, sizeof(mysql_statemsg));
        mysql_payload = mysql_payload + sizeof(mysql_statemsg);
        
        /** write error message */
        memcpy(mysql_payload, mysql_error_msg, strlen(mysql_error_msg));

        return errbuf;
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
int mysql_send_custom_error (
        DCB       *dcb, 
        int        packet_number, 
        int        in_affected_rows, 
        const char *mysql_message) 
{
        GWBUF* buf;

        buf = mysql_create_custom_error(packet_number, in_affected_rows, mysql_message);
        
        dcb->func.write(dcb, buf);

        return GWBUF_LENGTH(buf);
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
        char dbpass[MYSQL_USER_MAXLEN + 1]="";
	GWBUF *buffer;
	DCB *dcb;

        char *curr_db = NULL;
        uint8_t *curr_passwd = NULL;

        if (strlen(dbname))
                curr_db = dbname;

        if (strlen((char *)passwd))
                curr_passwd = passwd;

	dcb = conn->owner_dcb;

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
 * @param token 	The token sent by the client in the authentication request
 * @param token_len 	The token size in bytes
 * @param scramble 	The scramble data sent by the server during handshake
 * @param scramble_len 	The scrable size in bytes
 * @param username	The current username in the authentication request
 * @param stage1_hash	The SHA1(candidate_password) decoded by this routine
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

	/*<
	 * get the user's password from repository in SHA1(SHA1(real_password));
	 * please note 'real_password' is unknown!
	 */

	ret_val = gw_find_mysql_user_password_sha1(username, password, dcb);

	if (ret_val) {
		return 1;
	}

	if (token && token_len) {
		/*<
		 * convert in hex format: this is the content of mysql.user table.
		 * The field password is without the '*' prefix and it is 40 bytes long
		 */

		gw_bin2hex(hex_double_sha1, password, SHA_DIGEST_LENGTH);
	} else {
		/* check if the password is not set in the user table */
		if (!strlen((char *)password)) {
			/* Username without password */
			return 0;
		} else {
			return 1;
		}
	}

	/*<
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

	/*<
	 * step2: STEP2 = XOR(token, STEP1)
	 *
	 * token is trasmitted form client and it's based on the handshake scramble and SHA1(real_passowrd)
	 * step1 has been computed in the previous step
	 * the result STEP2 is SHA1(the_password_to_check) and is SHA_DIGEST_LENGTH long
	 */

	gw_str_xor(step2, token, step1, token_len);

	/*<
	 * copy the stage1_hash back to the caller
	 * stage1_hash will be used for backend authentication
	 */
	
	memcpy(stage1_hash, step2, SHA_DIGEST_LENGTH);

	/*<
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
 * The routine fetches look for an user int he Gateway users' table
 * The users' table is dcb->service->users or a different one specified with void *repository
 *
 * If found the HEX password, representing sha1(sha1(password)), is converted in binary data and
 * copied into gateway_password 
 *
 * @param username 		The user to look for
 * @param gateway_password	The related SHA1(SHA1(password)), the pointer must be preallocated
 * @param dcb			Current DCB
 * @return 1 if user is not found or 0 if the user exists
 *
 */

int gw_find_mysql_user_password_sha1(char *username, uint8_t *gateway_password, DCB *dcb) {
        SERVICE *service = NULL;
	struct sockaddr_in *client;
        char *user_password = NULL;
	MYSQL_USER_HOST key;

	service = (SERVICE *) dcb->service;
	client = (struct sockaddr_in *) &dcb->ipv4;

	key.user = username;
	memcpy(&key.ipv4, client, sizeof(struct sockaddr_in));

	LOGIF(LD,
		(skygw_log_write_flush(
			LOGFILE_DEBUG,
			"%lu [MySQL Client Auth], checking user [%s@%s]",
			pthread_self(),
			key.user,
			dcb->remote)));

	/* look for user@current_host now */
        user_password = mysql_users_fetch(service->users, &key);

        if (!user_password) {
		/* The user is not authenticated @ current host */

		/* 1) Check for localhost first.
		 * The check for localhost is 127.0.0.1 (IPv4 only)
 		 */

		if (key.ipv4.sin_addr.s_addr == 0x0100007F) {
 		 	/* Skip the wildcard check and return 1 */
			LOGIF(LD,
				(skygw_log_write_flush(
					LOGFILE_DEBUG,
					"%lu [MySQL Client Auth], user [%s@%s] not existent",
					pthread_self(),
					key.user,
					dcb->remote)));

			return 1;
		}
	
		/* 2) Continue and check for wildcard host, user@%
		 * Return 1 if no match
		 */

		memset(&key.ipv4, 0, sizeof(struct sockaddr_in));

		LOGIF(LD,
			(skygw_log_write_flush(
				LOGFILE_DEBUG,
				"%lu [MySQL Client Auth], checking user [%s@%s] with wildcard host [%%]",
				pthread_self(),
				key.user,
				dcb->remote)));

		user_password = mysql_users_fetch(service->users, &key);
     
		if (!user_password) {
			/* the user@% was not found.
 			 * Return 1
 			 */
			LOGIF(LD,
				(skygw_log_write_flush(
					LOGFILE_DEBUG,
					"%lu [MySQL Client Auth], user [%s@%s] not existent",
					pthread_self(),
					key.user,
					dcb->remote)));
			return 1;
		}
	}

	/* user@host found: now check the password
 	 *
	 * Convert the hex data (40 bytes) to binary (20 bytes).
         * The gateway_password represents the SHA1(SHA1(real_password)).
         * Please note: the real_password is unknown and SHA1(real_password) is unknown as well
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
mysql_send_auth_error (
        DCB         *dcb, 
        int         packet_number, 
        int         in_affected_rows,
        const char  *mysql_message) 
{
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

        if (dcb->state != DCB_STATE_POLLING)
        {
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [mysql_send_auth_error] dcb %p is in a state %s, "
                        "and it is not in epoll set anymore. Skip error sending.",
                        pthread_self(),
                        dcb,
                        STRDCBSTATE(dcb->state))));
                return 0;
        }
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


/**
 * Buffer contains at least one of the following:
 * complete [complete] [partial] mysql packet
 * 
 * return pointer to gwbuf containing a complete packet or
 *   NULL if no complete packet was found.
 */
GWBUF* gw_MySQL_get_next_packet(
        GWBUF** p_readbuf)
{
        GWBUF*   packetbuf;
        GWBUF*   readbuf;
        size_t   buflen;
        size_t   packetlen;
        size_t   totalbuflen;
        uint8_t* data;
        size_t   nbytes_copied = 0;
        uint8_t* target;
        
        readbuf = *p_readbuf;

        if (readbuf == NULL)
        {
                packetbuf = NULL;
                goto return_packetbuf;
        }                
        CHK_GWBUF(readbuf);
        
        if (GWBUF_EMPTY(readbuf))
        {
                packetbuf = NULL;
                goto return_packetbuf;
        }
        
        buflen      = GWBUF_LENGTH((readbuf));
        totalbuflen = gwbuf_length(readbuf);
        data        = (uint8_t *)GWBUF_DATA((readbuf));
        packetlen   = MYSQL_GET_PACKET_LEN(data)+4;

        /** packet is incomplete */
        if (packetlen > totalbuflen)
        {
                packetbuf = NULL;
                goto return_packetbuf;
        }
        
        packetbuf = gwbuf_alloc(packetlen);
        target    = GWBUF_DATA(packetbuf);
        packetbuf->gwbuf_type = readbuf->gwbuf_type; /*< Copy the type too */
        /**
         * Copy first MySQL packet to packetbuf and leave posible other
         * packets to read buffer.
         */
        while (nbytes_copied < packetlen && totalbuflen > 0)
        {
                uint8_t* src = GWBUF_DATA((*p_readbuf));
                size_t   bytestocopy;
                
                bytestocopy = MIN(buflen,packetlen-nbytes_copied);
                
                memcpy(target+nbytes_copied, src, bytestocopy);
                *p_readbuf = gwbuf_consume((*p_readbuf), bytestocopy);
                totalbuflen = gwbuf_length((*p_readbuf));
                nbytes_copied += bytestocopy;
        }
        ss_dassert(buflen == 0 || nbytes_copied == packetlen);
        
return_packetbuf:
        return packetbuf;
}


/**
 * Move <npackets> from buffer pointed to by <*p_readbuf>.
 */
GWBUF* gw_MySQL_get_packets(
        GWBUF** p_srcbuf,
        int*    npackets)
{
        GWBUF* packetbuf;
        GWBUF* targetbuf = NULL;
        
        while (*npackets > 0 && (packetbuf = gw_MySQL_get_next_packet(p_srcbuf)) != NULL)
        {
                targetbuf = gwbuf_append(targetbuf, packetbuf);
                *npackets -= 1;
        }
        ss_dassert(*npackets < 128);
        ss_dassert(*npackets >= 0);
        
        return targetbuf;
}


static server_command_t* server_command_init(
        server_command_t* srvcmd,
        mysql_server_cmd_t cmd)
{
        server_command_t* c;
        
        if (srvcmd != NULL)
        {
                c = srvcmd;
        }
        else
        {
                c = (server_command_t *)malloc(sizeof(server_command_t));
        }
        c->scom_cmd = cmd;
        c->scom_nresponse_packets = -1;
        c->scom_nbytes_to_read = 0;
        c->scom_next = NULL;
        
        return c;
}

static server_command_t* server_command_copy(
        server_command_t* srvcmd)
{
        server_command_t* c = 
                (server_command_t *)malloc(sizeof(server_command_t));
        *c = *srvcmd;
        
        return c;
}

#define MAX_CMD_HISTORY 10

void protocol_archive_srv_command(
        MySQLProtocol* p)
{
        server_command_t*  s1;
        server_command_t*  h1;
        int                len = 0;
        
        spinlock_acquire(&p->protocol_lock);
        
        if (p->protocol_state != MYSQL_PROTOCOL_ACTIVE)
        {
                goto retblock;
        }
        
        s1 = &p->protocol_command;
        
        LOGIF(LT, (skygw_log_write(
                LOGFILE_TRACE,
                "Move command %s from fd %d to command history.",
                STRPACKETTYPE(s1->scom_cmd), 
                p->owner_dcb->fd)));
        
        /** Copy to history list */
        if ((h1 = p->protocol_cmd_history) == NULL)
        {
                p->protocol_cmd_history = server_command_copy(s1);
        }
        else
        {
                while (h1->scom_next != NULL)
                {
                        h1 = h1->scom_next;
                }
                h1->scom_next = server_command_copy(s1);
        }       

        /** Keep history limits, remove oldest */
        if (len > MAX_CMD_HISTORY)
        {
                server_command_t* c = p->protocol_cmd_history;
                p->protocol_cmd_history = p->protocol_cmd_history->scom_next;
                free(c);
        }
        
        /** Remove from command list */
        if (s1->scom_next == NULL)
        {
                p->protocol_command.scom_cmd = MYSQL_COM_UNDEFINED;
        }
        else
        {
                p->protocol_command = *(s1->scom_next);
                free(s1->scom_next);
        }
        
retblock:
        spinlock_release(&p->protocol_lock);
}


/**
 * If router expects to get separate, complete statements, add MySQL command 
 * to MySQLProtocol structure. It is removed when response has arrived.
 */
void protocol_add_srv_command(
        MySQLProtocol*     p,
        mysql_server_cmd_t cmd)
{
        server_command_t* c;
        
        spinlock_acquire(&p->protocol_lock);

        if (p->protocol_state != MYSQL_PROTOCOL_ACTIVE)
        {
                goto retblock;
        }
        /** this is the only server command in protocol */
        if (p->protocol_command.scom_cmd == MYSQL_COM_UNDEFINED)
        {
                /** write into structure */
                server_command_init(&p->protocol_command, cmd);
        }
        else
        {
                /** add to the end of list */
                p->protocol_command.scom_next = server_command_init(NULL, cmd);
        }
        
        LOGIF(LT, (skygw_log_write(
                LOGFILE_TRACE,
                "Added command %s to fd %d.",
                STRPACKETTYPE(cmd),
                p->owner_dcb->fd)));
        
#if defined(SS_DEBUG)
        c = &p->protocol_command;

        while (c != NULL && c->scom_cmd != MYSQL_COM_UNDEFINED)
        {
                LOGIF(LT, (skygw_log_write(
                        LOGFILE_TRACE,
                        "fd %d : %d %s",
                        p->owner_dcb->fd,
                        c->scom_cmd,
                        STRPACKETTYPE(c->scom_cmd))));
                c = c->scom_next;
        }
#endif
retblock:
        spinlock_release(&p->protocol_lock);
}

    
/** 
 * If router processes separate statements, every stmt has corresponding MySQL
 * command stored in MySQLProtocol structure. 
 * 
 * Remove current (=oldest) command.
 */
void protocol_remove_srv_command(
        MySQLProtocol* p)
{
        server_command_t* s;
        spinlock_acquire(&p->protocol_lock);
        s = &p->protocol_command;
        
        LOGIF(LT, (skygw_log_write(
                LOGFILE_TRACE,
                "Removed command %s from fd %d.",
                STRPACKETTYPE(s->scom_cmd),
                p->owner_dcb->fd)));
        
        if (s->scom_next == NULL)
        {
                p->protocol_command.scom_cmd = MYSQL_COM_UNDEFINED;
        }
        else
        {
                p->protocol_command = *(s->scom_next);
                free(s->scom_next);
        }
                
        spinlock_release(&p->protocol_lock);
}

mysql_server_cmd_t protocol_get_srv_command(
        MySQLProtocol* p,
        bool           removep)
{
        mysql_server_cmd_t      cmd;
        
        cmd = p->protocol_command.scom_cmd;

        if (removep)
        {
                protocol_remove_srv_command(p);
        }
        LOGIF(LD, (skygw_log_write(
                LOGFILE_DEBUG,
                "%lu [protocol_get_srv_command] Read command %s for fd %d.",
                pthread_self(),
                STRPACKETTYPE(cmd),
                p->owner_dcb->fd)));
        return cmd;
}


/** 
 * Examine command type and the readbuf. Conclude response 
 * packet count from the command type or from the first packet 
 * content. 
 * Fails if read buffer doesn't include enough data to read the 
 * packet length.
 */
void init_response_status (
        GWBUF*             buf,
        mysql_server_cmd_t cmd,
        int*               npackets,
        size_t*            nbytes_left)
{
        uint8_t* packet;
        int      nparam;
        int      nattr;
        uint8_t* data;
        
        ss_dassert(gwbuf_length(buf) >= 3);

        data = (uint8_t *)buf->start;
        
        if (data[4] == 0xff) /*< error */
        {
                *npackets = 1;
        }
        else 
        {
                switch (cmd) {
                        case MYSQL_COM_STMT_PREPARE:
                                packet = (uint8_t *)GWBUF_DATA(buf);
                                /** ok + nparam + eof + nattr + eof */
                                nparam = MYSQL_GET_STMTOK_NPARAM(packet);
                                nattr  = MYSQL_GET_STMTOK_NATTR(packet);
                                
                                *npackets = 1 + nparam + MIN(1, nparam) +
                                nattr + MIN(nattr, 1);
                                break;

                        case MYSQL_COM_QUIT:
                        case MYSQL_COM_STMT_SEND_LONG_DATA:
                        case MYSQL_COM_STMT_CLOSE:
                                *npackets = 0; /*< these don't reply anything */
                                break;

                        default:
                                /** 
                                 * assume that other session commands respond 
                                 * OK or ERR 
                                 */
                                *npackets = 1; 
                                break;
                }
        }
        *nbytes_left = MYSQL_GET_PACKET_LEN(data) + MYSQL_HEADER_LEN;
        /** 
         * There is at least one complete packet in the buffer so buffer is bigger 
         * than packet 
         */
        ss_dassert(*nbytes_left > 0);
        ss_dassert(*npackets > 0);
        ss_dassert(*npackets<128);
}



/**
 * Read how many packets are left from current response and how many bytes there
 * is still to be read from the current packet. 
 */
bool protocol_get_response_status (
        MySQLProtocol* p,
        int*           npackets,
        size_t*        nbytes)
{
        bool succp;
        
        CHK_PROTOCOL(p);
        
        spinlock_acquire(&p->protocol_lock);
        *npackets = p->protocol_command.scom_nresponse_packets;
        *nbytes   = p->protocol_command.scom_nbytes_to_read;
        spinlock_release(&p->protocol_lock);
        
        if (*npackets < 0 && *nbytes == 0)
        {
                succp = false;
        }
        else
        {
                succp = true;
        }
        
        return succp;
}

void protocol_set_response_status (
        MySQLProtocol* p,
        int            npackets_left,
        size_t         nbytes)
{
        
        CHK_PROTOCOL(p);

        spinlock_acquire(&p->protocol_lock);
        
        p->protocol_command.scom_nbytes_to_read = nbytes;
        ss_dassert(p->protocol_command.scom_nbytes_to_read >= 0);

        p->protocol_command.scom_nresponse_packets = npackets_left;
        
        spinlock_release(&p->protocol_lock);
}

