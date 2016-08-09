#ifndef _SECRETS_H
#define _SECRETS_H
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
