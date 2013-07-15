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
 */

#include "mysql_client_server_protocol.h"

extern int gw_read_backend_event(DCB* dcb);
extern int gw_write_backend_event(DCB *dcb);
extern int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
extern int gw_error_backend_event(DCB *dcb);

///////////////////////////////
// Initialize mysql protocol struct
///////////////////////////////////////
MySQLProtocol *gw_mysql_init(MySQLProtocol *data) {
        MySQLProtocol *input = NULL;

        if (input == NULL) {
                // structure allocation
                input = calloc(1, sizeof(MySQLProtocol));

                if (input == NULL)
                        return NULL;

        }

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "gw_mysql_init() called\n");
#endif

        return input;
}


//////////////////////////////////////
// close a connection if opened
// free data scructure for MySQLProtocol
//////////////////////////////////////
void gw_mysql_close(MySQLProtocol **ptr) {
	MySQLProtocol *conn = *ptr;

	if (*ptr == NULL)
		return;

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Closing MySQL connection %i, [%s]\n", conn->fd, conn->scramble);
#endif

	if (conn->fd > 0) {
		//COM_QUIT will not be sent here, but from the caller of this routine!
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
	DCB *dcb = conn->descriptor;
	int n = -1;
	uint8_t *payload = NULL;
	unsigned int packet_len = 0;

	if ((n = dcb_read(dcb, &head)) != -1) {
		dcb->state = DCB_STATE_PROCESSING;

		if (head) {
			payload = GWBUF_DATA(head);
			packet_len = gw_mysql_get_byte3(payload);

			// skip the 4 bytes header
			payload += 4;

			//Now decode mysql handshake
			gw_decode_mysql_server_handshake(conn, payload);

			conn->state = MYSQL_AUTH_SENT;

			// consume all the data here
			head = gwbuf_consume(head, gwbuf_length(head));

			dcb->state = DCB_STATE_POLLING;

			return 0;
		}
	}

	return 1;
}

/**
 * Decode mysql server handshake
 */ 
int gw_decode_mysql_server_handshake(MySQLProtocol *conn, uint8_t *payload) {
	int server_protocol;
	uint8_t *server_version_end = NULL;
	uint16_t mysql_server_capabilities_one;
	uint16_t mysql_server_capabilities_two;
	unsigned long tid =0;
	uint8_t scramble_data_1[8] = "";
	uint8_t scramble_data_2[12] = "";
	uint8_t capab_ptr[4];
	int scramble_len;
	uint8_t scramble[GW_MYSQL_SCRAMBLE_SIZE];

	// Get server protocol
	server_protocol= payload[0];

	payload++;

	// Get server version (string)
	server_version_end = (uint8_t *) gw_strend((char*) payload);
	payload = server_version_end + 1;

	// get ThreadID
	tid = gw_mysql_get_byte4(payload);
	memcpy(&conn->tid, &tid, 4);

	payload +=4;

	// scramble_part 1
	memcpy(scramble_data_1, payload, 8);
	payload += 8;

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

	// skip 10 zero bytes
	payload += 11;

	// copy the second part of the scramble
	memcpy(scramble_data_2, payload, scramble_len - 8);

	memcpy(scramble, scramble_data_1, 8);
	memcpy(scramble + 8, scramble_data_2, scramble_len - 8);

	// full 20 bytes scramble is ready
	memcpy(conn->scramble, scramble, GW_MYSQL_SCRAMBLE_SIZE);

	return 0;
}

/**
 * Receive the MySQL authentication packet from backend, packet # is 2
 *
 * @param conn The MySQL protocol structure
 * @return 0 for user authenticated or 1 for authentication failed
 */
int gw_receive_backend_auth(MySQLProtocol *conn) {
	int rv = 1;
	int n = -1;
	GWBUF *head = NULL;
	DCB *dcb = conn->descriptor;
	uint8_t *ptr = NULL;
	unsigned int packet_len = 0;

	if ((n = dcb_read(dcb, &head)) != -1) {
		dcb->state = DCB_STATE_PROCESSING;
		if (head) {
			ptr = GWBUF_DATA(head);
			packet_len = gw_mysql_get_byte3(ptr);

			// check if the auth is SUCCESFUL
			if (ptr[4] == '\x00') {
				// Auth is OK 
				rv = 0;
			} else {
				rv = 1;
			}

			// consume all the data here
			head = gwbuf_consume(head, gwbuf_length(head));
		}
	}

	dcb->state = DCB_STATE_POLLING;

	return rv;
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
int gw_send_authentication_to_backend(char *dbname, char *user, uint8_t *passwd, MySQLProtocol *conn) {
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

	dcb = conn->descriptor;

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

        memcpy(payload, "mysql_native_password", strlen("mysql_native_password"));

        payload += strlen("mysql_native_password");
        payload++;

	// put here the paylod size: bytes to write - 4 bytes packet header
        gw_mysql_set_byte3(payload_start, (bytes-4));

	// write to backend dcb 
	// ToDO: handle the EAGAIN | EWOULDBLOCK
	rv = write(dcb->fd, GWBUF_DATA(buffer), bytes);
	gwbuf_consume(buffer, bytes);

	conn->state = MYSQL_AUTH_RECV;

	if (rv < 0)
		return rv;
	else
		return 0;
}

/**
 * Only backend connect syscall
 */
int gw_do_connect_to_backend(char *host, int port, MySQLProtocol *conn) {
	struct sockaddr_in serv_addr;
	int rv;
	int so = 0;

	memset(&serv_addr, 0, sizeof serv_addr);
	serv_addr.sin_family = AF_INET;

	so = socket(AF_INET,SOCK_STREAM,0);

	conn->fd = so;

	if (so < 0) {
		fprintf(stderr, "Error creating backend socket: [%s] %i\n", strerror(errno), errno);
		/* this is an error */
		return -1;
	}

	setipaddress(&serv_addr.sin_addr, host);
	serv_addr.sin_port = htons(port);

	/* set NON BLOCKING here */
	setnonblocking(so);

	if ((rv = connect(so, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0) {
		/* If connection is not yet completed just return 1 */
		if (errno == EINPROGRESS) {
			//fprintf(stderr, ">>> Connection is not yet completed for backend server [%s:%i]: errno %i, %s: RV = [%i]\n", host, port, errno, strerror(errno), rv);

			return 1;
		} else {
			/* this is a real error */
			fprintf(stderr, ">>> ERROR connecting to backend server [%s:%i]: errno %i, %s: RV = [%i]\n", host, port, errno, strerror(errno), rv);
			return -1;
		}
	}

	/* The connection succesfully completed now */

	return 0;
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
 * @param dcb Descriptor Control Block for the connection to which the OK is sent
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

        GWBUF   *buf;

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
/////
/**
 * Write a MySQL CHANGE_USER packet to backend server
 *
 * @param conn  MySQL protocol structure
 * @param dbname The selected database
 * @param user The selected user
 * @param passwd The SHA1(real_password): Note real_password is unknown
 * @return 0 on success, 1 on failure
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

	dcb = conn->descriptor;

//#ifdef DEBUG_MYSQL_CONN
	fprintf(stderr, ">> Sending credentials %s, %s, db %s\n", user, passwd, dbname);
//#endif

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

	// write to backend dcb 
	// ToDO: handle the EAGAIN | EWOULDBLOCK
	rv = write(dcb->fd, GWBUF_DATA(buffer), bytes);
	gwbuf_consume(buffer, bytes);

	conn->state = MYSQL_IDLE;

	if (rv < 0)
		return rv;
	else
		return 0;
}

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

	// get the user's password from repository in SHA1(SHA1(real_password));
	// please note 'real_password' is unknown!
	ret_val = gw_find_mysql_user_password_sha1(username, password, (DCB *) dcb);

	if (ret_val) {
		fprintf(stderr, "<<<< User [%s] was not found\n", username);
		return 1;
	}

	if (token && token_len) {
		// convert in hex format: this is the content of mysql.user table, field password without the '*' prefix
		// and it is 40 bytes long
		gw_bin2hex(hex_double_sha1, password, SHA_DIGEST_LENGTH);
	} else {
		// check if the password is not set in the user table
		if (!strlen((char *)password)) {
			fprintf(stderr, ">>> continue WITHOUT auth, no password\n");
			return 0;
		} else {
			return 1;
		}
	}

	///////////////////////////
	// Auth check in 3 steps
	//////////////////////////

	// Note: token = XOR (SHA1(real_password), SHA1(CONCAT(scramble, SHA1(SHA1(real_password)))))
	// the client sends token
	//
	// Now, server side:
	//
	/////////////
	// step 1: compute the STEP1 = SHA1(CONCAT(scramble, gateway_password))
	// the result in step1 is SHA_DIGEST_LENGTH long
	////////////
	gw_sha1_2_str(scramble, scramble_len, password, SHA_DIGEST_LENGTH, step1);

	////////////
	// step2: STEP2 = XOR(token, STEP1)
	////////////
	// token is trasmitted form client and it's based on the handshake scramble and SHA1(real_passowrd)
	// step1 has been computed in the previous step
	// the result STEP2 is SHA1(the_password_to_check) and is SHA_DIGEST_LENGTH long

	gw_str_xor(step2, token, step1, token_len);

	// copy the stage1_hash back to the caller
	// stage1_hash will be used for backend authentication
	
	memcpy(stage1_hash, step2, SHA_DIGEST_LENGTH);

	///////////
	// step 3: prepare the check_hash
	///////////
	// compute the SHA1(STEP2) that is SHA1(SHA1(the_password_to_check)), and is SHA_DIGEST_LENGTH long
	
	gw_sha1_str(step2, SHA_DIGEST_LENGTH, check_hash);


#ifdef GW_DEBUG_CLIENT_AUTH
	{
		char inpass[128]="";
		gw_bin2hex(inpass, check_hash, SHA_DIGEST_LENGTH);
		
		fprintf(stderr, "The CLIENT hex(SHA1(SHA1(password))) for \"%s\" is [%s]", username, inpass);
	}
#endif

	// now compare SHA1(SHA1(gateway_password)) and check_hash: return 0 is MYSQL_AUTH_OK
	return memcmp(password, check_hash, SHA_DIGEST_LENGTH);
}

/////////////////////////////////////////////////
// get the sha1(sha1(password) from repository
/////////////////////////////////////////////////
int gw_find_mysql_user_password_sha1(char *username, uint8_t *gateway_password, void *repository) {
        SERVICE *service = NULL;
        char *user_password = NULL;

        if (strcmp(username , "root") == 0) {
                return 1;
        }

        service = (SERVICE *) ((DCB *)repository)->service;

        user_password = (char *)users_fetch(service->users, username);

        if (!user_password) {
                fprintf(stderr, ">>> MYSQL user NOT FOUND: %s\n", username);
                return 1;
        }

        // convert hex data (40 bytes) to binary (20 bytes)
        // gateway_password represents the SHA1(SHA1(real_password))
        // please not real_password is unknown and SHA1(real_password)
        // is unknown as well

        if (strlen(user_password))
                gw_hex2bin(gateway_password, user_password, SHA_DIGEST_LENGTH * 2);

        return 0;
}

