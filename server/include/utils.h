#ifndef _UTILS_H
#define _UTILS_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/**
 * @file utils.h Utility functions headers
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 22/03/16	Martin Brampton 	Initial implementation
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