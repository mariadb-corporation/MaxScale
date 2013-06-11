/*
 * This file is distributed as part of the SkySQL Gateway. It is free
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
 * 
 */

/*
 * MYSQL mysql protocol header file
 * Revision History
 *
 * Date		Who			Description
 * 10/06/13	Massimiliano Pinto	Initial implementation
 *
 */

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


#define SMALL_CHUNK 1024
#define MAX_CHUNK SMALL_CHUNK * 8 * 4
#define ToHex(Y) (Y>='0'&&Y<='9'?Y-'0':Y-'A'+10)
