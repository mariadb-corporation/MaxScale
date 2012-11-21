////////////////////////////////////////
// SKYSQL header file
// By Massimiliano Pinto 2012
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

// sha1
#include "apr_sha1.h"

// getpid
#include <unistd.h>


/* Protocol packing macros. */
#define skysql_set_byte2(__buffer, __int) do { \
  (__buffer)[0]= (uint8_t)((__int) & 0xFF); \
  (__buffer)[1]= (uint8_t)(((__int) >> 8) & 0xFF); } while (0)
#define skysql_set_byte3(__buffer, __int) do { \
  (__buffer)[0]= (uint8_t)((__int) & 0xFF); \
  (__buffer)[1]= (uint8_t)(((__int) >> 8) & 0xFF); \
  (__buffer)[2]= (uint8_t)(((__int) >> 16) & 0xFF); } while (0)
#define skysql_set_byte4(__buffer, __int) do { \
  (__buffer)[0]= (uint8_t)((__int) & 0xFF); \
  (__buffer)[1]= (uint8_t)(((__int) >> 8) & 0xFF); \
  (__buffer)[2]= (uint8_t)(((__int) >> 16) & 0xFF); \
  (__buffer)[3]= (uint8_t)(((__int) >> 24) & 0xFF); } while (0)


/* Protocol unpacking macros. */
#define skysql_get_byte2(__buffer) \
  (uint16_t)((__buffer)[0] | \
            ((__buffer)[1] << 8))
#define skysql_get_byte3(__buffer) \
  (uint32_t)((__buffer)[0] | \
            ((__buffer)[1] << 8) | \
            ((__buffer)[2] << 16))
#define skysql_get_byte4(__buffer) \
  (uint32_t)((__buffer)[0] | \
            ((__buffer)[1] << 8) | \
            ((__buffer)[2] << 16) | \
            ((__buffer)[3] << 24))
#define skysql_get_byte8(__buffer) \
  ((uint64_t)(__buffer)[0] | \
  ((uint64_t)(__buffer)[1] << 8) | \
  ((uint64_t)(__buffer)[2] << 16) | \
  ((uint64_t)(__buffer)[3] << 24) | \
  ((uint64_t)(__buffer)[4] << 32) | \
  ((uint64_t)(__buffer)[5] << 40) | \
  ((uint64_t)(__buffer)[6] << 48) | \
  ((uint64_t)(__buffer)[7] << 56))

typedef enum
{
  SKYSQL_CAPABILITIES_NONE=                   0,
  SKYSQL_CAPABILITIES_LONG_PASSWORD=          (1 << 0),
  SKYSQL_CAPABILITIES_FOUND_ROWS=             (1 << 1),
  SKYSQL_CAPABILITIES_LONG_FLAG=              (1 << 2),
  SKYSQL_CAPABILITIES_CONNECT_WITH_DB=        (1 << 3),
  SKYSQL_CAPABILITIES_NO_SCHEMA=              (1 << 4),
  SKYSQL_CAPABILITIES_COMPRESS=               (1 << 5),
  SKYSQL_CAPABILITIES_ODBC=                   (1 << 6),
  SKYSQL_CAPABILITIES_LOCAL_FILES=            (1 << 7),
  SKYSQL_CAPABILITIES_IGNORE_SPACE=           (1 << 8),
  SKYSQL_CAPABILITIES_PROTOCOL_41=            (1 << 9),
  SKYSQL_CAPABILITIES_INTERACTIVE=            (1 << 10),
  SKYSQL_CAPABILITIES_SSL=                    (1 << 11),
  SKYSQL_CAPABILITIES_IGNORE_SIGPIPE=         (1 << 12),
  SKYSQL_CAPABILITIES_TRANSACTIONS=           (1 << 13),
  SKYSQL_CAPABILITIES_RESERVED=               (1 << 14),
  SKYSQL_CAPABILITIES_SECURE_CONNECTION=      (1 << 15),
  SKYSQL_CAPABILITIES_MULTI_STATEMENTS=       (1 << 16),
  SKYSQL_CAPABILITIES_MULTI_RESULTS=          (1 << 17),
  SKYSQL_CAPABILITIES_PS_MULTI_RESULTS=       (1 << 18),
  SKYSQL_CAPABILITIES_PLUGIN_AUTH=            (1 << 19),
  SKYSQL_CAPABILITIES_SSL_VERIFY_SERVER_CERT= (1 << 30),
  SKYSQL_CAPABILITIES_REMEMBER_OPTIONS=       (1 << 31),
  SKYSQL_CAPABILITIES_CLIENT= (SKYSQL_CAPABILITIES_LONG_PASSWORD |
                                SKYSQL_CAPABILITIES_FOUND_ROWS |
                                SKYSQL_CAPABILITIES_LONG_FLAG |
                                SKYSQL_CAPABILITIES_CONNECT_WITH_DB |
                                SKYSQL_CAPABILITIES_LOCAL_FILES |
                                SKYSQL_CAPABILITIES_PLUGIN_AUTH |
                                SKYSQL_CAPABILITIES_TRANSACTIONS |
                                SKYSQL_CAPABILITIES_PROTOCOL_41 |
                                SKYSQL_CAPABILITIES_MULTI_STATEMENTS |
                                SKYSQL_CAPABILITIES_MULTI_RESULTS |
                                SKYSQL_CAPABILITIES_PS_MULTI_RESULTS |
                                SKYSQL_CAPABILITIES_SECURE_CONNECTION)
} skysql_capabilities_t;


#define SMALL_CHUNK 1024
#define MAX_CHUNK SMALL_CHUNK * 16
#define ToHex(Y) (Y>='0'&&Y<='9'?Y-'0':Y-'A'+10)

typedef struct {
        apr_socket_t *socket;
        char scramble[33];
        uint32_t server_capabs;
        uint32_t client_capabs;
	unsigned long tid;
	apr_pool_t *pool;
} MYSQL_conn;
