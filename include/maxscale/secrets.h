#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

/**
 * @file include/maxscale/secrets.h - MaxScale config file password decryption
 */

#include <maxscale/cdefs.h>

#include <sys/types.h>

MXS_BEGIN_DECLS

char *decrypt_password(const char *);

MXS_END_DECLS
