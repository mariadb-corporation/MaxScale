#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file utils.h Utility functions headers
 */

#include <maxscale/cdefs.h>
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <netinet/in.h>

MXS_BEGIN_DECLS

#define CALCLEN(i) ((size_t)(floor(log10(abs(i))) + 1))
#define UINTLEN(i) (i<10 ? 1 : (i<100 ? 2 : (i<1000 ? 3 : CALCLEN(i))))

#define MXS_ARRAY_NELEMS(array) ((size_t)(sizeof(array)/sizeof(array[0])))

/** Macro for safe pointer arithmetic on void pointers
 * @param a The void pointer
 * @param b The offset into @c a
 */
#define MXS_PTR(a, b) (((uint8_t*)(a)) + (b))

bool utils_init(); /*< Call this first before using any other function */
void utils_end();

/**
 * Parse the bind configuration data.
 *
 * The configuration is passed as string in the `address|port` format.
 *
 * @param config        The bind address and port separated by a '|'
 * @param addr          The sockaddr_in6 in which the data is written
 * @return              1 on success, 0 on failure
 */
int  parse_bindconfig(const char *, struct sockaddr_in6 *, int *);


/**
 * @brief Create a network socket and a socket configuration
 *
 * @param dest Pointer to a struct sockaddr_storage where the configuration is stored
 * @param host The target host for which the socket is created
 *
 * @return The opened socket or -1 on failure
 */int create_network_socket(struct sockaddr_storage *, char *);

int setnonblocking(int fd);
char  *gw_strend(register const char *s);
static char gw_randomchar();
int gw_generate_random_str(char *output, int len);
int gw_hex2bin(uint8_t *out, const char *in, unsigned int len);
char *gw_bin2hex(char *out, const uint8_t *in, unsigned int len);
void gw_str_xor(uint8_t *output, const uint8_t *input1, const uint8_t *input2, unsigned int len);
void gw_sha1_str(const uint8_t *in, int in_len, uint8_t *out);
void gw_sha1_2_str(const uint8_t *in, int in_len, const uint8_t *in2, int in2_len, uint8_t *out);
int gw_getsockerrno(int fd);
char *create_hex_sha1_sha1_passwd(char *passwd);

char* trim(char *str);
char* squeeze_whitespace(char* str);
bool strip_escape_chars(char*);

bool is_valid_posix_path(char* path);

char* remove_mysql_comments(const char** src, const size_t* srcsize, char** dest,
                            size_t* destsize);
char* replace_values(const char** src, const size_t* srcsize, char** dest,
                     size_t* destsize);
char* replace_literal(char* haystack,
                      const char* needle,
                      const char* replacement);
char* replace_quoted(const char** src, const size_t* srcsize, char** dest, size_t* destsize);

bool clean_up_pathname(char *path);

bool mxs_mkdir_all(const char *path, int mask);

long get_processor_count();

MXS_END_DECLS
