#ifndef _SECRETS_H
#define _SECRETS_H
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
 * @file secrets.h
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 23/06/2013   Massimiliano Pinto      Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <openssl/aes.h>

#define MAXSCALE_KEYLEN 32
#define MAXSCALE_IV_LEN 16

/**
 * The key structure held in the secrets file
 */
typedef struct maxkeys
{
    unsigned char enckey[MAXSCALE_KEYLEN];
    unsigned char initvector[MAXSCALE_IV_LEN];
} MAXKEYS;

extern int  secrets_writeKeys(const char *filename);
extern char *decryptPassword(const char *);
extern char *encryptPassword(const char*, const char *);

#endif
