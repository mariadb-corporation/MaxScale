#ifndef _MAXSCALE_FILTER_CACHE_STORAGE_H
#define _MAXSCALE_FILTER_CACHE_STORAGE_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "cache_storage_api.h"

typedef struct cache_storage_module_t
{
    void* handle;
    CACHE_STORAGE_API* api;
} CACHE_STORAGE_MODULE;

CACHE_STORAGE_MODULE* cache_storage_open(const char *name);
void cache_storage_close(CACHE_STORAGE_MODULE *module);

#endif
