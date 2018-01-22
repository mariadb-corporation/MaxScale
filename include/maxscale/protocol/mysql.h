#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>

#include <mysql.h>
#include <mysqld_error.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <maxscale/buffer.h>
#include <maxscale/dcb.h>
#include <maxscale/session.h>
#include <maxscale/version.h>

MXS_BEGIN_DECLS

#define GW_MYSQL_VERSION "5.5.5-10.0.0 " MAXSCALE_VERSION "-maxscale"
#define GW_MYSQL_LOOP_TIMEOUT 300000000
#define GW_MYSQL_READ 0
#define GW_MYSQL_WRITE 1

#define MYSQL_HEADER_LEN 4
#define MYSQL_CHECKSUM_LEN 4
#define MYSQL_EOF_PACKET_LEN 9
#define MYSQL_OK_PACKET_MIN_LEN 11
#define MYSQL_ERR_PACKET_MIN_LEN 9

/**
 * Offsets and sizes of various parts of the client packet. If the offset is
 * defined but not the size, the size of the value is one byte.
 */
#define MYSQL_SEQ_OFFSET        3
#define MYSQL_COM_OFFSET        4
#define MYSQL_CHARSET_OFFSET    12
#define MYSQL_CLIENT_CAP_OFFSET 4
#define MYSQL_CLIENT_CAP_SIZE   4
#define MARIADB_CAP_OFFSET MYSQL_CHARSET_OFFSET + 19

#define GW_MYSQL_PROTOCOL_VERSION 10 // version is 10
#define GW_MYSQL_HANDSHAKE_FILLER 0x00
#define GW_MYSQL_SERVER_LANGUAGE 0x08
#define GW_MYSQL_MAX_PACKET_LEN 0xffffffL
#define GW_MYSQL_SCRAMBLE_SIZE 20
#define GW_SCRAMBLE_LENGTH_323 8

/**
 * Prepared statement payload response offsets for a COM_STMT_PREPARE response:
 *
 * [0]     OK (1)            -- always 0x00
 * [1-4]   statement_id (4)  -- statement-id
 * [5-6]   num_columns (2)   -- number of columns
 * [7-8]   num_params (2)    -- number of parameters
 * [9]     filler
 * [10-11] warning_count (2) -- number of warnings
 */
#define MYSQL_PS_ID_OFFSET     MYSQL_HEADER_LEN + 1
#define MYSQL_PS_ID_SIZE       4
#define MYSQL_PS_COLS_OFFSET   MYSQL_HEADER_LEN + 5
#define MYSQL_PS_COLS_SIZE     2
#define MYSQL_PS_PARAMS_OFFSET MYSQL_HEADER_LEN + 7
#define MYSQL_PS_PARAMS_SIZE   2
#define MYSQL_PS_WARN_OFFSET   MYSQL_HEADER_LEN + 10
#define MYSQL_PS_WARN_SIZE     2

/** Name of the default server side authentication plugin */
#define DEFAULT_MYSQL_AUTH_PLUGIN "mysql_native_password"

/** All authentication responses are at least this many bytes long */
#define MYSQL_AUTH_PACKET_BASE_SIZE 36

/** Maximum length of a MySQL packet */
#define MYSQL_PACKET_LENGTH_MAX 0x00ffffff

#ifndef MYSQL_SCRAMBLE_LEN
# define MYSQL_SCRAMBLE_LEN GW_MYSQL_SCRAMBLE_SIZE
#endif

/* Max length of fields in the mysql.user table */
#define MYSQL_USER_MAXLEN     128
#define MYSQL_PASSWORD_LEN    41
#define MYSQL_HOST_MAXLEN     60
#define MYSQL_DATABASE_MAXLEN 128
#define MYSQL_TABLE_MAXLEN    64

#define GW_NOINTR_CALL(A)       do { errno = 0; A; } while (errno == EINTR)
#define SMALL_CHUNK 1024
#define MAX_CHUNK SMALL_CHUNK * 8 * 4
#define ToHex(Y) (Y>='0'&&Y<='9'?Y-'0':Y-'A'+10)
#define COM_QUIT_PACKET_SIZE (4+1)
struct dcb;

typedef enum  
{
  TX_EMPTY        =   0,  ///< "none of the below"
  TX_EXPLICIT     =   1,  ///< an explicit transaction is active
  TX_IMPLICIT     =   2,  ///< an implicit transaction is active
  TX_READ_TRX     =   4,  ///<     transactional reads  were done
  TX_READ_UNSAFE  =   8,  ///< non-transaction   reads  were done
  TX_WRITE_TRX    =  16,  ///<     transactional writes were done
  TX_WRITE_UNSAFE =  32,  ///< non-transactional writes were done
  TX_STMT_UNSAFE  =  64,  ///< "unsafe" (non-deterministic like UUID()) stmts
  TX_RESULT_SET   = 128,  ///< result-set was sent
  TX_WITH_SNAPSHOT= 256,  ///< WITH CONSISTENT SNAPSHOT was used
  TX_LOCKED_TABLES= 512   ///< LOCK TABLES is active
} mysql_tx_state_t;

typedef enum
{
    MYSQL_PROTOCOL_ALLOC,
    MYSQL_PROTOCOL_ACTIVE,
    MYSQL_PROTOCOL_DONE
} mysql_protocol_state_t;


/*
 * MySQL session specific data
 *
 */
typedef struct mysql_session
{
#if defined(SS_DEBUG)
    skygw_chk_t myses_chk_top;
#endif
    uint8_t client_sha1[MYSQL_SCRAMBLE_LEN];        /*< SHA1(password) */
    char user[MYSQL_USER_MAXLEN + 1];               /*< username       */
    char db[MYSQL_DATABASE_MAXLEN + 1];             /*< database       */
    int  auth_token_len;                            /*< token length   */
    uint8_t *auth_token;                            /*< token          */
#if defined(SS_DEBUG)
    skygw_chk_t myses_chk_tail;
#endif
} MYSQL_session;

/** Protocol packing macros. */
#define gw_mysql_set_byte2(__buffer, __int) do { \
  (__buffer)[0]= (uint8_t)((__int) & 0xFF); \
  (__buffer)[1]= (uint8_t)(((__int) >> 8) & 0xFF); } while (0)
#define gw_mysql_set_byte3(__buffer, __int) do { \
  (__buffer)[0]= (uint8_t)((__int) & 0xFF); \
  (__buffer)[1]= (uint8_t)(((__int) >> 8) & 0xFF); \
  (__buffer)[2]= (uint8_t)(((__int) >> 16) & 0xFF); } while (0)
#define gw_mysql_set_byte4(__buffer, __int) do { \
  (__buffer)[0]= (uint8_t)((__int) & 0xFF); \
  (__buffer)[1]= (uint8_t)(((__int) >> 8) & 0xFF); \
  (__buffer)[2]= (uint8_t)(((__int) >> 16) & 0xFF); \
  (__buffer)[3]= (uint8_t)(((__int) >> 24) & 0xFF); } while (0)

/** Protocol unpacking macros. */
#define gw_mysql_get_byte2(__buffer) \
  (uint16_t)((__buffer)[0] | \
            ((__buffer)[1] << 8))
#define gw_mysql_get_byte3(__buffer) \
  (uint32_t)((__buffer)[0] | \
            ((__buffer)[1] << 8) | \
            ((__buffer)[2] << 16))
#define gw_mysql_get_byte4(__buffer) \
  (uint32_t)((__buffer)[0] | \
            ((__buffer)[1] << 8) | \
            ((__buffer)[2] << 16) | \
            ((__buffer)[3] << 24))
#define gw_mysql_get_byte8(__buffer) \
  ((uint64_t)(__buffer)[0] | \
  ((uint64_t)(__buffer)[1] << 8) | \
  ((uint64_t)(__buffer)[2] << 16) | \
  ((uint64_t)(__buffer)[3] << 24) | \
  ((uint64_t)(__buffer)[4] << 32) | \
  ((uint64_t)(__buffer)[5] << 40) | \
  ((uint64_t)(__buffer)[6] << 48) | \
  ((uint64_t)(__buffer)[7] << 56))

/** MySQL protocol constants */
typedef enum
{
    GW_MYSQL_CAPABILITIES_NONE =                   0,
    /** This is sent by pre-10.2 clients */
    GW_MYSQL_CAPABILITIES_CLIENT_MYSQL =           (1 << 0),
    GW_MYSQL_CAPABILITIES_FOUND_ROWS =             (1 << 1),
    GW_MYSQL_CAPABILITIES_LONG_FLAG =              (1 << 2),
    GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB =        (1 << 3),
    GW_MYSQL_CAPABILITIES_NO_SCHEMA =              (1 << 4),
    GW_MYSQL_CAPABILITIES_COMPRESS =               (1 << 5),
    GW_MYSQL_CAPABILITIES_ODBC =                   (1 << 6),
    GW_MYSQL_CAPABILITIES_LOCAL_FILES =            (1 << 7),
    GW_MYSQL_CAPABILITIES_IGNORE_SPACE =           (1 << 8),
    GW_MYSQL_CAPABILITIES_PROTOCOL_41 =            (1 << 9),
    GW_MYSQL_CAPABILITIES_INTERACTIVE =            (1 << 10),
    GW_MYSQL_CAPABILITIES_SSL =                    (1 << 11),
    GW_MYSQL_CAPABILITIES_IGNORE_SIGPIPE =         (1 << 12),
    GW_MYSQL_CAPABILITIES_TRANSACTIONS =           (1 << 13),
    GW_MYSQL_CAPABILITIES_RESERVED =               (1 << 14),
    GW_MYSQL_CAPABILITIES_SECURE_CONNECTION =      (1 << 15),
    GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS =       (1 << 16),
    GW_MYSQL_CAPABILITIES_MULTI_RESULTS =          (1 << 17),
    GW_MYSQL_CAPABILITIES_PS_MULTI_RESULTS =       (1 << 18),
    GW_MYSQL_CAPABILITIES_PLUGIN_AUTH =            (1 << 19),
    GW_MYSQL_CAPABILITIES_CONNECT_ATTRS =          (1 << 20),
    GW_MYSQL_CAPABILITIES_AUTH_LENENC_DATA =       (1 << 21),
    GW_MYSQL_CAPABILITIES_EXPIRE_PASSWORD =        (1 << 22),
    GW_MYSQL_CAPABILITIES_SESSION_TRACK =          (1 << 23),
    GW_MYSQL_CAPABILITIES_DEPRECATE_EOF =          (1 << 24),
    GW_MYSQL_CAPABILITIES_SSL_VERIFY_SERVER_CERT = (1 << 30),
    GW_MYSQL_CAPABILITIES_REMEMBER_OPTIONS =       (1 << 31),
    GW_MYSQL_CAPABILITIES_CLIENT = (
        GW_MYSQL_CAPABILITIES_CLIENT_MYSQL |
        GW_MYSQL_CAPABILITIES_FOUND_ROWS |
        GW_MYSQL_CAPABILITIES_LONG_FLAG |
        GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB |
        GW_MYSQL_CAPABILITIES_LOCAL_FILES |
        GW_MYSQL_CAPABILITIES_PLUGIN_AUTH |
        GW_MYSQL_CAPABILITIES_TRANSACTIONS |
        GW_MYSQL_CAPABILITIES_PROTOCOL_41 |
        GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS |
        GW_MYSQL_CAPABILITIES_MULTI_RESULTS |
        GW_MYSQL_CAPABILITIES_PS_MULTI_RESULTS |
        GW_MYSQL_CAPABILITIES_SECURE_CONNECTION),
    GW_MYSQL_CAPABILITIES_SERVER = (
        GW_MYSQL_CAPABILITIES_CLIENT_MYSQL |
        GW_MYSQL_CAPABILITIES_FOUND_ROWS |
        GW_MYSQL_CAPABILITIES_LONG_FLAG |
        GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB |
        GW_MYSQL_CAPABILITIES_NO_SCHEMA |
        GW_MYSQL_CAPABILITIES_ODBC |
        GW_MYSQL_CAPABILITIES_LOCAL_FILES |
        GW_MYSQL_CAPABILITIES_IGNORE_SPACE |
        GW_MYSQL_CAPABILITIES_PROTOCOL_41 |
        GW_MYSQL_CAPABILITIES_INTERACTIVE |
        GW_MYSQL_CAPABILITIES_IGNORE_SIGPIPE |
        GW_MYSQL_CAPABILITIES_TRANSACTIONS |
        GW_MYSQL_CAPABILITIES_RESERVED |
        GW_MYSQL_CAPABILITIES_SECURE_CONNECTION |
        GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS |
        GW_MYSQL_CAPABILITIES_MULTI_RESULTS |
        GW_MYSQL_CAPABILITIES_PS_MULTI_RESULTS |
        GW_MYSQL_CAPABILITIES_PLUGIN_AUTH),
} gw_mysql_capabilities_t;

/**
 * Capabilities supported by MariaDB 10.2 and later, stored in the last 4 bytes
 * of the 10 byte filler of the initial handshake packet.
 *
 * The actual capability bytes use by the server are left shifted by an extra 32
 * bits to get one 64 bit capability that combines the old and new capabilities.
 * Since we only use these in the non-shifted form, the definitions declared here
 * are right shifted by 32 bytes and can be directly copied into the extra capabilities.
 */
#define MXS_MARIA_CAP_PROGRESS             (1 << 0)
#define MXS_MARIA_CAP_COM_MULTI            (1 << 1)
#define MXS_MARIA_CAP_STMT_BULK_OPERATIONS (1 << 2)

typedef enum
{
    MXS_COM_SLEEP = 0,
    MXS_COM_QUIT,
    MXS_COM_INIT_DB,
    MXS_COM_QUERY,
    MXS_COM_FIELD_LIST,
    MXS_COM_CREATE_DB,
    MXS_COM_DROP_DB,
    MXS_COM_REFRESH,
    MXS_COM_SHUTDOWN,
    MXS_COM_STATISTICS,
    MXS_COM_PROCESS_INFO,
    MXS_COM_CONNECT,
    MXS_COM_PROCESS_KILL,
    MXS_COM_DEBUG,
    MXS_COM_PING,
    MXS_COM_TIME = 15,
    MXS_COM_DELAYED_INSERT,
    MXS_COM_CHANGE_USER,
    MXS_COM_BINLOG_DUMP,
    MXS_COM_TABLE_DUMP,
    MXS_COM_CONNECT_OUT = 20,
    MXS_COM_REGISTER_SLAVE,
    MXS_COM_STMT_PREPARE = 22,
    MXS_COM_STMT_EXECUTE = 23,
    MXS_COM_STMT_SEND_LONG_DATA = 24,
    MXS_COM_STMT_CLOSE = 25,
    MXS_COM_STMT_RESET = 26,
    MXS_COM_SET_OPTION = 27,
    MXS_COM_STMT_FETCH = 28,
    MXS_COM_DAEMON,
    MXS_COM_END
} mxs_mysql_cmd_t;

static const mxs_mysql_cmd_t MXS_COM_UNDEFINED = (mxs_mysql_cmd_t) - 1;

/**
 * List of server commands, and number of response packets are stored here.
 * server_command_t is used in MySQLProtocol structure, so for each DCB there is
 * one MySQLProtocol and one server command list.
 */
typedef struct server_command_st
{
    mxs_mysql_cmd_t           scom_cmd;
    int                       scom_nresponse_packets; /*< packets in response */
    size_t                    scom_nbytes_to_read;    /*< bytes left to read in current packet */
    struct server_command_st* scom_next;
} server_command_t;

/**
 * MySQL Protocol specific state data.
 *
 * Protocol carries information from client side to backend side, such as
 * MySQL session command information and history of earlier session commands.
 */
typedef struct
{
#if defined(SS_DEBUG)
    skygw_chk_t            protocol_chk_top;
#endif
    int                    fd;                           /*< The socket descriptor */
    struct dcb*            owner_dcb;                    /*< The DCB of the socket we are running on */
    mxs_mysql_cmd_t        current_command;              /*< Current command being executed */
    server_command_t       protocol_command;             /*< session command list */
    server_command_t*      protocol_cmd_history;         /*< session command history */
    mxs_auth_state_t       protocol_auth_state;          /*< Authentication status */
    mysql_protocol_state_t protocol_state;               /*< Protocol struct status */
    uint8_t                scramble[MYSQL_SCRAMBLE_LEN]; /*< server scramble, created or received */
    uint32_t               server_capabilities;          /*< server capabilities, created or received */
    uint32_t               client_capabilities;          /*< client capabilities, created or received */
    uint32_t               extra_capabilities;           /*< MariaDB 10.2 capabilities */
    uint64_t               thread_id;                    /*< MySQL Thread ID. Send only 32bits in handshake. */
    unsigned int           charset;                      /*< MySQL character set at connect time */
    int                    ignore_replies;               /*< How many replies should be discarded */
    GWBUF*                 stored_query;                 /*< Temporarily stored queries */
    bool                   collect_result;               /*< Collect the next result set as one buffer */
    uint32_t               num_eof_packets;              /*< Signal number to indicate is current packet is ok packet*/
#if defined(SS_DEBUG)
    skygw_chk_t            protocol_chk_tail;
#endif
} MySQLProtocol;

typedef struct
{
    uint32_t id;
    uint16_t columns;
    uint16_t parameters;
    uint16_t warnings;
} MXS_PS_RESPONSE;

/** Defines for response codes */
#define MYSQL_REPLY_ERR               0xff
#define MYSQL_REPLY_OK                0x00
#define MYSQL_REPLY_EOF               0xfe
#define MYSQL_REPLY_LOCAL_INFILE      0xfb
#define MYSQL_REPLY_AUTHSWITCHREQUEST 0xfe /**< Only sent during authentication */

static inline mxs_mysql_cmd_t MYSQL_GET_COMMAND(const uint8_t* header)
{
    return (mxs_mysql_cmd_t)header[4];
}

static inline uint8_t MYSQL_GET_PACKET_NO(const uint8_t* header)
{
    return header[3];
}

static inline uint32_t MYSQL_GET_PAYLOAD_LEN(const uint8_t* header)
{
    return gw_mysql_get_byte3(header);
}

static inline uint32_t MYSQL_GET_PACKET_LEN(const GWBUF* buffer)
{
    return MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buffer)) + MYSQL_HEADER_LEN;
}

#define MYSQL_GET_ERRCODE(payload)              (gw_mysql_get_byte2(&payload[5]))
#define MYSQL_GET_STMTOK_NPARAM(payload)        (gw_mysql_get_byte2(&payload[9]))
#define MYSQL_GET_STMTOK_NATTR(payload)         (gw_mysql_get_byte2(&payload[11]))
#define MYSQL_GET_NATTR(payload)                ((int)payload[4])

static inline bool MYSQL_IS_ERROR_PACKET(const uint8_t* header)
{
    return MYSQL_GET_COMMAND(header) == MYSQL_REPLY_ERR;
}

static inline bool MYSQL_IS_COM_QUIT(const uint8_t* header)
{
    return MYSQL_GET_COMMAND(header) == MXS_COM_QUIT &&
        MYSQL_GET_PAYLOAD_LEN(header) == 1;
}

static inline bool MYSQL_IS_COM_INIT_DB(const uint8_t* header)
{
    return MYSQL_GET_COMMAND(header) == MXS_COM_INIT_DB;
}

static inline bool MYSQL_IS_CHANGE_USER(const uint8_t* header)
{
    return MYSQL_GET_COMMAND(header) == MXS_COM_CHANGE_USER;
}

/* The following can be compared using memcmp to detect a null password */
extern uint8_t null_client_sha1[MYSQL_SCRAMBLE_LEN];

/**
 * Allocate a new MySQL_session
 *
 * @return New MySQL_session or NULL if memory allocation failed
 */
MYSQL_session* mysql_session_alloc();

/**
 * Create MySQL protocol structure
 *
 * @param dcb Owning DCB
 * @param fd  File descriptor of the DCB
 *
 * @return New protocol or NULL on error
 */
MySQLProtocol* mysql_protocol_init(DCB* dcb, int fd);

/**
 * Free protocol object
 *
 * @param dcb Owner DCB
 *
 * @return True if protocol was closed
 */
bool mysql_protocol_done(DCB* dcb);

/**
 * Return a string representation of a MySQL protocol state.
 *
 * @param state The protocol state
 *
 * @return String representation of the state
 */
const char *gw_mysql_protocol_state2string(int state);

/**
 * Set current command being executed
 *
 * @param dcb The DCB whose protocol is modified
 * @param cmd The command being executed
 *
 * @note This function should not be used in normal operation
 */
void mysql_protocol_set_current_command(DCB* dcb, mxs_mysql_cmd_t cmd);

GWBUF* mysql_create_com_quit(GWBUF* bufparam, int sequence);
GWBUF* mysql_create_custom_error(int sequence, int affected_rows, const char* msg);
GWBUF *mysql_create_standard_error(int sequence, int error_number, const char *msg);

int mysql_send_com_quit(DCB* dcb, int sequence, GWBUF* buf);
int mysql_send_custom_error(DCB *dcb, int sequence, int affected_rows, const char* msg);
int mysql_send_standard_error(DCB *dcb, int sequence, int errnum, const char *msg);
int mysql_send_auth_error(DCB *dcb, int sequence, int affected_rows, const char* msg);

GWBUF* gw_MySQL_get_next_packet(GWBUF** p_readbuf);
GWBUF* gw_MySQL_get_packets(GWBUF** p_readbuf, int* npackets);
void   protocol_add_srv_command(MySQLProtocol* p, mxs_mysql_cmd_t cmd);
void   protocol_remove_srv_command(MySQLProtocol* p);
bool   protocol_waits_response(MySQLProtocol* p);
mxs_mysql_cmd_t protocol_get_srv_command(MySQLProtocol* p, bool removep);
int  get_stmt_nresponse_packets(GWBUF* buf, mxs_mysql_cmd_t cmd);
bool protocol_get_response_status(MySQLProtocol* p, int* npackets, size_t* nbytes);
void protocol_set_response_status(MySQLProtocol* p, int  npackets, size_t  nbytes);
void protocol_archive_srv_command(MySQLProtocol* p);

char* create_auth_fail_str(char *username, char *hostaddr, bool password, char *db, int);

void init_response_status(GWBUF* buf, uint8_t cmd, int* npackets, size_t* nbytes);
bool read_complete_packet(DCB *dcb, GWBUF **readbuf);
bool gw_get_shared_session_auth_info(DCB* dcb, MYSQL_session* session);
void mxs_mysql_get_session_track_info(GWBUF *buff, MySQLProtocol *proto);
mysql_tx_state_t parse_trx_state(const char *str);

/**
 * Decode server handshake
 *
 * @param conn    The MySQLProtocol structure
 * @param payload The handshake payload without the network header
 *
 * @return 0 on success, -1 on failure
 *
 */
int gw_decode_mysql_server_handshake(MySQLProtocol *conn, uint8_t *payload);

/**
 * Create a response to the server handshake
 *
 * @param client          Shared session data
 * @param conn            MySQL Protocol object for this connection
 * @param with_ssl        Whether to create an SSL response or a normal response packet
 * @param ssl_established Set to true if the SSL response has been sent
 *
 * @return Generated response packet
 */
GWBUF* gw_generate_auth_response(MYSQL_session* client, MySQLProtocol *conn,
                                 bool with_ssl, bool ssl_established);

/** Read the backend server's handshake */
bool gw_read_backend_handshake(DCB *dcb, GWBUF *buffer);

/** Send the server handshake response packet to the backend server */
mxs_auth_state_t gw_send_backend_auth(DCB *dcb);

/** Sends a response for an AuthSwitchRequest to the default auth plugin */
int send_mysql_native_password_response(DCB* dcb);

/** Write an OK packet to a DCB */
int mxs_mysql_send_ok(DCB *dcb, int sequence, uint8_t affected_rows, const char* message);

/**
 * @brief Check if the buffer contains an OK packet
 *
 * @param buffer Buffer containing a complete MySQL packet
 * @return True if the buffer contains an OK packet
 */
bool mxs_mysql_is_ok_packet(GWBUF *buffer);

/**
 * @brief Check if the buffer contains an ERR packet
 *
 * @param buffer Buffer containing a complete MySQL packet
 * @return True if the buffer contains an ERR packet
 */
bool mxs_mysql_is_err_packet(GWBUF *buffer);

/**
 * @brief Check if a buffer contains a result set
 *
 * @param buffer Buffer to check
 *
 * @return True if the @c buffer contains the start of a result set
 */
bool mxs_mysql_is_result_set(GWBUF *buffer);

/**
 * @brief Check if the buffer contains a prepared statement OK packet
 *
 * @param buffer Buffer to check
 *
 * @return True if the @c buffer contains a prepared statement OK packet
 */
bool mxs_mysql_is_prep_stmt_ok(GWBUF *buffer);

/**
 * @brief Check if the OK packet is followed by another result
 *
 * @param buffer Buffer to check
 *
 * @return True if more results are expected
 */
bool mxs_mysql_more_results_after_ok(GWBUF *buffer);

/** Get current command for a session */
mxs_mysql_cmd_t mxs_mysql_current_command(MXS_SESSION* session);
/**
 * @brief Calculate how many packets a session command will receive
 *
 * @param buf Buffer containing the response
 * @param cmd Command that was executed
 * @param npackets Pointer where the number of packets is stored
 * @param nbytes Pointer where number of bytes is stored
 */
void mysql_num_response_packets(GWBUF *buf, uint8_t cmd,
                                int* npackets, size_t *nbytes);

/**
 * @brief Return current database of the session
 *
 * If no active database is in use, the database is an empty string.
 *
 * @param session Session to inspect
 *
 * @return The current database
 */
const char* mxs_mysql_get_current_db(MXS_SESSION* session);

/**
 * @brief Set the currently active database for a session
 *
 * @param session Session to modify
 * @param db      The new database
 */
void mxs_mysql_set_current_db(MXS_SESSION* session, const char* db);

/**
 * @brief Get the command byte
 *
 * @param buffer Buffer containing a complete MySQL packet
 *
 * @return The command byte
 */
static inline uint8_t mxs_mysql_get_command(GWBUF* buffer)
{
    if (GWBUF_LENGTH(buffer) > MYSQL_HEADER_LEN)
    {
        return GWBUF_DATA(buffer)[4];
    }
    else
    {
        uint8_t command = 0;
        gwbuf_copy_data(buffer, MYSQL_HEADER_LEN, 1, &command);
        return command;
    }
}

/**
 * @brief Extract PS response values
 *
 * @param buffer Buffer containing a complete response to a binary protocol
 *               preparation of a prepared statement
 * @param out    Destination where the values are extracted
 *
 * @return True if values were extracted successfully
 */
bool mxs_mysql_extract_ps_response(GWBUF* buffer, MXS_PS_RESPONSE* out);

/**
 * @brief Extract the ID from a COM_STMT command
 *
 * All the COM_STMT type commands store the statement ID in the same place.
 *
 * @param buffer Buffer containing one of the COM_STMT commands (not COM_STMT_PREPARE)
 *
 * @return The statement ID
 */
uint32_t mxs_mysql_extract_ps_id(GWBUF* buffer);

/**
 * @brief Determine if a packet contains a one way message
 *
 * @param cmd Command to inspect
 *
 * @return True if a response is expected from the server
 */
bool mxs_mysql_command_will_respond(uint8_t cmd);

/* Type of the kill-command sent by client. */
typedef enum kill_type
{
    KT_CONNECTION = (1 << 0),
    KT_QUERY      = (1 << 1),
    KT_SOFT       = (1 << 2),
    KT_HARD       = (1 << 3)
} kill_type_t;

void mxs_mysql_execute_kill(MXS_SESSION* issuer, uint64_t target_id, kill_type_t type);
void mxs_mysql_execute_kill_user(MXS_SESSION* issuer, const char* user, kill_type_t type);

MXS_END_DECLS
