////////////////////////////////////////
// SKYSQL Utils
// By Massimiliano Pinto 2012/2013
// SkySQL AB
////////////////////////////////////////

#include "skysql_gw.h"
#include "apr_sha1.h"
#include "apr_general.h"

#define MYSQL_PROTOCOL_VERSION41_CHAR '*'

#define char_val(X) (X >= '0' && X <= '9' ? X-'0' :\
                     X >= 'A' && X <= 'Z' ? X-'A'+10 :\
                     X >= 'a' && X <= 'z' ? X-'a'+10 :\
                     '\177')

char hex_upper[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char hex_lower[] = "0123456789abcdefghijklmnopqrstuvwxyz";

/////////////////////////////////
// binary data to hex string
// output must be pre allocated
/////////////////////////////////
char *bin2hex(char *out, const uint8_t *in, unsigned int len) {
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

/////////////////////////////////
// hex string to binary data
// output must be pre allocated
/////////////////////////////////
int hex2bin(uint8_t *out, const char *in, unsigned int len) {
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
// general random string
// output must be pre allocated
/////////////////////////////////
void skysql_set_random_str(uint8_t *output, unsigned int length) {
	uint8_t *ptr = output;
	apr_status_t rv = apr_generate_random_bytes(output, length);

	// this is for debug, the same scramble for every handshake
	//strcpy(output, "12345678abcdefjhilmn");
}

/////////////////////////////////////////////////////////////
// fill a 20 bytes preallocated with SHA1 digest (160 bits)
// for one input on in_len bytes
/////////////////////////////////////////////////////////////
void skysql_sha1_str(const uint8_t *in, int in_len, uint8_t *out) {
    int l;
    apr_sha1_ctx_t context;
    apr_byte_t digest[APR_SHA1_DIGESTSIZE];

    apr_sha1_init(&context);
    apr_sha1_update(&context, in, in_len);
    apr_sha1_final(digest, &context);

    memcpy(out, digest, APR_SHA1_DIGESTSIZE);
}

/////////////////////////////////////////////////////////////
// fill 20 bytes preallocated with SHA1 digest (160 bits)
// for two inputs, in_len and in2_len bytes
/////////////////////////////////////////////////////////////
void skysql_sha1_2_str(const uint8_t *in, int in_len, const uint8_t *in2, int in2_len, uint8_t *out) {
    int l;
    apr_sha1_ctx_t context;
    apr_byte_t digest[APR_SHA1_DIGESTSIZE];

    apr_sha1_init(&context);
    apr_sha1_update(&context, in, in_len);
    apr_sha1_update(&context, in2, in2_len);
    apr_sha1_final(digest, &context);

    memcpy(out, digest, APR_SHA1_DIGESTSIZE);
}

///////////////////////////////////////////////////////
// fill a preallocated buffer with XOR(str1, str2)
// XOR between 2 equal len strings
// note that XOR(str1, XOR(str1 CONCAT str2)) == str2
// and that  XOR(str1, str2) == XOR(str2, str1)
///////////////////////////////////////////////////////

void skysql_str_xor(char *output, const uint8_t *input1, const uint8_t *input2, unsigned int len) {
	const uint8_t *input1_end = NULL;
	input1_end = input1 + len;
	while (input1 < input1_end)
		*output++= *input1++ ^ *input2++;
	
	*output = '\0';
}

//////////////////////////////////////////
// get skygateway password from username
// output is SHA1(SHA1(password))
//////////////////////////////////////////

char *gateway_find_user_password_sha1(char *username, void *repository, conn_rec *c, apr_pool_t *p) {

	uint8_t hash1[APR_SHA1_DIGESTSIZE];
	uint8_t hash2[APR_SHA1_DIGESTSIZE];

	skysql_sha1_str(username, strlen(username), hash1);
	skysql_sha1_str(hash1, APR_SHA1_DIGESTSIZE, hash2);

	return apr_pstrmemdup(p, hash2, APR_SHA1_DIGESTSIZE);
}

/////////////////////////////////////////////
// get the SHA1(SHA1(password)) from client
/////////////////////////////////////////////

int skysql_check_scramble(conn_rec *c, apr_pool_t *p, uint8_t *token, unsigned int token_len, uint8_t *scramble, unsigned int scramble_len, char *username, uint8_t *stage1_hash) {
	uint8_t step1[APR_SHA1_DIGESTSIZE];
	uint8_t step2[APR_SHA1_DIGESTSIZE +1];
	uint8_t check_hash[APR_SHA1_DIGESTSIZE];
	char hex_double_sha1[2 * APR_SHA1_DIGESTSIZE + 1]="";

	uint8_t *password = gateway_find_user_password_sha1(username, NULL, c, p);

	bin2hex(hex_double_sha1, password, APR_SHA1_DIGESTSIZE);
	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "The Gateway stored hex(SHA1(SHA1(password))) for \"%s\" [%s]", username, hex_double_sha1);

	// possible, now skipped
	/*
	if (password == NULL) {
		??????
	}
	*/

	// step 1
	skysql_sha1_2_str(scramble, scramble_len, password, APR_SHA1_DIGESTSIZE, step1);

	//step2
	skysql_str_xor(step2, token, step1, token_len);

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "skygateway SHA1(password) [%s]", step2);
	memcpy(stage1_hash, step2, 20);

	skysql_sha1_str(step2, APR_SHA1_DIGESTSIZE, check_hash);

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SHA1 di SHA1(client password) [%s]", check_hash);

	if (1) {
		char inpass[100]="";
		bin2hex(inpass, check_hash, 20);
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "The CLIENT hex(SHA1(SHA1(password))) for \"%s\" [%s]", username, inpass);
	}

	return memcmp(password, check_hash,  APR_SHA1_DIGESTSIZE);
}

apr_status_t gateway_reply_data(conn_rec *c, apr_pool_t *pool, void *data, int len) {
	apr_status_t rv = APR_SUCCESS;
	apr_bucket_brigade *bb;
	apr_bucket_brigade *r_bb;

	// create brigade
	bb = apr_brigade_create(pool, c->bucket_alloc);

	apr_brigade_write(bb, ap_filter_flush, c->output_filters, data, len);
	ap_fflush(c->output_filters, bb);

	apr_brigade_destroy(bb);

	return 1;
}

apr_status_t skysql_change_user(conn_rec *c, apr_pool_t *p, char *username, char *database, MYSQL_conn *conn, uint8_t *stage1_hash) {
        uint8_t skysql_payload_size = 0;
	uint8_t skysql_packet_header[4];
	uint8_t skysql_packet_id = 0;
	uint8_t change_user_command = 0x11;
	uint8_t *outbuf = NULL;
	uint8_t token[20 + 1]="";
	uint8_t charset[2]="";
	uint8_t backend_scramble[20 +1]="";

	int user_len = strlen(username);
	int database_len = strlen(database);
	uint8_t *password = NULL;

	uint8_t temp_token[20 +1] ="";
	uint8_t stage1_password[20 +1] ="";
	apr_status_t rv = -1;
	long bytes;

	//get password from repository
	password = gateway_find_user_password_sha1(username, NULL, c, p);
	memcpy(backend_scramble, conn->scramble, 20);

	skysql_sha1_2_str(backend_scramble, 20, password, 20, temp_token);

	*token = '\x14';

	charset[0]='\x08';
	charset[1]='\x00';

        skysql_str_xor(token+1, temp_token, stage1_hash, 20);

        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "skygateway TO backend scramble [%s]", backend_scramble);
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "skygateway SHA1(password) [%s]", stage1_hash);
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "skygateway internal password [%s]", password);
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "skygateway SHA1(scramble + SHA1(stage1_hash)) [%s]", temp_token);
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "skygateway TO backend token [%s]", token+1);

	//skysql_payload_size = 1 + user_len + 1 + sizeof(token) + database_len + 1 + sizeof(charset) + 1 + sizeof("mysql_native_password") + 1; 
	skysql_payload_size = 1 + user_len + 1 + sizeof(token) + database_len + 1 + sizeof(charset);

	// allocate memory for packet header + payload
	outbuf = (uint8_t *) apr_pcalloc(p, sizeof(skysql_packet_header) + skysql_payload_size);

	// write packet header with packet number
	skysql_set_byte3(skysql_packet_header, skysql_payload_size);
	skysql_packet_header[3] = skysql_packet_id;

	memcpy(outbuf, skysql_packet_header, sizeof(skysql_packet_header));
	memcpy(outbuf + sizeof(skysql_packet_header), &change_user_command, 1);
	memcpy(outbuf + sizeof(skysql_packet_header) + 1, username, user_len);
	memcpy(outbuf + sizeof(skysql_packet_header) + 1 + strlen(username) + 1, token, 21);
	memcpy(outbuf + sizeof(skysql_packet_header) + 1 + strlen(username) + 1 + 21, database, database_len);
	memcpy(outbuf + sizeof(skysql_packet_header) + 1 + strlen(username) + 1 + 21 + database_len + 1, charset, sizeof(charset));
	//memcpy(outbuf + sizeof(skysql_packet_header) + 1 + strlen(username) + 1 + 21 + database_len + 1 + sizeof(charset) + 1,  "mysql_native_password", strlen("mysql_native_password"));

	bytes = sizeof(skysql_packet_header) + skysql_payload_size;
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "skysql_change_user is %li bytes", bytes);
	rv = apr_socket_send(conn->socket, outbuf, &bytes);
}

apr_status_t skysql_read_client_autentication(conn_rec *c, apr_pool_t *pool, uint8_t *scramble, int scramble_len, skysql_client_auth *mysql_client_data, uint8_t *stage1_hash) {
        apr_bucket_brigade *r_bb;
        apr_bucket *r_b;
        apr_status_t rv;
        int seen_eos = 0;
        int child_stopped_reading = 0;
        int i;
        apr_bucket *auth_bucket;
        apr_bucket *bucket;
        const char *client_auth_packet = NULL;
        unsigned int query_ret = 0;
        int return_data = 0;
        int input_read = 0;
        uint8_t client_flags[4];
        char *current_slave_server_host = NULL;
        int current_slave_server_port = 3306;
	apr_pool_t *p = NULL;
	mysql_driver_details *mysql_driver = NULL;
	
	uint8_t *token = NULL;
	unsigned int token_len = 0;
	int auth_ret = 0;

	// use the passed pool?
	p = pool == NULL ? c->pool : pool;

        // now read the client authentication
	r_bb = apr_brigade_create(p, c->bucket_alloc);

	if (((rv = ap_get_brigade(c->input_filters, r_bb, AP_MODE_READBYTES, APR_BLOCK_READ, 8192)) != APR_SUCCESS) || APR_BRIGADE_EMPTY(r_bb)) {
		apr_brigade_destroy(r_bb);
		return input_read;
	}

	for (auth_bucket = APR_BRIGADE_FIRST(r_bb); bucket != APR_BRIGADE_SENTINEL(r_bb); bucket = APR_BUCKET_NEXT(auth_bucket)) {
		apr_size_t len;
		const char *data;

		if (APR_BUCKET_IS_EOS(auth_bucket)) {
			seen_eos = 1;
			break;
		}

		if (APR_BUCKET_IS_FLUSH(auth_bucket)) {
			continue;
		}

		if (child_stopped_reading) {
			break;
		}

		rv = apr_bucket_read(auth_bucket, &data, &len, APR_BLOCK_READ);

		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Auth Data len [%i]", len);

		if (rv != APR_SUCCESS) {
			child_stopped_reading = 1;
		}

		client_auth_packet = apr_pstrmemdup(p, data, len);
		input_read = 1;
	}

	// this brigade is useless
	apr_brigade_destroy(r_bb);

	if (input_read && client_auth_packet) {
		// now fill data structure for client data in driver MYSQL5
		if (mysql_client_data != NULL) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "Now decode MYSQL client auth packet");
			mysql_driver = (mysql_driver_details *)mysql_client_data->driver_details;
			if (mysql_driver != NULL) {
				uint8_t hash_stage1[20 +1];
/*
				uint8_t hash_stage1[20];
				uint8_t hash_stage2[20];
				uint8_t temp_token[20];
				uint8_t client_token[20];
				uint8_t check_auth[20];
				uint8_t final_hash[20];
*/

				// todo: insert constant values instead of numbers
				memcpy(mysql_driver->client_flags, client_auth_packet + 4, 4);
				mysql_driver->connect_with_db = SKYSQL_CAPABILITIES_CONNECT_WITH_DB & skysql_get_byte4(mysql_driver->client_flags);
				mysql_driver->compress = SKYSQL_CAPABILITIES_COMPRESS & skysql_get_byte4(mysql_driver->client_flags);
				mysql_client_data->username = apr_pstrndup(p, client_auth_packet + 4 + 4 + 4 + 1 + 23, 128);
				memcpy(&token_len, client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(mysql_client_data->username) + 1, 1);

				token = apr_pstrmemdup(p, client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(mysql_client_data->username) + 1 + 1, token_len);
 
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "[client TO gateway] current username is [%s], token is [%s] len %i", mysql_client_data->username, token, token_len);
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "[gateway TO client] server scramble was [%s], len %i", scramble, scramble_len);

/*

				skysql_sha1_str(mysql_client_data->username, strlen(mysql_client_data->username), hash_stage1);
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SHA1 di '%s' [%s]", mysql_client_data->username, hash_stage1);
				skysql_sha1_str(hash_stage1, 20, hash_stage2);
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SHA1 di SHA1('%s') [%s]", mysql_client_data->username, hash_stage2);

			
				skysql_sha1_2_str(scramble, scramble_len, hash_stage2, 20, temp_token);
				skysql_str_xor(check_auth, hash_stage1, temp_token, 20);
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "This is the client input?? [%s]", check_auth);

				memset(temp_token, '\0', sizeof(temp_token));
				memcpy(client_token, scramble, scramble_len);
				memcpy(client_token + scramble_len, hash_stage2, 20);
				skysql_sha1_str(client_token, scramble_len, temp_token);
			
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "This is the client input?? [%s]", temp_token);


				skysql_str_xor(check_auth, hash_stage2, hash_stage1, scramble_len);
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "XOR( client token, stage2_hash) [%s]", check_auth);
				skysql_sha1_str(check_auth, 20, final_hash);
				ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SHA1 di check_auth [%s]", final_hash);
*/
				// decode the token and check the password
				auth_ret = skysql_check_scramble(c, p, token, token_len, scramble, scramble_len, mysql_client_data->username, stage1_hash);

				if (auth_ret == 0) {
					ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SkySQL Gateway Authentication OK for [%s]", mysql_client_data->username);
				} else {
					ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "**** SkySQL Gateway Authentication ERROR [%s], retcode = [%i]", mysql_client_data->username, auth_ret);
				}

				if (mysql_driver->connect_with_db) {
					mysql_client_data->database = apr_pstrndup(p, client_auth_packet + 4 + 4 + 4 + 1 + 23 + strlen(mysql_client_data->username) + 1 + 1 + token_len, 128);
				}

			}
		}
	}

	return input_read;
}

apr_status_t gateway_send_error (conn_rec *c, apr_pool_t *p, uint8_t packet_number) {
        apr_status_t rv;
        rv = APR_SUCCESS;
        apr_bucket_brigade *bb;
        apr_bucket *b;
        uint8_t *outbuf = NULL;
        uint8_t skysql_payload_size = 0;
        uint8_t skysql_packet_header[4];
        uint8_t *skysql_payload = NULL;
        uint8_t field_count = 0;
        uint8_t affected_rows = 0;
        uint8_t insert_id = 0;
        uint8_t skysql_err[2];
        uint8_t skysql_statemsg[6];

        unsigned int skysql_errno = 0;
        const char *skysql_error_msg = NULL;
        const char *skysql_state = NULL;

        skysql_errno = 6969;
        skysql_error_msg = "Too many queries in one connection";
        skysql_state = "FA5D3";

        field_count = 0xff;
        skysql_set_byte2(skysql_err, skysql_errno);
        skysql_statemsg[0]='#';
        memcpy(skysql_statemsg+1, skysql_state, 5);

        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQL_Error: Errno [%u], ErrorMessage [%s], State [%s]", skysql_errno, skysql_error_msg, skysql_state);

        skysql_payload_size = sizeof(field_count) + sizeof(skysql_err) + sizeof(skysql_statemsg) + strlen(skysql_error_msg);

        // allocate memory for packet header + payload
        outbuf = (uint8_t *) apr_pcalloc(p, sizeof(skysql_packet_header) + skysql_payload_size);

        // write packet header with packet number
        skysql_set_byte3(skysql_packet_header, skysql_payload_size);
        skysql_packet_header[3] = packet_number;

        // write header
        memcpy(outbuf, skysql_packet_header, sizeof(skysql_packet_header));

        skysql_payload = outbuf + sizeof(skysql_packet_header);

        // write field
        memcpy(skysql_payload, &field_count, sizeof(field_count));
        skysql_payload = skysql_payload + sizeof(field_count);

        // write errno
        memcpy(skysql_payload, skysql_err, sizeof(skysql_err));
        skysql_payload = skysql_payload + sizeof(skysql_err);

        // write sqlstate
        memcpy(skysql_payload, skysql_statemsg, sizeof(skysql_statemsg));
        skysql_payload = skysql_payload + sizeof(skysql_statemsg);

        // write err messg
        memcpy(skysql_payload, skysql_error_msg, strlen(skysql_error_msg));

        // create brigade
        bb = apr_brigade_create(p, c->bucket_alloc);
        b = apr_bucket_pool_create(outbuf, sizeof(skysql_packet_header) + skysql_payload_size, p, c->bucket_alloc);
        APR_BRIGADE_INSERT_HEAD(bb, b);
        b = apr_bucket_flush_create(c->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(bb, b);

        return ap_pass_brigade(c->output_filters, bb);
}

apr_status_t skysql_send_error (conn_rec *c, uint8_t packet_number, MYSQL_conn *conn) {
        apr_status_t rv;
        rv = APR_SUCCESS;
        apr_bucket_brigade *bb;
        apr_bucket *b;
        uint8_t *outbuf = NULL;
        uint8_t skysql_payload_size = 0;
        uint8_t skysql_packet_header[4];
        uint8_t *skysql_payload = NULL;
        uint8_t field_count = 0;
        uint8_t affected_rows = 0;
        uint8_t insert_id = 0;
        uint8_t skysql_err[2];
        uint8_t skysql_statemsg[6];

        unsigned int skysql_errno = 0;
        const char *skysql_error_msg = NULL;
        const char *skysql_state = NULL;

        skysql_errno = mysql_errno(conn);
        skysql_error_msg = mysql_error(conn);
        skysql_state = mysql_sqlstate(conn);

        field_count = 0xff;
        skysql_set_byte2(skysql_err, skysql_errno);
        skysql_statemsg[0]='#';
        memcpy(skysql_statemsg+1, skysql_state, 5);

        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQL_Error: Errno [%u], ErrorMessage [%s], State [%s]", skysql_errno, skysql_error_msg, skysql_state);

        skysql_payload_size = sizeof(field_count) + sizeof(skysql_err) + sizeof(skysql_statemsg) + strlen(skysql_error_msg);

        // allocate memory for packet header + payload
        outbuf = (uint8_t *) apr_pcalloc(c->pool, sizeof(skysql_packet_header) + skysql_payload_size);

        // write packet header with packet number
        skysql_set_byte3(skysql_packet_header, skysql_payload_size);
        skysql_packet_header[3] = packet_number;

        // write header
        memcpy(outbuf, skysql_packet_header, sizeof(skysql_packet_header));

        skysql_payload = outbuf + sizeof(skysql_packet_header);

        // write field
        memcpy(skysql_payload, &field_count, sizeof(field_count));
        skysql_payload = skysql_payload + sizeof(field_count);

        // write errno
        memcpy(skysql_payload, skysql_err, sizeof(skysql_err));
        skysql_payload = skysql_payload + sizeof(skysql_err);

        // write sqlstate
        memcpy(skysql_payload, skysql_statemsg, sizeof(skysql_statemsg));
        skysql_payload = skysql_payload + sizeof(skysql_statemsg);

        // write err messg
        memcpy(skysql_payload, skysql_error_msg, strlen(skysql_error_msg));

        // create brigade
        bb = apr_brigade_create(c->pool, c->bucket_alloc);
        b = apr_bucket_pool_create(outbuf, sizeof(skysql_packet_header) + skysql_payload_size, c->pool, c->bucket_alloc);
        APR_BRIGADE_INSERT_HEAD(bb, b);
        b = apr_bucket_flush_create(c->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(bb, b);

        return ap_pass_brigade(c->output_filters, bb);
}

apr_status_t skysql_send_result(conn_rec *c, uint8_t *data, uint8_t len) {
        apr_status_t rv;
        rv = APR_SUCCESS;
        apr_bucket_brigade *bb;
        apr_bucket *b;

        // create brigade
        bb = apr_brigade_create(c->pool, c->bucket_alloc);

        // write
        apr_brigade_write(bb, ap_filter_flush, c->output_filters, data, len);

        //send & flush
        return ap_fflush(c->output_filters, bb);
}

apr_status_t skysql_send_eof(conn_rec *c, apr_pool_t *p, uint8_t packet_number) {
        apr_status_t rv;
        rv = APR_SUCCESS;
        apr_bucket_brigade *bb;
        apr_bucket *b;
        uint8_t *outbuf = NULL;
        uint8_t skysql_payload_size = 0;
        uint8_t skysql_packet_header[4];
        uint8_t *skysql_payload = NULL;
        uint8_t field_count = 0;
        uint8_t skysql_server_status[2];
        uint8_t skysql_warning_count[2];

        field_count = 0xfe;

        skysql_payload_size = sizeof(field_count) + sizeof(skysql_server_status) + sizeof(skysql_warning_count);

        // allocate memory for packet header + payload
        outbuf = (uint8_t *) apr_pcalloc(p, sizeof(skysql_packet_header) + skysql_payload_size);

        // write packet header with packet number
        skysql_set_byte3(skysql_packet_header, skysql_payload_size);
        skysql_packet_header[3] = packet_number;

        // write header
        memcpy(outbuf, skysql_packet_header, sizeof(skysql_packet_header));

        skysql_payload = outbuf + sizeof(skysql_packet_header);

        skysql_server_status[0] = 2;
        skysql_server_status[1] = 0;
        skysql_warning_count[0] = 0;
        skysql_warning_count[1] = 0;

        // write data
        memcpy(skysql_payload, &field_count, sizeof(field_count));
        skysql_payload = skysql_payload + sizeof(field_count);

        memcpy(skysql_payload, skysql_server_status, sizeof(skysql_server_status));
        skysql_payload = skysql_payload + sizeof(skysql_server_status);

        memcpy(skysql_payload, skysql_warning_count, sizeof(skysql_warning_count));
        skysql_payload = skysql_payload + sizeof(skysql_warning_count);

        // create brigade
        bb = apr_brigade_create(p, c->bucket_alloc);
        // write
        apr_brigade_write(bb, ap_filter_flush, c->output_filters, outbuf, sizeof(skysql_packet_header) + skysql_payload_size);
        //send & flush
        return ap_fflush(c->output_filters, bb);
}

apr_status_t skysql_send_ok(conn_rec *c, apr_pool_t *p, uint8_t packet_number, uint8_t in_affected_rows, const char* skysql_message) {
        apr_status_t rv;
        rv = APR_SUCCESS;
        apr_bucket_brigade *bb;
        apr_bucket *b;
        uint8_t *outbuf = NULL;
        uint8_t skysql_payload_size = 0;
        uint8_t skysql_packet_header[4];
        uint8_t *skysql_payload = NULL;
        uint8_t field_count = 0;
        uint8_t affected_rows = 0;
        uint8_t insert_id = 0;
        uint8_t skysql_server_status[2];
        uint8_t skysql_warning_count[2];

        affected_rows = in_affected_rows;

        skysql_payload_size = sizeof(field_count) + sizeof(affected_rows) + sizeof(insert_id) + sizeof(skysql_server_status) + sizeof(skysql_warning_count);

        if (skysql_message != NULL) {
                skysql_payload_size += strlen(skysql_message);
        }

        // allocate memory for packet header + payload
        outbuf = (uint8_t *) apr_pcalloc(p, sizeof(skysql_packet_header) + skysql_payload_size);

        // write packet header with packet number
        skysql_set_byte3(skysql_packet_header, skysql_payload_size);
        skysql_packet_header[3] = packet_number;

        // write header
        memcpy(outbuf, skysql_packet_header, sizeof(skysql_packet_header));

        skysql_payload = outbuf + sizeof(skysql_packet_header);

        skysql_server_status[0] = 2;
        skysql_server_status[1] = 0;
        skysql_warning_count[0] = 0;
        skysql_warning_count[1] = 0;

        // write data
        memcpy(skysql_payload, &field_count, sizeof(field_count));
        skysql_payload = skysql_payload + sizeof(field_count);

        memcpy(skysql_payload, &affected_rows, sizeof(affected_rows));
        skysql_payload = skysql_payload + sizeof(affected_rows);

        memcpy(skysql_payload, &insert_id, sizeof(insert_id));
        skysql_payload = skysql_payload + sizeof(insert_id);

        memcpy(skysql_payload, skysql_server_status, sizeof(skysql_server_status));
        skysql_payload = skysql_payload + sizeof(skysql_server_status);

        memcpy(skysql_payload, skysql_warning_count, sizeof(skysql_warning_count));
        skysql_payload = skysql_payload + sizeof(skysql_warning_count);

        if (skysql_message != NULL) {
                memcpy(skysql_payload, skysql_message, strlen(skysql_message));
        }


        // create brigade
        bb = apr_brigade_create(p, c->bucket_alloc);

/*
        b = apr_bucket_pool_create(outbuf, sizeof(skysql_packet_header) + skysql_payload_size, c->pool, c->bucket_alloc);
        APR_BRIGADE_INSERT_HEAD(bb, b);
        b = apr_bucket_flush_create(c->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(bb, b);

        return ap_pass_brigade(c->output_filters, bb);
*/
        apr_brigade_write(bb, ap_filter_flush, c->output_filters, outbuf, sizeof(skysql_packet_header) + skysql_payload_size);
        ap_fflush(c->output_filters, bb);

	apr_brigade_destroy(bb);

	return 1;
}

///////////////////////////
// scramble is 20 bytes and must be pre allocated
apr_status_t skysql_send_handshake(conn_rec *c, uint8_t *scramble, int *scramble_len) {
        apr_status_t rv;
        rv = APR_SUCCESS;
        apr_bucket_brigade *bb;
        apr_bucket *b;
	apr_pool_t *p = c->pool;

        uint8_t *outbuf = NULL;
        uint8_t skysql_payload_size = 0;
        uint8_t skysql_packet_header[4];
        uint8_t skysql_packet_id = 0;
        uint8_t skysql_filler = SKYSQL_HANDSKAKE_FILLER;
        uint8_t skysql_protocol_version = SKYSQL_PROTOCOL_VERSION;
        uint8_t *skysql_handshake_payload = NULL;
        uint8_t skysql_thread_id[4];
        uint8_t skysql_scramble_buf[9] = "";
        uint8_t skysql_plugin_data[13] = "";
        uint8_t skysql_server_capabilities_one[2];
        uint8_t skysql_server_capabilities_two[2];
        uint8_t skysql_server_language = 8;
        uint8_t skysql_server_status[2];
        uint8_t skysql_scramble_len = 21;
        uint8_t skysql_filler_ten[10];
        uint8_t skysql_last_byte = 0x00;

	uint8_t scramble_buffer[20]="";

	skysql_set_random_str(scramble_buffer, 20);

	// set len to the caller
	memset(scramble_len, 20, 1);

	// copy back to the caller
	memcpy(scramble, scramble_buffer, 20);

        memset(&skysql_filler_ten, 0x00, sizeof(skysql_filler_ten));

        // thread id, now put the apache child PID, then a conversion map in memory is needed!
        skysql_set_byte4(skysql_thread_id, getpid());

        memcpy(skysql_scramble_buf, scramble_buffer, 8);
	
        memcpy(skysql_plugin_data, scramble_buffer + 8, 12);

        skysql_payload_size  = sizeof(skysql_protocol_version) + (strlen(SKYSQL_VERSION) + 1) + sizeof(skysql_thread_id) + 8 + sizeof(skysql_filler) + sizeof(skysql_server_capabilities_one) + sizeof(skysql_server_language) + sizeof(skysql_server_status) + sizeof(skysql_server_capabilities_two) + sizeof(skysql_scramble_len) + sizeof(skysql_filler_ten) + 12 + sizeof(skysql_last_byte) + strlen("mysql_native_password") + sizeof(skysql_last_byte);

        // allocate memory for packet header + payload
        outbuf = (uint8_t *) apr_pcalloc(p, sizeof(skysql_packet_header) + skysql_payload_size);

        // write packet heder with skysql_payload_size
        skysql_set_byte3(skysql_packet_header, skysql_payload_size);
        //skysql_packet_header[0] = skysql_payload_size;

        // write packent number, now is 0
        skysql_packet_header[3]= skysql_packet_id;
        memcpy(outbuf, skysql_packet_header, sizeof(skysql_packet_header));

        // current buffer pointer
        skysql_handshake_payload = outbuf + sizeof(skysql_packet_header);

        // write protocol version
        memcpy(skysql_handshake_payload, &skysql_protocol_version, sizeof(skysql_protocol_version));
        skysql_handshake_payload = skysql_handshake_payload + sizeof(skysql_protocol_version);

        // write server version plus 0 filler
        strcpy(skysql_handshake_payload, SKYSQL_VERSION);
        skysql_handshake_payload = skysql_handshake_payload + strlen(SKYSQL_VERSION);
        *skysql_handshake_payload = 0x00;
        skysql_handshake_payload++;

        // write thread id
        memcpy(skysql_handshake_payload, skysql_thread_id, sizeof(skysql_thread_id));
        skysql_handshake_payload = skysql_handshake_payload + sizeof(skysql_thread_id);

        // write scramble buf
        memcpy(skysql_handshake_payload, skysql_scramble_buf, 8);
        skysql_handshake_payload = skysql_handshake_payload + 8;
        *skysql_handshake_payload =  SKYSQL_HANDSKAKE_FILLER;
        skysql_handshake_payload++;

        // write server capabilities part one
        skysql_server_capabilities_one[0] = SKYSQL_SERVER_CAPABILITIES_BYTE1;
        skysql_server_capabilities_one[1] = SKYSQL_SERVER_CAPABILITIES_BYTE2;

        memcpy(skysql_handshake_payload, skysql_server_capabilities_one, sizeof(skysql_server_capabilities_one));
        skysql_handshake_payload = skysql_handshake_payload + sizeof(skysql_server_capabilities_one);

        // write server language
        memcpy(skysql_handshake_payload, &skysql_server_language, sizeof(skysql_server_language));
        skysql_handshake_payload = skysql_handshake_payload + sizeof(skysql_server_language);

        //write server status
        skysql_server_status[0] = 2;
        skysql_server_status[1] = 0;
        memcpy(skysql_handshake_payload, skysql_server_status, sizeof(skysql_server_status));
        skysql_handshake_payload = skysql_handshake_payload + sizeof(skysql_server_status);

        //write server capabilities part two
        skysql_server_capabilities_two[0] = 15;
        skysql_server_capabilities_two[1] = 128;

	//skysql_server_capabilities_two[0] & SKYSQL_CAPABILITIES_COMPRESS;

        memcpy(skysql_handshake_payload, skysql_server_capabilities_two, sizeof(skysql_server_capabilities_two));
        skysql_handshake_payload = skysql_handshake_payload + sizeof(skysql_server_capabilities_two);

        // write scramble_len
        memcpy(skysql_handshake_payload, &skysql_scramble_len, sizeof(skysql_scramble_len));
        skysql_handshake_payload = skysql_handshake_payload + sizeof(skysql_scramble_len);

        //write 10 filler
        memcpy(skysql_handshake_payload, skysql_filler_ten, sizeof(skysql_filler_ten));
        skysql_handshake_payload = skysql_handshake_payload + sizeof(skysql_filler_ten);

        // write plugin data
        memcpy(skysql_handshake_payload, skysql_plugin_data, 12);
        skysql_handshake_payload = skysql_handshake_payload + 12;

        //write last byte, 0
        *skysql_handshake_payload = 0x00;
        skysql_handshake_payload++;

        // to be understanded ????
        memcpy(skysql_handshake_payload, "mysql_native_password", strlen("mysql_native_password"));
        skysql_handshake_payload = skysql_handshake_payload + strlen("mysql_native_password");

        //write last byte, 0
        *skysql_handshake_payload = 0x00;
        skysql_handshake_payload++;



        // create brigade
        bb = apr_brigade_create(p, c->bucket_alloc);
/*
        b = apr_bucket_pool_create(outbuf, sizeof(skysql_packet_header) + skysql_payload_size, p, c->bucket_alloc);
        APR_BRIGADE_INSERT_HEAD(bb, b);
        b = apr_bucket_flush_create(c->bucket_alloc);
        APR_BRIGADE_INSERT_TAIL(bb, b);

        ap_pass_brigade(c->output_filters, bb);
	apr_brigade_destroy(bb);
*/
        apr_brigade_write(bb, ap_filter_flush, c->output_filters, outbuf, sizeof(skysql_packet_header) + skysql_payload_size);
        ap_fflush(c->output_filters, bb);

        apr_brigade_destroy(bb);

        return 1;
}

int skygateway_query_result(conn_rec *c, apr_pool_t *p, MYSQL_conn *conn, const char *query) {
	int query_ret = 0;
	int num_fields = 0;
	int return_data = 0;
	uint8_t result_column_count = 0;
	uint8_t header_result_packet[4];
	apr_bucket_brigade *bb1;
	apr_bucket *b1;
	uint8_t *outbuf = NULL;
        apr_status_t rv;
        uint8_t buffer[MAX_CHUNK];
        unsigned long bytes = MAX_CHUNK;
        unsigned long tot_bytes = 0;
	int cycle=0;
	apr_pollset_t *pset;
	apr_pollfd_t pfd;
	apr_int32_t nsocks=1;
	apr_status_t poll_rv;
	int is_eof = 0;

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW is sending query to backend [%lu] ...", conn->tid);

	// send the query to the backend
	query_ret = mysql_query(conn, query);

	if (query_ret) {
		// send error, packet #1
		skysql_send_error(c, 1, conn);

		return 1;
	}

	//poll_rv = apr_pollset_create(&pset, 1, p, 0);

      	//pfd.p = p;
       	//pfd.desc_type = APR_POLL_SOCKET;
        //pfd.reqevents = APR_POLLIN;
        //pfd.rtnevents = APR_POLLIN;
        //pfd.desc.s = conn->socket;
        //pfd.client_data = NULL;

	//rv = apr_pollset_add(pset, &pfd);

	//rv = apr_socket_opt_set(conn->socket, APR_SO_NONBLOCK , 1);

	apr_socket_timeout_set(conn->socket, 100000000);

	// read query resut from backend
	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW is receiving query result from backend ...");

	while(1) {
		char errmesg_p[1000]="";
		bytes=MAX_CHUNK;

		memset(buffer, '\0', MAX_CHUNK);

		//rv = apr_wait_for_io_or_timeout(NULL, conn->socket, 1);	
		//fprintf(stderr, "wait socket recv %lu\n", bytes);
		//apr_strerror(rv, errmesg_p, sizeof(errmesg_p));
		//fprintf(stderr, "wait Errore in recv, rv is %i, [%s]\n", rv, errmesg_p);
		//fflush(stderr);

		//apr_socket_atreadeof(conn->socket, &is_eof);

		rv = apr_socket_recv(conn->socket, buffer, &bytes);
	
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW received %lu bytes", bytes);

		if (rv) {
                	if (APR_STATUS_IS_EAGAIN(rv)) {	
				continue;
			}
		}

		tot_bytes += bytes;

		if (rv != APR_SUCCESS && rv != APR_EOF && rv != APR_EAGAIN) {
			char errmesg[1000]="";
			apr_strerror(rv, errmesg, sizeof(errmesg));

			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive error %i, [%s]", rv, errmesg);

			return 1;
		}

		if (rv == APR_EOF && bytes == 0) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive error: EOF");
		}

		bb1 = apr_brigade_create(p, c->bucket_alloc);

		apr_brigade_write(bb1, ap_filter_flush, c->output_filters, buffer, bytes);
		ap_fflush(c->output_filters, bb1);

		apr_brigade_destroy(bb1);

		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive, brigade sent to the client with %li bytes", bytes);

		cycle++;


		if (bytes < MAX_CHUNK) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: less bytes than buffer here, Query Result: total bytes %lu in %i", tot_bytes, cycle);

			return 0;
		}

		if (bytes ==  MAX_CHUNK) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: ALL bytes in the buffer here, continue");
		}

	}

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: Return from query result: total bytes %lu in %i", tot_bytes, cycle);

	return 0;
}

int skygateway_statement_prepare_result(conn_rec *c, apr_pool_t *p, MYSQL_conn *conn, const char *query, int len) {
	int query_ret = 0;
	int num_fields = 0;
	int return_data = 0;
	uint8_t result_column_count = 0;
	uint8_t header_result_packet[4];
	apr_bucket_brigade *bb1;
	apr_bucket *b1;
	uint8_t *outbuf = NULL;
        apr_status_t rv;
        uint8_t buffer[MAX_CHUNK];
        unsigned long bytes = MAX_CHUNK;
        unsigned long tot_bytes = 0;
	int cycle=0;
	apr_pollset_t *pset;
	apr_pollfd_t pfd;
	apr_int32_t nsocks=1;
	apr_status_t poll_rv;
	int is_eof = 0;

	query_ret = mysql_send_command(conn, query, 0x16, len);

	if (query_ret) {
		// send error, packet #1
		skysql_send_error(c, 1, conn);

		return 1;
	}

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW is sending result set ...");

	poll_rv = apr_pollset_create(&pset, 1, p, 0);

        pfd.p = p;
        pfd.desc_type = APR_POLL_SOCKET;
        pfd.reqevents = APR_POLLIN;
        pfd.rtnevents = APR_POLLIN;
        pfd.desc.s = conn->socket;
        pfd.client_data = NULL;

	//rv = apr_pollset_add(pset, &pfd);

	//rv = apr_socket_opt_set(conn->socket, APR_SO_NONBLOCK , 1);

	apr_socket_timeout_set(conn->socket, 100000000);

	while(1) {
		char errmesg_p[1000]="";
		bytes=MAX_CHUNK;

		memset(buffer, '\0', MAX_CHUNK);

		//rv = apr_wait_for_io_or_timeout(NULL, conn->socket, 1);	
		//fprintf(stderr, "wait socket recv %lu\n", bytes);
		//apr_strerror(rv, errmesg_p, sizeof(errmesg_p));
		//fprintf(stderr, "wait Errore in recv, rv is %i, [%s]\n", rv, errmesg_p);
		//fflush(stderr);

		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW is receiving ...");

		//apr_socket_atreadeof(conn->socket, &is_eof);

		rv = apr_socket_recv(conn->socket, buffer, &bytes);
	
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW received %lu bytes", bytes);

		if (rv) {
                	if (APR_STATUS_IS_EAGAIN(rv)) {	
				continue;
			}
		}

		tot_bytes += bytes;

		if (rv != APR_SUCCESS && rv != APR_EOF && rv != APR_EAGAIN) {
			char errmesg[1000]="";
			apr_strerror(rv, errmesg, sizeof(errmesg));

			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive error %i, [%s]", rv, errmesg);

			return 1;
		}

		if (rv == APR_EOF && bytes == 0) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive error: EOF");
		}

		bb1 = apr_brigade_create(p, c->bucket_alloc);

		apr_brigade_write(bb1, ap_filter_flush, c->output_filters, buffer, bytes);
		ap_fflush(c->output_filters, bb1);

		apr_brigade_destroy(bb1);

		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive, brigade sent with %li bytes", bytes);

		cycle++;


		if (bytes < MAX_CHUNK) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: less bytes than buffer here, Return from query result: total bytes %lu in %i", tot_bytes, cycle);

			return 0;
		}

		if (bytes ==  MAX_CHUNK) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: ALL bytes in the buffer here, continue");
		}

	}

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: Return from query result: total bytes %lu in %i", tot_bytes, cycle);

	return 0;
}

int skygateway_statement_execute_result(conn_rec *c, apr_pool_t *p, MYSQL_conn *conn, const char *query, int len) {
	int query_ret = 0;
	int num_fields = 0;
	int return_data = 0;
	uint8_t result_column_count = 0;
	uint8_t header_result_packet[4];
	apr_bucket_brigade *bb1;
	apr_bucket *b1;
	uint8_t *outbuf = NULL;
        apr_status_t rv;
        uint8_t buffer[MAX_CHUNK];
        unsigned long bytes = MAX_CHUNK;
        unsigned long tot_bytes = 0;
	int cycle=0;
	apr_pollset_t *pset;
	apr_pollfd_t pfd;
	apr_int32_t nsocks=1;
	apr_status_t poll_rv;
	int is_eof = 0;

	query_ret = mysql_send_command(conn, query, 0x17, len);

	if (query_ret) {
		// send error, packet #1
		skysql_send_error(c, 1, conn);

		return 1;
	}

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW is sending result set ...");

	poll_rv = apr_pollset_create(&pset, 1, p, 0);

        pfd.p = p;
        pfd.desc_type = APR_POLL_SOCKET;
        pfd.reqevents = APR_POLLIN;
        pfd.rtnevents = APR_POLLIN;
        pfd.desc.s = conn->socket;
        pfd.client_data = NULL;

	//rv = apr_pollset_add(pset, &pfd);

	//rv = apr_socket_opt_set(conn->socket, APR_SO_NONBLOCK , 1);

	apr_socket_timeout_set(conn->socket, 100000000);

	while(1) {
		char errmesg_p[1000]="";
		bytes=MAX_CHUNK;

		memset(buffer, '\0', MAX_CHUNK);

		//rv = apr_wait_for_io_or_timeout(NULL, conn->socket, 1);	
		//fprintf(stderr, "wait socket recv %lu\n", bytes);
		//apr_strerror(rv, errmesg_p, sizeof(errmesg_p));
		//fprintf(stderr, "wait Errore in recv, rv is %i, [%s]\n", rv, errmesg_p);
		//fflush(stderr);

		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW is receiving ...");

		//apr_socket_atreadeof(conn->socket, &is_eof);

		rv = apr_socket_recv(conn->socket, buffer, &bytes);
	
		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW received %lu bytes", bytes);

		if (rv) {
                	if (APR_STATUS_IS_EAGAIN(rv)) {	
				continue;
			}
		}

		tot_bytes += bytes;

		if (rv != APR_SUCCESS && rv != APR_EOF && rv != APR_EAGAIN) {
			char errmesg[1000]="";
			apr_strerror(rv, errmesg, sizeof(errmesg));

			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive error %i, [%s]", rv, errmesg);

			return 1;
		}

		if (rv == APR_EOF && bytes == 0) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive error: EOF");
		}

		bb1 = apr_brigade_create(p, c->bucket_alloc);

		apr_brigade_write(bb1, ap_filter_flush, c->output_filters, buffer, bytes);
		ap_fflush(c->output_filters, bb1);

		apr_brigade_destroy(bb1);

		ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive, brigade sent with %li bytes", bytes);

		cycle++;


		if (bytes < MAX_CHUNK) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: less bytes than buffer here, Return from query result: total bytes %lu in %i", tot_bytes, cycle);

			return 0;
		}

		if (bytes ==  MAX_CHUNK) {
			ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: ALL bytes in the buffer here, continue");
		}

	}

	ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: Return from query result: total bytes %lu in %i", tot_bytes, cycle);

	return 0;
}

int mysql_send_command(MYSQL_conn *conn, const char *command, int cmd, int len) {
        apr_status_t rv;
        //uint8_t *packet_buffer=NULL;
        uint8_t packet_buffer[SMALL_CHUNK];
        long bytes;
        int fd;

        //packet_buffer = (uint8_t *) calloc(1, 5 + len + 1);
        memset(&packet_buffer, '\0', sizeof(packet_buffer));

        packet_buffer[4]= cmd;
        memcpy(packet_buffer+5, command, len);

        skysql_set_byte3(packet_buffer, 1 + len);

        bytes = 4 + 1 + len;

#ifdef MYSQL_CONN_DEBUG
        fprintf(stderr, "THE COMMAND is [%s] len %i\n", command, bytes);
        fprintf(stderr, "THE COMMAND TID is [%lu]", conn->tid);
        fprintf(stderr, "THE COMMAND scramble is [%s]", conn->scramble);
        if (conn->socket == NULL) {
                fprintf(stderr, "***** THE COMMAND sock struct is NULL\n");
        }
        fwrite(packet_buffer, bytes, 1, stderr);
        fflush(stderr);
#endif
        apr_os_sock_get(&fd,conn->socket);

#ifdef MYSQL_CONN_DEBUG
        fprintf(stderr, "COMMAND Socket FD is %i\n", fd);
        fflush(stderr);
#endif

        rv = apr_socket_send(conn->socket, packet_buffer, &bytes);

#ifdef MYSQL_CONN_DEBUG
        fprintf(stderr, "COMMAND SENT [%x] [%s]\n", cmd, command);
        fflush(stderr);
#endif

        if (rv != APR_SUCCESS) {
                return 1;
        }

        return 0;
}


int mysql_pass_packet(MYSQL_conn *conn, const char *command, int len) {
        apr_status_t rv;
        //uint8_t *packet_buffer=NULL;
        uint8_t packet_buffer[SMALL_CHUNK];
        long bytes;
        int fd;

        //packet_buffer = (uint8_t *) calloc(1, 5 + len + 1);
        memset(&packet_buffer, '\0', sizeof(packet_buffer));

        memcpy(packet_buffer, command, len);

        bytes = len;

#ifdef MYSQL_CONN_DEBUG
        fprintf(stderr, "THE COMMAND is [%s] len %i\n", command, bytes);
        fprintf(stderr, "THE COMMAND TID is [%lu]", conn->tid);
        fprintf(stderr, "THE COMMAND scramble is [%s]", conn->scramble);
        if (conn->socket == NULL) {
                fprintf(stderr, "***** THE COMMAND sock struct is NULL\n");
        }
        fwrite(packet_buffer, bytes, 1, stderr);
        fflush(stderr);
#endif
        apr_os_sock_get(&fd,conn->socket);

#ifdef MYSQL_CONN_DEBUG
        fprintf(stderr, "PACKET Socket FD is %i\n", fd);
        fflush(stderr);
#endif

        rv = apr_socket_send(conn->socket, packet_buffer, &bytes);

#ifdef MYSQL_CONN_DEBUG
        fprintf(stderr, "PACKET SENT [%s]\n", command);
        fflush(stderr);
#endif

        if (rv != APR_SUCCESS) {
                return 1;
        }

        return 0;
}

int mysql_receive_packet(conn_rec *c, apr_pool_t *p, MYSQL_conn *conn) {
        apr_bucket_brigade *bb1;
        apr_bucket *b1;
        apr_status_t rv;
        uint8_t buffer[MAX_CHUNK];
        unsigned long bytes = MAX_CHUNK;
        unsigned long tot_bytes = 0;
        int cycle=0;
        int is_eof = 0;
	apr_socket_timeout_set(conn->socket, 100000000);

        while(1) {
                char errmesg_p[1000]="";
                bytes=MAX_CHUNK;

                memset(buffer, '\0', MAX_CHUNK);

                ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW is receiving ...");

                rv = apr_socket_recv(conn->socket, buffer, &bytes);

                ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW received %lu bytes", bytes);

                if (rv) {
                        if (APR_STATUS_IS_EAGAIN(rv)) {
                                continue;
                        }
                }

                tot_bytes += bytes;

                if (rv != APR_SUCCESS && rv != APR_EOF && rv != APR_EAGAIN) {
                        char errmesg[1000]="";
                        apr_strerror(rv, errmesg, sizeof(errmesg));

                        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive error %i, [%s]", rv, errmesg);

                        return 1;
                }

                if (rv == APR_EOF && bytes == 0) {
                        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive error: EOF");
                }

                bb1 = apr_brigade_create(p, c->bucket_alloc);

                apr_brigade_write(bb1, ap_filter_flush, c->output_filters, buffer, bytes);
                ap_fflush(c->output_filters, bb1);

                apr_brigade_destroy(bb1);

                ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive, brigade sent with %li bytes", bytes);

                cycle++;

                if (bytes < MAX_CHUNK) {
                        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: less bytes than buffer here, Return from query result: total bytes %lu in %i", tot_bytes, cycle);

                        return 0;
                }

                if (bytes ==  MAX_CHUNK) {
                        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: ALL bytes in the buffer here, continue");
                }

        }

        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, c->base_server, "SKYSQLGW receive: Return from query result: total bytes %lu in %i", tot_bytes, cycle);

        return 0;
}


backend_list select_backend_servers() {
	backend_list l;

	memset(&l, '\0', sizeof(backend_list));

	l.num = 2;
	l.list = "127.0.0.1:3307,127.0.0.1:3306,xxxx:11";
	
	return l;
}
