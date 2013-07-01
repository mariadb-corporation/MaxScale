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

/* 
 * Read the backend server handshake  
 */
int gw_read_backend_handshake(MySQLProtocol *conn) {
	GWBUF *head = NULL;
	DCB *dcb = conn->descriptor;
	int n = -1;
	uint8_t *payload = NULL;

	if ((n = dcb_read(dcb, &head)) != -1) {
		dcb->state = DCB_STATE_PROCESSING;
		if (head) {
			payload = GWBUF_DATA(head);

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

/*
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

/*
 Receive the MySQL authentication packet from backend, packet # is 2
*/
int gw_receive_backend_auth(MySQLProtocol *conn) {
	int rv = 1;
	int n = -1;
	GWBUF *head = NULL;
	DCB *dcb = conn->descriptor;

	if ((n = dcb_read(dcb, &head)) != -1) {
		dcb->state = DCB_STATE_PROCESSING;
		if (head) {
			uint8_t *ptr = GWBUF_DATA(head);
			// check if the auth is SUCCESFUL
			if (ptr[4] == '\x00') {
				// Auth is OK 
				conn->state = MYSQL_IDLE;

				rv = 0;
			} else {
				conn->state = MYSQL_AUTH_FAILED;

				rv = 1;
			}

			// consume all the data here
			head = gwbuf_consume(head, gwbuf_length(head));
		}
	}

	dcb->state = DCB_STATE_POLLING;

	return rv;
}

/*
 * send authentication to backend
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

	fprintf(stderr, ">> Sending credentials %s, %s, db %s\n", user, passwd, dbname);

	// Zero the vars
	memset(&server_capabilities, '\0', sizeof(server_capabilities));
	memset(&final_capabilities, '\0', sizeof(final_capabilities));

        final_capabilities = gw_mysql_get_byte4((uint8_t *)&server_capabilities);

        final_capabilities |= GW_MYSQL_CAPABILITIES_PROTOCOL_41;
        final_capabilities |= GW_MYSQL_CAPABILITIES_CLIENT;

        if (compress) {
                final_capabilities |= GW_MYSQL_CAPABILITIES_COMPRESS;
                fprintf(stderr, ">>>> Backend Connection with compression\n");
                fflush(stderr);
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

        // 4 + 4 + 4 + 1 + 23 = 36

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

        gw_mysql_set_byte3(payload_start, (bytes-4));

	// write to backend dcb 
	rv = dcb->func.write(dcb, buffer);

	conn->state = MYSQL_AUTH_RECV;

	if (rv < 0)
		return rv;
	else
		return 0;
}
/////
