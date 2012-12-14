//////////////////////////////////
// SKYSQL GATEWAY main module
// By Massimiliano Pinto 2012
// SkySQL AB
//////////////////////////////////
//
//////////////////////////////////
//				//
// S K Y S Q L    G A T E W A Y //
//				//
//////////////////////////////////

#include "skysql_gw.h"

unsigned int mysql_errno(MYSQL_conn *mysql) {
	return 1146U;
}


const char *mysql_sqlstate(MYSQL_conn *mysql) {
	return "00000";

}

const char *mysql_error(MYSQL_conn *mysql) {
	return "error 1111";
}

static char *strend(register const char *s)
{
  while (*s++);
  return (char*) (s-1);
}

int mysql_select_db(MYSQL_conn *conn, const char *db) {
	apr_status_t rv;
	//uint8_t *packet_buffer = NULL;
	long bytes;
	int ret = 1;
	uint8_t packet_buffer[SMALL_CHUNK] = "";

	// set COMM_INIT_DB	
	packet_buffer[4]= '\x02';
	strncpy(packet_buffer+4+1, db, SMALL_CHUNK - 1);

	//COMM_INIT_DB + DBNAME = paylod
	skysql_set_byte3(packet_buffer, 1 + strlen(packet_buffer+4+1));

	//packet header + payload = bytes to send
	bytes = 4 + 1 + strlen(packet_buffer+4+1);

	// send to server
	rv = apr_socket_send(conn->socket, packet_buffer, &bytes);

	if (rv != APR_SUCCESS) {
		return 1;
	}

	// now read the response from server	
	bytes = SMALL_CHUNK;

	memset(&packet_buffer, '\0', sizeof(packet_buffer));

	rv = apr_socket_recv(conn->socket, packet_buffer, &bytes);
	ret = packet_buffer[4];

	if (rv != APR_SUCCESS) {
		return 1;
	}	

	if (ret == '\x00') 
		return 0;
	else
		return ret;	
}

///////////////////////////////////////
// MYSQL_conn structure setup
// A new standalone pool is allocated
///////////////////////////////////////
MYSQL_conn *mysql_init(MYSQL_conn *data) {
	apr_pool_t *pool = NULL;
	apr_status_t rv = -1;

	MYSQL_conn *input = NULL;

	if (input == NULL) {
		// structure allocation
		input = malloc(sizeof(MYSQL_conn));
		memset(input, '\0', sizeof(MYSQL_conn));

		if (input == NULL)
			return NULL;
		// new pool created
		rv = apr_pool_create_core(&pool);

		if (rv != APR_SUCCESS) {

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "MYSQL_INIT: apr_pool_create_core FAILED\n");
	fflush(stderr);
#endif
			free(input);
			return NULL;
		}

		// the structure now has the pool
		input->pool = pool;
	}

	return input;
}

/////////////////////////////////////
// Send COM_QUIT to server
// Close socket
// free the pool
// free main pointer
/////////////////////////////////////
void mysql_close(MYSQL_conn **ptr) {
	apr_status_t rv;
	uint8_t packet_buffer[5];
	MYSQL_conn *conn = *ptr;

	if (conn == NULL)
		return;

	long bytes = 5;

	// Packet # is 0
	packet_buffer[3]= '\x00';

	// COM_QUIT is \x01
	packet_buffer[4]= '\x01';

	// set packet length to 1
	skysql_set_byte3(packet_buffer, 1);

	if (conn->socket) {
	
		// send COM_QUIT
		rv = apr_socket_send(conn->socket, packet_buffer, &bytes);

		// close socket & free
		apr_socket_close(conn->socket);
	}

	if (conn->pool) {
	
		// pool destroy
		apr_pool_destroy(conn->pool);
		conn->pool = NULL;
	}
#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Open/Close Connection %lu to backend closed/cleaned\n", conn->tid);
	fflush(stderr);
#endif
	// free structure pointer
	if (conn != NULL) {
		free(conn);
		conn = NULL;
		*ptr = NULL;
		ptr = NULL;	
	}

}

int mysql_query(MYSQL_conn *conn, const char *query) {
	apr_status_t rv;
	//uint8_t *packet_buffer=NULL;
	uint8_t packet_buffer[SMALL_CHUNK];
	long bytes;
	int fd;

	//packet_buffer = (uint8_t *) calloc(1, 5 + strlen(query) + 1);
	memset(&packet_buffer, '\0', sizeof(packet_buffer));

	packet_buffer[4]= '\x03';
	strcpy(packet_buffer+5, query);

	skysql_set_byte3(packet_buffer, 1 + strlen(query));

	bytes = 4 + 1 + strlen(query);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "THE QUERY is [%s] len %i\n", query, bytes);
	fprintf(stderr, "THE QUERY TID is [%lu]", conn->tid);
	fprintf(stderr, "THE QUERY scramble is [%s]", conn->scramble);
	if (conn->socket == NULL) {
		fprintf(stderr, "***** THE QUERY sock struct is NULL\n");
	}
	fwrite(packet_buffer, bytes, 1, stderr);
	fflush(stderr);
#endif
	apr_os_sock_get(&fd,conn->socket);

#ifdef MYSQL_CONN_DEBUG
        fprintf(stderr, "QUERY Socket FD is %i\n", fd);
	fflush(stderr);
#endif

	rv = apr_socket_send(conn->socket, packet_buffer, &bytes);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "QUERY SENT [%s]\n", query);
	fflush(stderr);
#endif

	if (rv != APR_SUCCESS) {
		return 1;
	}

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Query [%s] sent\n", query);
	fflush(stderr);
#endif

	return 0;
}


int mysql_print_result(MYSQL_conn *conn) {
	apr_status_t rv;
	uint8_t buffer[MAX_CHUNK];
	long bytes;

	bytes = 1024 * 16;

	memset(buffer, '\0', sizeof(buffer));

	rv = apr_socket_recv(conn->socket, buffer, &bytes);

	if (rv != APR_SUCCESS) {
	return 1;
	}

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Result with %li columns\n", buffer[4]);
	fwrite(buffer, bytes, 1, stderr);
	fflush(stderr);
#endif

	return (int) buffer[4];
}

static int mysql_connect(char *host, int port, char *dbname, char *user, char *passwd, MYSQL_conn *conn) {
	apr_status_t rv;
	int connect = 0;
	int ciclo = 0;
	char buffer[SMALL_CHUNK];
	uint8_t packet_buffer[SMALL_CHUNK];
	char errmesg[128];
	uint8_t *payload = NULL;
	int server_protocol;
	char server_version[100]="";
	uint8_t *server_version_end = NULL;
	uint16_t skysql_server_capabilities_one;
	uint16_t skysql_server_capabilities_two;
	int fd;
	unsigned long tid =0;
	apr_sockaddr_t *connessione;
	apr_socket_t *socket = NULL;
	long bytes;
	uint8_t scramble_data_1[8 + 1] = "";
	uint8_t scramble_data_2[12 + 1] = "";
	uint8_t scramble_data[20 + 1] = "";
	uint8_t capab_ptr[4];
	int scramble_len;
	uint8_t scramble[20 + 1];
	uint8_t client_scramble[20 + 1];
	uint8_t client_capabilities[4];
	uint32_t server_capabilities;
	uint32_t final_capabilities;
	char dbpass[500]="";
	apr_pool_t *pool = NULL;

	pool = conn->pool;

	apr_sockaddr_info_get(&connessione, host, APR_INET, port, 0, pool);

	if ((rv = apr_socket_create(&socket, connessione->family, SOCK_STREAM, APR_PROTO_TCP, pool)) != APR_SUCCESS) {
		fprintf(stderr, "Errore creazione socket: [%s] %i\n", strerror(errno), errno);
		exit;
	}

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Socket initialized\n");
	fflush(stderr);
#endif
	conn->socket=socket;

	rv = apr_socket_opt_set(socket, APR_TCP_NODELAY, 1);
	rv = apr_socket_opt_set(socket, APR_SO_NONBLOCK , 0);

	//apr_socket_timeout_set(socket, 355000);

	if ((rv = apr_socket_connect(socket, connessione)) != APR_SUCCESS) {
		apr_strerror(rv, errmesg, sizeof(errmesg));
		fprintf(stderr, "Errore connect %i, %s: RV = [%i], [%s]\n", errno, strerror(errno), rv, errmesg);
		apr_socket_close(socket);

		return -1;

	} else {
		connect = 1;
	}

	apr_os_sock_get(&fd,socket);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "CONNECT is DONE\n");
	fprintf(stderr, "Socket FD is %i\n", fd);
	fflush(stderr);
#endif

	memset(&buffer, '\0', sizeof(buffer));

	bytes = 16384;


	rv = apr_socket_recv(socket, buffer, &bytes);

	if ( rv == APR_SUCCESS) {
#ifdef MYSQL_CONN_DEBUG
		fprintf(stderr, "RESPONSE ciclo %i HO letto [%s] bytes %li\n",ciclo, buffer, bytes);
		fflush(stderr);
#endif
		ciclo++;
	} else {
		if (APR_STATUS_IS_EOF(rv)) {
#ifdef MYSQL_CONN_DEBUG
			fprintf(stderr, "EOF reached. Bytes = %li\n", bytes);
			fflush(stderr);
#endif
		} else {
#ifdef MYSQL_CONN_DEBUG
			apr_strerror(rv, errmesg, sizeof(errmesg));
			fprintf(stderr, "###### Receive error FINAL : connection not completed %i %s:  RV = [%i], [%s]\n", errno, strerror(errno), rv, errmesg);
#endif
			apr_socket_close(socket);

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

	server_version_end = strend((char*) payload);
	payload = server_version_end + 1;

	// TID
	tid = skysql_get_byte4(payload);
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

	skysql_server_capabilities_one = skysql_get_byte2(payload);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Capab_1[\n");
	fwrite(&skysql_server_capabilities_one, 2, 1, stderr);
	fflush(stderr);
#endif

	//2 capab_part 1 + 1 language + 2 server_status
	payload +=5;

	skysql_server_capabilities_two = skysql_get_byte2(payload);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "]Capab_2[\n");
	fwrite(&skysql_server_capabilities_two, 2, 1, stderr);
	fprintf(stderr, "]\n");
	fflush(stderr);
#endif

	memcpy(&capab_ptr, &skysql_server_capabilities_one, 2);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Capab_1[\n");
	fwrite(capab_ptr, 2, 1, stderr);
	fflush(stderr);
#endif

	memcpy(&(capab_ptr[2]), &skysql_server_capabilities_two, 2);

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
	fwrite(scramble, 20, 1, stderr);
	fprintf(stderr, "\n]\n");
	fflush(stderr);
#endif

	memcpy(conn->scramble, scramble, 20);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Scramble from MYSQL_Conn is  [\n");
	fwrite(scramble, 20, 1, stderr);
	fprintf(stderr, "\n]\n");
	fflush(stderr);
	fprintf(stderr, "Now sending user, pass & db\n[");
	fwrite(&server_capabilities, 4, 1, stderr);
	fprintf(stderr, "]\n");
#endif

	final_capabilities = skysql_get_byte4((uint8_t *)&server_capabilities);

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "CAPABS [%u]\n", final_capabilities);
	fflush(stderr);
#endif
	memset(packet_buffer, '\0', sizeof(packet_buffer));
	//packet_header(byte3 +1 pack#)
	packet_buffer[3] = '\x01';

	//final_capabilities = 1025669;
	final_capabilities |= SKYSQL_CAPABILITIES_PROTOCOL_41;
	final_capabilities &= SKYSQL_CAPABILITIES_CLIENT;

	if (passwd != NULL) {
		uint8_t hash1[APR_SHA1_DIGESTSIZE];
		uint8_t hash2[APR_SHA1_DIGESTSIZE];
		uint8_t new_sha[APR_SHA1_DIGESTSIZE];

		skysql_sha1_str(passwd, strlen(passwd), hash1);
		skysql_sha1_str(hash1, 20, hash2);
		bin2hex(dbpass, hash2, 20);
		skysql_sha1_2_str(scramble, 20, hash2, 20, new_sha);
		skysql_str_xor(client_scramble, new_sha, hash1, 20);

#ifdef MYSQL_CONN_DEBUG
		fprintf(stderr, "Hash1 [%s]\n", hash1);
		fprintf(stderr, "Hash2 [%s]\n", hash2);
		fprintf(stderr, "SHA1(SHA1(password in hex)\n");
		fprintf(stderr, "PAss [%s]\n", dbpass);
		fflush(stderr);
		fprintf(stderr, "newsha [%s]\n", new_sha);
		fprintf(stderr, "Client send scramble 20 [\n");
		fwrite(client_scramble, 20, 1, stderr);
		fprintf(stderr, "\n]\n");
		fflush(stderr);
#endif

	}

	if (dbname == NULL) {
		// now without db!!
		final_capabilities &= ~SKYSQL_CAPABILITIES_CONNECT_WITH_DB;
	} else {
		final_capabilities |= SKYSQL_CAPABILITIES_CONNECT_WITH_DB;
	}


	skysql_set_byte4(client_capabilities, final_capabilities);
	memcpy(packet_buffer + 4, client_capabilities, 4);

	packet_buffer[4] = '\x8d';
	packet_buffer[5] = '\xa6';
	packet_buffer[6] = '\x0f';
	packet_buffer[7] = '\x00';

	skysql_set_byte4(packet_buffer + 4 + 4, 16777216);
	packet_buffer[12] = '\x08';

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "User is [%s]\n", user);
	fflush(stderr);
#endif
	strcpy(packet_buffer+36, user);

	bytes = 32 + 22 + 1 + 1;

	bytes += strlen(user);

	if (dbname == NULL) {
		strcpy(packet_buffer+36 + 5 + 2, "mysql_native_password");
	} else {
		if (passwd != NULL) {
			*(packet_buffer+36 + 5 + 1) = 20;
			memcpy(packet_buffer+36 + 5 + 1 + 1, client_scramble, 20);
			strcpy(packet_buffer+36 + 5 + 1 + 1 + 20, dbname);
			strcpy(packet_buffer+36 + 5 + 1 + 1 + 20 + strlen(dbname) + 1, "mysql_native_password");
			bytes += 20 + strlen(dbname) + 1;
		} else {
			strcpy(packet_buffer+36 + 5 + 1 + 1, dbname);
			strcpy(packet_buffer+36 + 5 + 1 + 1 + strlen(dbname) + 1, "mysql_native_password");
			bytes += strlen(dbname) + 1;
		}
	}

	skysql_set_byte3(packet_buffer, bytes);

	bytes += 4;

	rv = apr_socket_send(socket, packet_buffer, &bytes);

	if (rv != APR_SUCCESS) {
		fprintf(stderr, "CONNECT Error in send auth\n");
	}

	bytes = 4096;

	memset(buffer, '\0', sizeof (buffer));

	rv = apr_socket_recv(socket, buffer, &bytes);


	if (rv != APR_SUCCESS) {
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

		return 0;
	}

	return 1;
}

///////////////////////////////////////
// interaction with apache scoreboard
//message 64 bytes max
///////////////////////////////////////

static int update_gateway_child_status(ap_sb_handle_t *sbh, int status, conn_rec *c, apr_bucket_brigade *bb, char *message) {
	worker_score *ws = ap_get_scoreboard_worker(sbh);
	int old_status = ws->status;

	ws->status = status;

	if (!ap_extended_status) {
		return old_status;
	}

	ws->last_used = apr_time_now();

	/* initial pass only, please - in the name of efficiency */
	if (c) {
		apr_cpystrn(ws->client, ap_get_remote_host(c, c->base_server->lookup_defaults, REMOTE_NOLOOKUP, NULL), sizeof(ws->client));
		apr_cpystrn(ws->vhost, c->base_server->server_hostname, sizeof(ws->vhost));
		/* Deliberate trailing space - filling in string on WRITE passes */
		apr_cpystrn(ws->request, message, sizeof(ws->request));
	}

    return old_status;
}

///////////////////////////////////////////////////
// custom mysqlclose for apache styart new child //
///////////////////////////////////////////////////

void child_mysql_close(MYSQL_conn *conn) {
        apr_status_t rv;
        uint8_t packet_buffer[5];
        long bytes = 5;

	fprintf(stderr, "SkySQL Gateway process ID %lu is exiting\n", getpid());
	fflush(stderr);
	if (conn)
		mysql_close(&conn);
}

///////////////////////////////////////////////
// custom mysqsl_close in process_connection //
///////////////////////////////////////////////

void my_mysql_close(MYSQL_conn *conn, conn_rec *c) {
	int fd=-1;

	apr_os_sock_get(&fd,conn->socket);	

	if (fd) {
		if (c !=NULL)
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Connection TID %lu to backend server closed", conn->tid);
	} else {
		if (c !=NULL)
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "init resources free");
	}

	mysql_close(&conn);
}

///////////////////////////////////////////////////////////
// the mysql protocol implementation at connection level //
///////////////////////////////////////////////////////////

static int skysql_process_connection(conn_rec *c) {
        apr_bucket_brigade *r_bb;
        apr_bucket_brigade *bb;
        apr_bucket *b;
        apr_bucket *auth_bucket;
        apr_bucket *bucket;
        apr_status_t rv;
        int seen_eos = 0;
        int child_stopped_reading = 0;

	int num_fields;
	int i;
	uint8_t header_result_packet[4];
	uint8_t result_column_count;
	int result_version_len = 0;
	char *query_from_client = NULL;
	int query_from_client_len = 0;
	char *client_auth_packet = NULL;
	unsigned int query_ret = 0;
	int return_data = 0;
	int input_read = 0;
	unsigned int skysql_errno = 0;
	const char *skysql_error_msg = NULL;
	const char *skysql_state = NULL;
	uint8_t *outbuf = NULL;
	uint8_t client_flags[4];

	int load_balancing_servers = 0;
	int current_slave = -1;

        skysql_server_conf *conf;
	char *current_slave_server_host = NULL;
	int current_slave_server_port = 3306;
	skysql_client_auth *mysql_client_data = NULL;
	mysql_driver_details *mysql_driver = NULL;
	MYSQL_conn *conn = NULL;
	apr_pool_t *pool = NULL;
	int max_queries_per_connection = 0;
	uint8_t mysql_command = 0;
	char tmp_buffer[10001]="";
	unsigned long tmp_buffer_len = 0L;

	uint8_t scramble[20]="";
	int scramble_len = 0;
	
	uint8_t stage1_hash[20 +1] ="";

	conn_details *find_server = NULL;
	char *selected_host = NULL;
	char *selected_dbname = NULL;
	int selected_shard = 0;
	int selected_port = 0;

	apr_interval_time_t timeout = 300000000;

	/////////////////////////////////////////
	// basic infos from configuration file
	/////////////////////////////////////////
	conf = (skysql_server_conf *)ap_get_module_config(c->base_server->module_config, &skysql_module);

	///////////////////////////////////////////
	// MYSQL Protocol switch in configuration
	///////////////////////////////////////////
	if (!conf->protocol_enabled) {
		return DECLINED;
	}

	///////////////////////////////////////////////
	// now setting the timeout form configuration
	///////////////////////////////////////////////
	
	if (conf->loop_timeout > 0) {
		timeout = conf->loop_timeout * 1000000;
	}

	////////////////////////////////////
	// apache scoreboard update
	// aka, customizing server-status!!
	/////////////////////////////////////

	ap_time_process_request(c->sbh, START_PREQUEST);
	update_gateway_child_status(c->sbh, SERVER_READY, c, NULL, "GATEWAY: MYSQL ready ");

	//////////////////////////
	// now the c->pool is ok
	//////////////////////////
	pool = c->pool;
	
	///////////////////////////////
	// mysql server/client detail
	///////////////////////////////
	mysql_client_data = apr_pcalloc(pool, sizeof(skysql_client_auth));
	mysql_driver = apr_pcalloc(pool, sizeof(mysql_driver_details));
	mysql_client_data->driver_details = (mysql_driver_details *) mysql_driver;

	// yeah, one connection
	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "I got a connection!, id [%i]", c->id);
	
	////////////////////////////////////////////////////////////////////////////////
	// default scenario is to perform here protocol handshake and the autentication
	////////////////////////////////////////////////////////////////////////////////

	//////////////////////////////////////
	// MYSQL 5.1/5.5 Compatible handshake
	// todo: a return structure with connection data: capabilities, scramble_buff

	update_gateway_child_status(c->sbh, SERVER_BUSY_WRITE, c, NULL, "GATEWAY: MYSQL handshake sent ");

	rv = skysql_send_handshake(c, scramble, &scramble_len);

	update_gateway_child_status(c->sbh, SERVER_BUSY_READ, c, NULL, "GATEWAY: MYSQL Auth read ");

	///////////////////////////////////////
	// now read the client authentication
	// and return data structure with client details, dbname, username, and the stage1_hash
	// the latest is for further backend authentication with same user/pass

	rv = skysql_read_client_autentication(c, pool, scramble, scramble_len, mysql_client_data, stage1_hash);

	// client authentication data stored

	if (!rv) {
		// todo implement custom error packet
		// message and return status
		skysql_send_ok(c, pool, 2, 0, NULL);
		ap_log_error(APLOG_MARK, APLOG_ERR, 0, c->base_server, "*** MySQL Authentication FALSE, thread ID is %i", getpid());

		return HTTP_INTERNAL_SERVER_ERROR;
	}
	
	update_gateway_child_status(c->sbh, SERVER_BUSY_WRITE, c, NULL, "GATEWAY: MYSQL Auth Done ");

	///////////////////////////////	
	// ok, client is autenticated
	// akwnoledge it!
	///////////////////////////////
	skysql_send_ok(c, pool, 2, 0, NULL);

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "MySQL Authentication OK, thread ID is %i", getpid());

	//////////////////////////////
	// check if db is in connect
	//////////////////////////////
	if (mysql_driver->connect_with_db) {
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "DB is in connect packet");
	}
	
	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "current username is [%s]", mysql_client_data->username);
	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "current DB is [%s]", mysql_client_data->database != NULL ? mysql_client_data->database : "");

	// now the pool pointer is set to NULL
	pool = NULL;

	//////////////////////////
	// check pooling config
	/////////////////////////

        if (!conf->pool_enabled) {
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "MySQL backend open/close");
		conn = mysql_init(NULL);

		if (conn == NULL) {
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, c->base_server, "MYSQL init Error %u: %s", 1, "No memory");
                        return 500;
                }

		// do the connect
		// find config data
		find_server = apr_hash_get(conf->resources, "loadbal", APR_HASH_KEY_STRING);
		if (find_server != NULL) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQL config find was DONE");
	
			//switch(find_server->type)
			if (find_server->nshards == 1) {	
				selected_port = atoi(strchr(find_server->server_list, ':') + 1);
				selected_host = apr_pstrndup(c->pool, find_server->server_list, strchr(find_server->server_list, ':') - find_server->server_list);
				selected_shard = 1;
			} else {
				selected_shard = select_random_slave_server(find_server->nshards);
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQL config find [%i] servers", find_server->nshards);
				get_server_from_list(&selected_host, &selected_port, find_server->server_list, selected_shard, c->pool);
			}
		} else {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQL config find KO: using default!");
			selected_port = 3306;
			selected_host = apr_pstrdup(c->pool, "127.0.0.1");
		}

		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQL backend selection [%i], [%s]:[%i]", selected_shard, selected_host, selected_port);

		if (mysql_client_data->database != NULL) {
			selected_dbname = mysql_client_data->database;
		} else {
			selected_dbname = "test";
		}
		
		if (mysql_connect(selected_host, selected_port, selected_dbname, mysql_client_data->username, "pippo", conn) != 0) {
		//if (mysql_real_connect(conn, "192.168.1.40", "root", "pippo", "test", 3306, NULL, 0) == NULL) //
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, c->base_server, "MYSQL Connect [%s:%i] Error %u: %s", selected_host, selected_port, mysql_errno(conn), mysql_error(conn));
			return 500;
		} else {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SkySQL RunTime Opened connection to backend");
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Backend Server TID %i, scamble_buf [%5s]", conn->tid, conn->scramble);
		}

	} else {
		// use the pool
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "MySQL backend pool");
		conn = conf->conn;
	}

	update_gateway_child_status(c->sbh, SERVER_BUSY_READ, c, NULL, apr_psprintf(c->pool, "GATEWAY: MYSQL backend selected, DB [%s] ", selected_dbname));

	//////////////////////////////////////////////////////
	// main loop
	// speaking MySQL protocol 5.1/5.5
	//////////////////////////////////////////////////////

	////////////////////////////////////////////////////
	// here applying the timeout to the current socket
	// this protects/saves the main loop
	// so ... choose the right value

	apr_socket_timeout_set(ap_get_conn_socket(c), timeout);

	while(1) {
		//////////////////////////////////////////////////////////////
		// the new pool is allocated on c->pool
		// this new pool is the right one for the while(1) main loop
		// it MUST BE destroyed just before exiting the loop, or on 
		// a break statement
		// take care of it
		//////////////////////////////////////////////////////////////
		
		apr_pool_create(&pool, c->pool);

		r_bb = apr_brigade_create(pool, c->bucket_alloc);

		/////////////////////////
		// reading client input
		/////////////////////////

		child_stopped_reading = 0;
		input_read = 0;

		update_gateway_child_status(c->sbh, SERVER_BUSY_KEEPALIVE, c, NULL, apr_psprintf(pool, "GATEWAY: MYSQL loop, DB [%s]", selected_dbname));

		///////////////////////////////////////////////
		// Get input bytes from the client, blocking
		// TODO: handle multi packet input
		// or reading larger data input
		// yes, this is only one brigade!
		///////////////////////////////////////////////

		if (((rv = ap_get_brigade(c->input_filters, r_bb, AP_MODE_READBYTES, APR_BLOCK_READ, 8192)) != APR_SUCCESS) || APR_BRIGADE_EMPTY(r_bb)) {
			char errmsg[256]="";
			// is this an error?
			//apr_brigade_cleanup(r_bb);
			//apr_brigade_destroy(r_bb);
			apr_strerror(rv, errmsg, sizeof(errmsg));
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, ">>> No more data from client, in ap_get_brigade [%s]", errmsg);

			//apr_pool_destroy(pool);
			// this breaks the main loop
			//break;
		}

		/////////////////////////////////////////////
		// now extract data bucket from the brigade
		/////////////////////////////////////////////

		for (bucket = APR_BRIGADE_FIRST(r_bb); bucket != APR_BRIGADE_SENTINEL(r_bb); bucket = APR_BUCKET_NEXT(bucket)) {
			apr_size_t len = 0;
			const char *data = NULL;
			if (APR_BUCKET_IS_EOS(bucket)) {
				seen_eos = 1;
				break;
			}

			if (APR_BUCKET_IS_FLUSH(bucket)) {
				continue;
			}

			if (child_stopped_reading) {
				// the statement breaks this 'for' loop NOT the main loop with 'while'!
				break;
			}

			///////////////////////////
			// reading a bucket
			// what to do with large input data, as 'mysql load data'????
			///////////////////////////
			rv = apr_bucket_read(bucket, &data, &len, APR_BLOCK_READ);

			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Input data with len [%i]", len);

			if (rv != APR_SUCCESS) {
				char errmsg[256]="";
				apr_strerror(rv, errmsg, sizeof(errmsg));
				child_stopped_reading = 1;
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Child stopped reading [%s]", errmsg);
			}
			////////////////////////////////////////////////////////
			// current data is copied into a pool allocated buffer 
			////////////////////////////////////////////////////////
			query_from_client = (char *)apr_pstrmemdup(pool, data, len);
			query_from_client_len = len;

			input_read = 1;
		}

		// let's destroy the brigate, it's useless now
		apr_brigade_destroy(r_bb);

		// now handle client input
		if (input_read == 1 && query_from_client != NULL) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Client Input [%s], command [%x]", query_from_client+5, query_from_client[4]);
		} else {
			// no data read
			// input buffer NULL or empty
			// what to do?
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SkySQL Gateway main loop: input is empty, exiting");
			apr_pool_destroy(pool);
			break;
		}

		//prepare custom error response if max is raised
		max_queries_per_connection++;
		if (max_queries_per_connection > 1000000002) {
			ap_log_error(APLOG_MARK, APLOG_ERR, 0, c->base_server, "max_queries_per_connection reached = %li", max_queries_per_connection);
			gateway_send_error(c, pool, 1);
			apr_pool_destroy(pool);

			// if (die_on__max_queries_per_connection)
			//break;
			continue;
		}

		// check the mysql thread id, for pre-openend the ti is in conf->tid
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Serving Client with MySQL Thread ID [%lu]", conn->tid);

		mysql_command = query_from_client[4];

		/////////////////////////////////////
		// now processing the mysql_command
		/////////////////////////////////////

		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Client Input command [%x]", mysql_command);

		update_gateway_child_status(c->sbh, SERVER_BUSY_KEEPALIVE, c, NULL, apr_psprintf(pool, "GATEWAY: MYSQL loop Command [%x], DB [%s]", mysql_command, selected_dbname));

		switch (mysql_command) {
			case 0x0e :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_PING");
				// reponse sent directly to the client
				// no ping to backend, for now
				skysql_send_ok(c, pool, 1, 0, NULL);

				break;
			case 0x04 :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_FIELD_LIST", query_from_client+5);
				skysql_send_ok(c, pool, 1, 0, NULL);

				break;
			case 0x1b :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_SET_OPTION");
				skysql_send_eof(c, pool, 1);

				break;
			case 0x0d :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_DEBUG");
				skysql_send_ok(c, pool, 1, 0, NULL);
				
				break;
			case 0x03 :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_QUERY");
				skygateway_query_result(c, pool, conn, query_from_client+5);
				//skysql_send_ok(c, pool, 1, 0, NULL);

				break;
			case 0x16 :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_PREPARE");
				skygateway_statement_prepare_result(c, pool, conn, query_from_client+5, query_from_client_len-5);

				break;
			case 0x17 :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_EXECUTE");
				skygateway_statement_execute_result(c, pool, conn, query_from_client+5, query_from_client_len-5);

				break;
			case 0x19 :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_CLOSE");

				mysql_send_command(conn, query_from_client+5, 0x19, query_from_client_len-5);

				break;
			case 0x02 :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_INIT_DB");
				//mysql_select_db(conn, query_from_client+5);
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_INIT_DB", query_from_client+5);
				// reponse sent to the client
				skysql_send_ok(c, pool, 1, 0, NULL);

				break;
			case 0x01 :
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_QUIT");
				// QUIT received
				// close backend connection if not pooled
				// and exit the switch

				if (!conf->pool_enabled) {
					mysql_close(&conn);
					ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "MYSQL_conn is NULL? %i", conn == NULL ? 1 : 0);
				}
				break;
			dafault :
				ap_log_error(APLOG_MARK, APLOG_ERR, 0, c->base_server, "UNKNOW MYSQL PROTOCOL COMMAND [%x]", mysql_command);
				// reponse sent to the client, with custom error: TODO
				skysql_send_ok(c, pool, 1, 0, "unknow command");
				break;
		}

		/////////////////////////
		// now all is done: destroy immediately all resources in the new poll
		// the loop continues with no resources allocated
		apr_pool_destroy(pool);

		// if COM_QUIT terminate the main loop!
		if (mysql_command == 0x01) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "COM_QUIT has been received, the main loop now ends");
			break;	
		} else {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "the main loop continues");
			continue;
		}

		////////////////////////////
		// main loop now ends
		////////////////////////////

	}

    	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Main loop ended!");

	if (conn != NULL) {
		if (!conf->pool_enabled) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, ">> opened connection found!, close it with COM_QUIT");

			mysql_close(&conn);
			}
	}

	// hey, it was okay to handle the protocol connectioni, is thereanything else to do?

	update_gateway_child_status(c->sbh, SERVER_CLOSING, c, NULL, "GATEWAY: MYSQL quit ");

	ap_time_process_request(c->sbh, STOP_PREQUEST);

	return OK;
}

/////////////////////////////////
// The sample content handler 
// Only with HTTP protocol
// so it's useless now
// will be useful with JSON
////////////////////////////////
static int skysql_handler(request_rec *r)
{
    if (strcmp(r->handler, "skysql")) {
        return DECLINED;
    }
    r->content_type = "text/html";      

    if (!r->header_only)
        ap_rputs("The sample page from mod_skysql.c\n", r);
    return OK;
}

/////////////////////////////////
// Module Initialization
// Persistent structures & data
/////////////////////////////////

static int skysql_init_module(apr_pool_t *p, apr_pool_t *plog, apr_pool_t *ptemp, server_rec *base_server) {
	server_rec *s;

	s = base_server;
/*
	do initialization here
*/
	ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, "SKYSQL Init: Internal structure done");
	ap_log_error(APLOG_MARK, APLOG_INFO, 0, s, "SKYSQL Init: ext file ver is [%i]", skysql_ext_file_ver());


	return OK;
}

////////////////////////////////////////
// Child Initialization 
// If enabled, per child connection(s)
////////////////////////////////////////

static void skysql_child_init(apr_pool_t *p, server_rec *s) {
	// take care of virtualhosts ...
	while(s) {
        	skysql_server_conf *conf;
		conf = (skysql_server_conf *)ap_get_module_config(s->module_config, &skysql_module);

		if (conf->protocol_enabled && conf->pool_enabled) {

			// MySQL Init
			conf->conn = mysql_init(NULL);
			conf->conn->pool = p;

			// store child process id
			conf->gateway_id = getpid();

			if (conf->conn == NULL) {
				ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "MYSQL init Error %u: %s\n", mysql_errno(conf->conn), mysql_error(conf->conn));
				return;
			}
			if (mysql_connect("127.0.0.1", 3306, "test", "pippo", "pippo", conf->conn) != 0) {
			//if (mysql_real_connect(conf->conn, "192.168.1.40", "root", "pippo", "test", 3306, NULL, 0) == NULL) {
				ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "MYSQL Connect Error %u: %s\n", mysql_errno(conf->conn), mysql_error(conf->conn));
				return ;
			} else {
				conf->mysql_tid = conf->conn->tid;
				ap_log_error(APLOG_MARK, APLOG_ERR, 0, s, "PID %li SkySQL Child Init & Open connection TID %lu to backend", getpid(), conf->mysql_tid);
			}

			// structure deallocation & connection close
			apr_pool_cleanup_register(p, conf->conn, child_mysql_close, child_mysql_close);

		} else {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, "Generic init flags %i, %i, Skip Protocol Setup & Skip database connection", conf->protocol_enabled, conf->pool_enabled);
		}

		// next virtual host ..
		s = s->next;	
	}
}

////////////////////////////////////////
// Creating defaulf configuration data
////////////////////////////////////////
static void * create_skysql_config(apr_pool_t *p, server_rec *s) {
	skysql_server_conf *ps = apr_pcalloc(p, sizeof(skysql_server_conf));
	ps->conn = NULL;
	ps->protocol_enabled = 0;
	ps->pool_enabled = 0;
	ps->resources = apr_hash_make(p);
	ps->loop_timeout = 300;
	
	return ps;
}

/////////////////////////////
// Enabling MySQL Protocol //
/////////////////////////////
static const char *skysql_protocol_enable(cmd_parms *cmd, void *dummy, int arg)
{
	skysql_server_conf *sconf = ap_get_module_config(cmd->server->module_config, &skysql_module);
	sconf->protocol_enabled = arg;

	return NULL;
}

/////////////////////////////////
// Enabling MySQL loop timeout //
/////////////////////////////////
static const char *skysql_loop_timeout(cmd_parms *cmd, void *dummy, const char *arg)
{
	skysql_server_conf *sconf = ap_get_module_config(cmd->server->module_config, &skysql_module);
	
	sconf->loop_timeout = atoi(arg);

	return NULL;
}

/////////////////////////////////////////////
// Enabling per child persistent connection
/////////////////////////////////////////////
static const char *skysql_pool_enable(cmd_parms *cmd, void *dummy, int arg)
{
        skysql_server_conf *sconf = ap_get_module_config(cmd->server->module_config, &skysql_module);
        sconf->pool_enabled = arg;

        return NULL;
}

static const char *skysql_single_db_resource(cmd_parms *cmd, void *dconf, const char *a1, const char *a2) {
	char *ptr_port = NULL;
	char *ptr_db = NULL;
	char *ptr_host = NULL;
	char *ptr_list = NULL;

	skysql_server_conf *conf = ap_get_module_config(cmd->server->module_config, &skysql_module);

	conn_details *newresource = apr_pcalloc(cmd->pool, sizeof(conn_details));

	newresource->raw_config = apr_pstrdup(cmd->pool, a2);
	newresource->name = apr_pstrdup(cmd->pool, a1);

	ptr_db = strchr(a2, ';');
	newresource->server_list = apr_pstrndup(cmd->pool, a2, ptr_db-a2);
	newresource->dbname = apr_pstrdup(cmd->pool, ptr_db+1);

	newresource->nshards = 1;

	ptr_list = newresource->server_list;
	ptr_host = ptr_list;
	while((ptr_host = strchr(ptr_list, ',')) != NULL) {
		newresource->nshards++;
		ptr_list = ptr_host + 1;
	}
	// now put the struct in the hash table
	apr_hash_set(conf->resources, apr_pstrdup(cmd->pool, a1), APR_HASH_KEY_STRING, newresource);

	// creare un contenitore, table??? da agganciare con la key a1 e value a2
	fprintf(stderr, "Config Resource %s with %i servers, [%s]\n", a1, newresource->nshards, newresource->server_list);
	fflush(stderr);

	return NULL;
}

//////////////////////////////
// commands implemeted here //
//////////////////////////////
static const command_rec skysql_cmds[] = {
	AP_INIT_FLAG("SkySQLProtocol", skysql_protocol_enable, NULL, RSRC_CONF, "Run an MYSQL protocol on this host"),
	AP_INIT_FLAG("SkySQLPool", skysql_pool_enable, NULL, RSRC_CONF, "SKYSQL backend servers pool"),
	AP_INIT_TAKE2("SkySQLSingleDBbresource",      skysql_single_db_resource,      NULL, OR_FILEINFO, "a single db resource name"),
	AP_INIT_TAKE1("SkySQLTimeout",      skysql_loop_timeout,      NULL, OR_FILEINFO, "MYSQL protocol loop timeout"),
	// SkySQLMaxQueryPerConnection
	{NULL}
};

////////////////////////////
// hooks implemented here //
////////////////////////////
static void skysql_register_hooks(apr_pool_t *p)
{
	ap_hook_post_config(skysql_init_module, NULL,NULL, APR_HOOK_MIDDLE);
	ap_hook_child_init(skysql_child_init, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_process_connection(skysql_process_connection, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_handler(skysql_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/////////////////////////////////
// Dispatch list for API hooks //
/////////////////////////////////

module AP_MODULE_DECLARE_DATA skysql_module = {
    STANDARD20_MODULE_STUFF, 
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    create_skysql_config,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
    skysql_cmds,                  /* table of config file commands       */
    skysql_register_hooks  /* register hooks                      */
};

///////////////////////////////////////////////
