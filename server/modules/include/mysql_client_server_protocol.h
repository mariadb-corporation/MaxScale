#ifndef _MYSQL_PROTOCOL_H
#define _MYSQL_PROTOCOL_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Revision History
 *
 * Date         Who                     Description
 * 01-06-2013   Mark Riddoch            Initial implementation
 * 14-06-2013   Massimiliano Pinto      Added specific data
 *                                      for MySQL session
 * 04-07-2013   Massimiliano Pinto      Added new MySQL protocol status for asynchronous connection
 *                                      Added authentication reply status
 * 12-07-2013   Massimiliano Pinto      Added routines for change_user
 * 14-02-2014   Massimiliano Pinto      setipaddress returns int
 * 25-02-2014   Massimiliano Pinto      Added dcb parameter to gw_find_mysql_user_password_sha1()
 *                                      and repository to gw_check_mysql_scramble_data()
 *                                      It's now possible to specify a different users' table than
 *                                      dcb->service->users default
 * 26-02-2014   Massimiliano Pinto      Removed previously added parameters to gw_check_mysql_scramble_data() and
 *                                      gw_find_mysql_user_password_sha1()
 * 28-02-2014   Massimiliano Pinto      MYSQL_DATABASE_MAXLEN,MYSQL_USER_MAXLEN moved to dbusers.h
 * 07-02-2016   Martin Brampton         Extend MYSQL_session type; add MYSQL_AUTH_SUCCEEDED
 * 17-05-2016   Martin Brampton         Moved gw_find_mysql_user_password_sha1 to mysql_auth.c
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <openssl/sha.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include <openssl/crypto.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <service.h>
#include <router.h>
#include <poll.h>
#include <users.h>
#include <dbusers.h>
#include <version.h>
#include <housekeeper.h>
#include <mysql.h>

#define GW_MYSQL_VERSION "5.5.5-10.0.0 " MAXSCALE_VERSION "-maxscale"
#define GW_MYSQL_LOOP_TIMEOUT 300000000
#define GW_MYSQL_READ 0
#define GW_MYSQL_WRITE 1
#define MYSQL_HEADER_LEN 4L
#define MYSQL_CHECKSUM_LEN 4L

#define GW_MYSQL_PROTOCOL_VERSION 10 // version is 10
#define GW_MYSQL_HANDSHAKE_FILLER 0x00
#define GW_MYSQL_SERVER_CAPABILITIES_BYTE1 0xff
#define GW_MYSQL_SERVER_CAPABILITIES_BYTE2 0xf7
#define GW_MYSQL_SERVER_LANGUAGE 0x08
#define GW_MYSQL_MAX_PACKET_LEN 0xffffffL;
#define GW_MYSQL_SCRAMBLE_SIZE 20
#define GW_SCRAMBLE_LENGTH_323 8

#define DEFAULT_MYSQL_AUTH_PLUGIN "mysql_native_password"

/** Maximum length of a MySQL packet */
#define MYSQL_PACKET_LENGTH_MAX 0x00ffffff

#ifndef MYSQL_SCRAMBLE_LEN
# define MYSQL_SCRAMBLE_LEN GW_MYSQL_SCRAMBLE_SIZE
#endif

#define MYSQL_HOSTNAME_MAXLEN  60

#define GW_NOINTR_CALL(A)       do { errno = 0; A; } while (errno == EINTR)
#define SMALL_CHUNK 1024
#define MAX_CHUNK SMALL_CHUNK * 8 * 4
#define ToHex(Y) (Y>='0'&&Y<='9'?Y-'0':Y-'A'+10)
#define COM_QUIT_PACKET_SIZE (4+1)
struct dcb;


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
    GW_MYSQL_CAPABILITIES_LONG_PASSWORD =          (1 << 0),
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
    GW_MYSQL_CAPABILITIES_SSL_VERIFY_SERVER_CERT = (1 << 30),
    GW_MYSQL_CAPABILITIES_REMEMBER_OPTIONS =       (1 << 31),
    GW_MYSQL_CAPABILITIES_CLIENT = (GW_MYSQL_CAPABILITIES_LONG_PASSWORD |
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
    GW_MYSQL_CAPABILITIES_CLIENT_COMPRESS = (GW_MYSQL_CAPABILITIES_LONG_PASSWORD |
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
                                             GW_MYSQL_CAPABILITIES_COMPRESS
                                            ),
} gw_mysql_capabilities_t;

typedef enum enum_server_command mysql_server_cmd_t;

static const mysql_server_cmd_t MYSQL_COM_UNDEFINED = (mysql_server_cmd_t) - 1;

/**
 * List of server commands, and number of response packets are stored here.
 * server_command_t is used in MySQLProtocol structure, so for each DCB there is
 * one MySQLProtocol and one server command list.
 */
typedef struct server_command_st
{
    mysql_server_cmd_t        scom_cmd;
    int                       scom_nresponse_packets; /*< packets in response */
    ssize_t                   scom_nbytes_to_read;    /*< bytes left to read in current packet */
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
    skygw_chk_t     protocol_chk_top;
#endif
    int                 fd;                           /*< The socket descriptor */
    struct dcb          *owner_dcb;                   /*< The DCB of the socket
        * we are running on */
    SPINLOCK            protocol_lock;
    mysql_server_cmd_t  current_command;              /**< Current command being executed */
    server_command_t    protocol_command;             /*< session command list */
    server_command_t*   protocol_cmd_history;         /*< session command history */
    mxs_auth_state_t  protocol_auth_state;            /*< Authentication status */
    mysql_protocol_state_t protocol_state;            /*< Protocol struct status */
    uint8_t             scramble[MYSQL_SCRAMBLE_LEN]; /*< server scramble,
        * created or received */
    uint32_t            server_capabilities;          /*< server capabilities,
        * created or received */
    uint32_t            client_capabilities;          /*< client capabilities,
        * created or received */
    unsigned        long tid;                         /*< MySQL Thread ID, in
        * handshake */
    unsigned int    charset;                          /*< MySQL character set at connect time */
#if defined(SS_DEBUG)
    skygw_chk_t     protocol_chk_tail;
#endif
} MySQLProtocol;

/** Defines for response codes */
#define MYSQL_REPLY_ERR 0xff
#define MYSQL_REPLY_OK  0x00

/*
 * Let's try this with proper enums instead of numbers
#define MYSQL_GET_COMMAND(payload)              (payload[4])
#define MYSQL_GET_PACKET_NO(payload)            (payload[3])
#define MYSQL_GET_PACKET_LEN(payload)           (gw_mysql_get_byte3(payload))
#define MYSQL_GET_ERRCODE(payload)              (gw_mysql_get_byte2(&payload[5]))
#define MYSQL_IS_ERROR_PACKET(payload)          (MYSQL_GET_COMMAND(payload)==0xff)
#define MYSQL_IS_COM_QUIT(payload)              (MYSQL_GET_COMMAND(payload)==0x01)
#define MYSQL_IS_COM_INIT_DB(payload)              (MYSQL_GET_COMMAND(payload)==0x02)
#define MYSQL_IS_CHANGE_USER(payload)       (MYSQL_GET_COMMAND(payload)==0x11)
#define MYSQL_GET_NATTR(payload)                ((int)payload[4])
*/
#define MYSQL_GET_COMMAND(payload)              ((mysql_server_cmd_t)((payload)[4]))
#define MYSQL_GET_PACKET_NO(payload)            (payload[3])
#define MYSQL_GET_PACKET_LEN(payload)           (gw_mysql_get_byte3(payload))
#define MYSQL_GET_ERRCODE(payload)              (gw_mysql_get_byte2(&payload[5]))
#define MYSQL_GET_STMTOK_NPARAM(payload)        (gw_mysql_get_byte2(&payload[9]))
#define MYSQL_GET_STMTOK_NATTR(payload)         (gw_mysql_get_byte2(&payload[11]))
#define MYSQL_IS_ERROR_PACKET(payload)          ((int)MYSQL_GET_COMMAND(payload)==MYSQL_REPLY_ERR)
#define MYSQL_IS_COM_QUIT(payload)              (MYSQL_GET_COMMAND(payload)==MYSQL_COM_QUIT)
#define MYSQL_IS_COM_INIT_DB(payload)           (MYSQL_GET_COMMAND(payload)==MYSQL_COM_INIT_DB)
#define MYSQL_IS_CHANGE_USER(payload)       (MYSQL_GET_COMMAND(payload)==MYSQL_COM_CHANGE_USER)
#define MYSQL_GET_NATTR(payload)                ((int)payload[4])


MySQLProtocol* mysql_protocol_init(DCB* dcb, int fd);
void           mysql_protocol_done (DCB* dcb);
const char *gw_mysql_protocol_state2string(int state);
int        mysql_send_com_quit(DCB* dcb, int packet_number, GWBUF* buf);
GWBUF*     mysql_create_com_quit(GWBUF* bufparam, int packet_number);

int mysql_send_custom_error (
    DCB *dcb,
    int packet_number,
    int in_affected_rows,
    const char* mysql_message);

GWBUF* mysql_create_custom_error(
    int packet_number,
    int affected_rows,
    const char* msg);

GWBUF *mysql_create_standard_error(int packet_number,
                                   int error_number,
                                   const char *error_message);

int mysql_send_standard_error(DCB *dcb,
                              int packet_number,
                              int error_number,
                              const char *error_message);

int mysql_send_auth_error (
    DCB *dcb,
    int packet_number,
    int in_affected_rows,
    const char* mysql_message);

void gw_sha1_str(const uint8_t *in, int in_len, uint8_t *out);
void gw_sha1_2_str(
    const uint8_t *in,
    int in_len,
    const uint8_t *in2,
    int in2_len,
    uint8_t *out);
void gw_str_xor(
    uint8_t       *output,
    const uint8_t *input1,
    const uint8_t *input2,
    unsigned int  len);

char  *gw_bin2hex(char *out, const uint8_t *in, unsigned int len);
int    gw_hex2bin(uint8_t *out, const char *in, unsigned int len);
int    gw_generate_random_str(char *output, int len);
int    setnonblocking(int fd);
int    setipaddress(struct in_addr *a, char *p);
GWBUF* gw_MySQL_get_next_packet(GWBUF** p_readbuf);
GWBUF* gw_MySQL_get_packets(GWBUF** p_readbuf, int* npackets);
GWBUF* gw_MySQL_discard_packets(GWBUF* buf, int npackets);
void   protocol_add_srv_command(MySQLProtocol* p, mysql_server_cmd_t cmd);
void   protocol_remove_srv_command(MySQLProtocol* p);
bool   protocol_waits_response(MySQLProtocol* p);
mysql_server_cmd_t protocol_get_srv_command(MySQLProtocol* p, bool removep);
int  get_stmt_nresponse_packets(GWBUF* buf, mysql_server_cmd_t cmd);
bool protocol_get_response_status (MySQLProtocol* p, int* npackets, ssize_t* nbytes);
void protocol_set_response_status (MySQLProtocol* p, int  npackets, ssize_t  nbytes);
void protocol_archive_srv_command(MySQLProtocol* p);

char* create_auth_fail_str(char *username, char *hostaddr, char *sha1, char *db, int);

void init_response_status (
    GWBUF* buf,
    mysql_server_cmd_t cmd,
    int* npackets,
    ssize_t* nbytes);
bool read_complete_packet(DCB *dcb, GWBUF **readbuf);

#endif /** _MYSQL_PROTOCOL_H */
