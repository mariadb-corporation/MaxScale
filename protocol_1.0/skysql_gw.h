////////////////////////////////////////
// SKYSQL header file
// By Massimiliano Pinto 2012/2013
// SkySQL AB
////////////////////////////////////////

#include "ap_config.h"
#include "ap_mmn.h"
#include "httpd.h"
#include "http_core.h"
#include "http_main.h"
#include "http_config.h"
#include "http_connection.h"
#include "http_request.h"
#include "http_log.h"
#include "http_protocol.h"
#include "ap_config_auto.h"
#include "http_connection.h"

#include "util_filter.h"
#include "util_script.h"
#include "apr.h"
#include "apr_general.h"
#include "apr_buckets.h"
#include "apr_optional.h"
#include "apr_strings.h"
#include "apr_tables.h"
#include "apr_lib.h"
#include "apr_fnmatch.h"
#include "apr_strings.h"
#include "apr_dbm.h"
#include "apr_rmm.h"
#include "apr_shm.h"
#include "apr_global_mutex.h"
#include "apr_time.h"
#include "scoreboard.h"

// getpid
#include <unistd.h>

// mapped I/O
#include <sys/mman.h>

#include "skysql_client.h"

#define SKYSQL_GATEWAY_VERSION "0.0.1"
#define SKYSQL_VERSION "5.5.22-SKY-1.6.5"

#define SKYSQL_READ 0
#define SKYSQL_WRITE 1

#define HTTP_WELCOME_MESSAGE "HTTP/1.1 200 OK\r\nConnection: Keep-Alive\r\nContent-Type: text/plain\r\n\r\nSKYSQL Gateway " SKYSQL_GATEWAY_VERSION

#define SKYSQL_LISTENER_VERSION "MySQL Community Server (GPL)"
#define SKYSQL_PROTOCOL_VERSION 10 // version is 10
#define SKYSQL_THREAD_ID 11
#define SKYSQL_HANDSKAKE_FILLER 0x00
#define SKYSQL_SERVER_CAPABILITIES_BYTE1 0xff
#define SKYSQL_SERVER_CAPABILITIES_BYTE2 0xf7
#define SKYSQL_SERVER_LANGUAGE 0x08

module AP_MODULE_DECLARE_DATA skysql_module;

static unsigned char *config_area=NULL;

//const int SKY_SQL_MAX_PACKET_LEN = 0xffffffL;

typedef struct {
        MYSQL_conn *conn;
	unsigned long mysql_tid;
	unsigned long gateway_id;
        int protocol_enabled;
	int pool_enabled;
	char backend_servers[2][128];
	char *server_list;
	apr_hash_t *resources;
	int loop_timeout;
} skysql_server_conf;

typedef struct
{
        char *name;
        char *raw_config;
        char *server_list;
        int r_port;
        char *dbname;
        char *defaults;
        int nshards;
} conn_details;

typedef struct {
	char *driver_name;
	char *username;
	char *password;
	char *database;
	void *driver_details;
} skysql_client_auth;

typedef struct {
	uint8_t client_flags[4];
	uint8_t max_packet_size[4];
	uint8_t charset;
	uint8_t scramble_buff;
	int connect_with_db;
	int compress;
} mysql_driver_details;

typedef struct {
        int num;
        char *list;
} backend_list;

int skysql_ext_file_ver();
int skysql_query_is_select(const char *query);
apr_status_t skysql_read_client_autentication(conn_rec *c, apr_pool_t *pool, uint8_t *scramble, int scramble_len, skysql_client_auth *mysql_client_data, uint8_t *stage1_hash);
apr_status_t skysql_send_handshake(conn_rec *c, uint8_t *scramble, int *scramble_len);
apr_status_t skysql_send_error (conn_rec *c, uint8_t packet_number, MYSQL_conn *conn);
//apr_status_t skysql_prepare_ok(conn_rec *c, uint8_t packet_number, MYSQL_STMT *statement, MYSQL_conn *conn);
apr_status_t skysql_send_ok(conn_rec *c, apr_pool_t *p, uint8_t packet_number, uint8_t in_affected_rows, const char* skysql_message);
apr_status_t skysql_send_eof(conn_rec *c, apr_pool_t *p, uint8_t packet_number);
apr_status_t skysql_send_result(conn_rec *c, uint8_t *data, uint8_t len);
int select_random_slave_server(const char *server_listi, int *num_slaves);
apr_status_t gateway_send_error (conn_rec *c, apr_pool_t *p, uint8_t packet_number);
apr_status_t gateway_reply_data(conn_rec *c, apr_pool_t *pool, void *data, int len);
char *gateway_find_user_password_sha1(char *username, void *repository, conn_rec *c, apr_pool_t *p);
void skysql_sha1_str(const uint8_t *in, int in_len, uint8_t *out);
int skygateway_query_result(conn_rec *c, apr_pool_t *p, MYSQL_conn *conn, const char *query);
char *bin2hex(char *out, const uint8_t *in, unsigned int len); 
void skysql_sha1_2_str(const uint8_t *in, int in_len, const uint8_t *in2, int in2_len, uint8_t *out);
void skysql_str_xor(char *output, const uint8_t *input1, const uint8_t *input2, unsigned int len);
int get_server_from_list(char **selected_host, int *selected_port, char *server_list, int num, apr_pool_t *p);
int get_master_from_list(char **selected_host, int *selected_port, char *server_list, apr_pool_t *p);
int mysql_pass_packet(MYSQL_conn *conn, const char *command, int len);
int mysql_receive_packet(conn_rec *c, apr_pool_t *p, MYSQL_conn *conn);
int skygateway_statement_prepare_result(conn_rec *c, apr_pool_t *p, MYSQL_conn *conn, const char *query, int len);
int skygateway_statement_execute_result(conn_rec *c, apr_pool_t *p, MYSQL_conn *conn, const char *query, int len);
int mysql_send_command(MYSQL_conn *conn, const char *command, int cmd, int len);
apr_status_t skysql_change_user(conn_rec *c, apr_pool_t *p, char *username, char *database, MYSQL_conn *conn, uint8_t *stage1_hash); 
int query_routing(const char *server_list, const char *sql_command, int procotol_command, int current_slave);
unsigned int mysql_errno(MYSQL_conn *mysql);
const char *mysql_error(MYSQL_conn *mysql);
const char *mysql_sqlstate(MYSQL_conn *mysql);
int mysql_query(MYSQL_conn *conn, const char *query);
