#ifndef _MYSQL_PROTOCOL_H
#define _MYSQL_PROTOCOL_H
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
 * Revision History
 *
 * Date         Who                     Description
 * 01-06-2013   Mark Riddoch            Initial implementation
 * 14-06-2013   Massimiliano Pinto      Added specific data
 *                                      for MySQL session
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <openssl/sha.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <service.h>
#include <router.h>
#include <poll.h>
#include <users.h>

#ifndef MYSQL_SCRAMBLE_LEN
#define MYSQL_SCRAMBLE_LEN GW_MYSQL_SCRAMBLE_SIZE
#endif

#define MYSQL_USER_MAXLEN 128
#define MYSQL_DATABASE_MAXLEN 128

#define GW_VERSION "0.1.0"
#define GW_MYSQL_VERSION "5.5.22-SKYSQL-" GW_VERSION
#define GW_MYSQL_LOOP_TIMEOUT 300000000
#define GW_MYSQL_READ 0
#define GW_MYSQL_WRITE 1

#define GW_MYSQL_PROTOCOL_VERSION 10 // version is 10
#define GW_MYSQL_HANDSHAKE_FILLER 0x00
#define GW_MYSQL_SERVER_CAPABILITIES_BYTE1 0xff
#define GW_MYSQL_SERVER_CAPABILITIES_BYTE2 0xf7
#define GW_MYSQL_SERVER_LANGUAGE 0x08
#define GW_MYSQL_MAX_PACKET_LEN 0xffffffL;
#define GW_MYSQL_SCRAMBLE_SIZE 20

#define GW_NOINTR_CALL(A)       do { errno = 0; A; } while (errno == EINTR)
// network buffer is 32K
#define MAX_BUFFER_SIZE 32768
// socket send buffer for backend
#define GW_BACKEND_SO_SNDBUF 1024
#define SMALL_CHUNK 1024
#define MAX_CHUNK SMALL_CHUNK * 8 * 4
#define ToHex(Y) (Y>='0'&&Y<='9'?Y-'0':Y-'A'+10)

struct dcb;

/*
 * MySQL Protocol specific state data
 */
typedef struct {
	int		fd;                             /* The socket descriptor */
 	struct dcb	*descriptor;                    /* The DCB of the socket we are running on */
	int		state;                          /* Current descriptor state */
	uint8_t		scramble[MYSQL_SCRAMBLE_LEN];   /* server scramble, created or received */
	uint32_t	server_capabilities;            /* server capabilities, created or received */
	uint32_t	client_capabilities;            /* client capabilities, created or received */
	unsigned	long tid;                       /* MySQL Thread ID, in handshake */
} MySQLProtocol;

/*
 * MySQL session specific data
 *
 */
typedef struct mysql_session {
        uint8_t client_sha1[MYSQL_SCRAMBLE_LEN];        /* SHA1(passowrd) */
        char user[MYSQL_USER_MAXLEN];                   /* username */
        char db[MYSQL_DATABASE_MAXLEN];                 /* database */
} MYSQL_session;

/* MySQL Protocol States */
#define MYSQL_ALLOC             0       /* Allocate data */
#define MYSQL_AUTH_SENT         1       /* Authentication handshake has been sent */
#define MYSQL_AUTH_RECV         2       /* Received user, password, db and capabilities */
#define MYSQL_AUTH_FAILED       3       /* Auth failed, return error packet */
#define MYSQL_IDLE              4       /* Auth done. Protocol is idle, waiting for statements */
#define MYSQL_ROUTING           5       /* The received command has been routed to backend(s) */
#define MYSQL_WAITING_RESULT    6       /* Waiting for result set */

/* Protocol packing macros. */
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

/* Protocol unpacking macros. */
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

/* MySQL protocol constants */
typedef enum
{
  GW_MYSQL_CAPABILITIES_NONE=                   0,
  GW_MYSQL_CAPABILITIES_LONG_PASSWORD=          (1 << 0),
  GW_MYSQL_CAPABILITIES_FOUND_ROWS=             (1 << 1),
  GW_MYSQL_CAPABILITIES_LONG_FLAG=              (1 << 2),
  GW_MYSQL_CAPABILITIES_CONNECT_WITH_DB=        (1 << 3),
  GW_MYSQL_CAPABILITIES_NO_SCHEMA=              (1 << 4),
  GW_MYSQL_CAPABILITIES_COMPRESS=               (1 << 5),
  GW_MYSQL_CAPABILITIES_ODBC=                   (1 << 6),
  GW_MYSQL_CAPABILITIES_LOCAL_FILES=            (1 << 7),
  GW_MYSQL_CAPABILITIES_IGNORE_SPACE=           (1 << 8),
  GW_MYSQL_CAPABILITIES_PROTOCOL_41=            (1 << 9),
  GW_MYSQL_CAPABILITIES_INTERACTIVE=            (1 << 10),
  GW_MYSQL_CAPABILITIES_SSL=                    (1 << 11),
  GW_MYSQL_CAPABILITIES_IGNORE_SIGPIPE=         (1 << 12),
  GW_MYSQL_CAPABILITIES_TRANSACTIONS=           (1 << 13),
  GW_MYSQL_CAPABILITIES_RESERVED=               (1 << 14),
  GW_MYSQL_CAPABILITIES_SECURE_CONNECTION=      (1 << 15),
  GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS=       (1 << 16),
  GW_MYSQL_CAPABILITIES_MULTI_RESULTS=          (1 << 17),
  GW_MYSQL_CAPABILITIES_PS_MULTI_RESULTS=       (1 << 18),
  GW_MYSQL_CAPABILITIES_PLUGIN_AUTH=            (1 << 19),
  GW_MYSQL_CAPABILITIES_SSL_VERIFY_SERVER_CERT= (1 << 30),
  GW_MYSQL_CAPABILITIES_REMEMBER_OPTIONS=       (1 << 31),
  GW_MYSQL_CAPABILITIES_CLIENT= (GW_MYSQL_CAPABILITIES_LONG_PASSWORD |
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
  GW_MYSQL_CAPABILITIES_CLIENT_COMPRESS= (GW_MYSQL_CAPABILITIES_LONG_PASSWORD |
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
#endif

extern void gw_sha1_str(const uint8_t *in, int in_len, uint8_t *out);
extern void gw_sha1_2_str(const uint8_t *in, int in_len, const uint8_t *in2, int in2_len, uint8_t *out);
extern void gw_str_xor(uint8_t *output, const uint8_t *input1, const uint8_t *input2, unsigned int len);
extern char *gw_bin2hex(char *out, const uint8_t *in, unsigned int len);
extern int gw_hex2bin(uint8_t *out, const char *in, unsigned int len);
extern int gw_generate_random_str(char *output, int len);
extern char *gw_strend(register const char *s);
extern int setnonblocking(int fd);
extern void setipaddress(struct in_addr *a, char *p);
extern int gw_read_gwbuff(DCB *dcb, GWBUF **head, int b);
void gw_mysql_close(MySQLProtocol **ptr);
