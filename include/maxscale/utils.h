/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file utils.h Utility functions headers
 */

#include <maxscale/cdefs.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <sys/un.h>

MXS_BEGIN_DECLS

#define CALCLEN(i) ((size_t)(floor(log10(abs((int64_t)i))) + 1))
#define UINTLEN(i) (i < 10 ? 1 : (i < 100 ? 2 : (i < 1000 ? 3 : CALCLEN(i))))

#define MXS_ARRAY_NELEMS(array) ((size_t)(sizeof(array) / sizeof(array[0])))

/** Macro for safe pointer arithmetic on void pointers
 * @param a The void pointer
 * @param b The offset into @c a
 */
#define MXS_PTR(a, b) (((uint8_t*)(a)) + (b))

/** The type of the socket */
enum mxs_socket_type
{
    MXS_SOCKET_LISTENER,    /**< */
    MXS_SOCKET_NETWORK,
};

bool utils_init();      /*< Call this first before using any other function */
void utils_end();

/**
 * Configure network socket options
 *
 * This is a helper function for setting various socket options that are always wanted for all types
 * of connections. It sets the socket into nonblocking mode, configures sndbuf and rcvbuf sizes
 * and sets TCP_NODELAY (no Nagle algorithm).
 *
 * @param so   Socket to configure
 * @param type Socket type
 *
 * @return True if configuration was successful
 */
bool configure_network_socket(int so, int type);

/**
 * @brief Create a network socket and a socket configuration
 *
 * This helper function can be used to open both listener socket and network
 * connection sockets. For listener sockets, the @c host and @c port parameters
 * tell where the socket will bind to. For network sockets, the parameters tell
 * where the connection is created.
 *
 * After calling this function, the only thing that needs to be done is to
 * give @c addr and the return value of this function as the parameters to
 * either bind() (for listeners) or connect() (for outbound network connections).
 *
 * @param type Type of the socket, either MXS_SOCKET_LISTENER for a listener
 *             socket or MXS_SOCKET_NETWORK for a network connection socket
 * @param addr Pointer to a struct sockaddr_storage where the socket
 *             configuration is stored
 * @param host The target host for which the socket is created
 * @param port The target port on the host
 *
 * @return The opened socket or -1 on failure
 */
int open_network_socket(enum mxs_socket_type type,
                        struct sockaddr_storage* addr,
                        const char* host,
                        uint16_t port);

/**
 * @brief Create a UNIX domain socket
 *
 * This opens and prepares a UNIX domain socket for use. The @c addr parameter
 * can be given to the bind() function to bind the socket.
 *
 * @param type Type of the socket, either MXS_SOCKET_LISTENER for a listener
 *             socket or MXS_SOCKET_NETWORK for a network connection socket
 * @param addr Pointer to a struct sockaddr_un where the socket configuration
 *             is stored
 * @param path Path to the socket
 *
 * @return The opened socket or -1 on failure
 */
int open_unix_socket(enum mxs_socket_type type,
                     struct sockaddr_un*  addr,
                     const char* path);

int         setnonblocking(int fd);
int         setblocking(int fd);
char*       gw_strend(register const char* s);
static char gw_randomchar();
int         gw_generate_random_str(char* output, int len);
int         gw_hex2bin(uint8_t* out, const char* in, unsigned int len);
char*       gw_bin2hex(char* out, const uint8_t* in, unsigned int len);
void        gw_str_xor(uint8_t* output, const uint8_t* input1, const uint8_t* input2, unsigned int len);
void        gw_sha1_str(const uint8_t* in, int in_len, uint8_t* out);
void        gw_sha1_2_str(const uint8_t* in, int in_len, const uint8_t* in2, int in2_len, uint8_t* out);
int         gw_getsockerrno(int fd);
char*       create_hex_sha1_sha1_passwd(char* passwd);

void  replace_whitespace(char* str);
char* squeeze_whitespace(char* str);
bool  strip_escape_chars(char*);

bool is_valid_posix_path(char* path);

char* remove_mysql_comments(const char** src,
                            const size_t* srcsize,
                            char** dest,
                            size_t* destsize);
char* replace_values(const char** src,
                     const size_t* srcsize,
                     char** dest,
                     size_t* destsize);
char* replace_literal(char* haystack,
                      const char* needle,
                      const char* replacement);
char* replace_quoted(const char** src, const size_t* srcsize, char** dest, size_t* destsize);

bool clean_up_pathname(char* path);

bool mxs_mkdir_all(const char* path, int mask);

/**
 * Return the number of processors
 *
 * @return Number of processors or 1 if the information is not available
 */
long get_processor_count();

/**
 * Return total system memory
 *
 * @return Total memory in bytes or 0 if the information is not available
 */
int64_t get_total_memory();

/**
 * Store a 4 byte integer
 *
 * @param ptr   Pointer where value is stored
 * @param value Value to store
 *
 * @return The next byte after the stored value
 */
static inline uint8_t* mxs_set_byte4(uint8_t* ptr, uint32_t value)
{
    *ptr++ = value;
    *ptr++ = (value >> 8);
    *ptr++ = (value >> 16);
    *ptr++ = (value >> 24);
    return ptr;
}

/**
 * Read a 4 byte integer
 *
 * @param ptr   Pointer where value is stored
 * @param value Value to store
 *
 * @return The next byte after the stored value
 */
static inline uint32_t mxs_get_byte4(const uint8_t* ptr)
{
    return ((uint32_t) ptr[0]) | ((uint32_t) ptr[1] << 8)
           | ((uint32_t) ptr[2] << 16) | ((uint32_t) ptr[3] << 24);
}

MXS_END_DECLS
