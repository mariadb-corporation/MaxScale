#ifndef _UTILS_H
#define _UTILS_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file utils.h Utility functions headers
 *
 * @verbatim
 * Revision History
 *
 * Date     Who                 Description
 * 22/03/16 Martin Brampton     Initial implementation
 *
 * @endverbatim
 */

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

#endif
