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
 * @file core/maxscale/secrets.h - MaxScale config file password encryption/decryption
 */

#include <maxscale/secrets.h>

MXS_BEGIN_DECLS

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

enum
{
    MXS_PASSWORD_MAXLEN = 79
};

int   secrets_write_keys(const char* directory);
char* encrypt_password(const char*, const char*);

MXS_END_DECLS
