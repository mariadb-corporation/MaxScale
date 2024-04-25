/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>

/* NOTE: Do not use these functions directly, use the macros below. */
// "caller" arg temporarily disabled so that existing code
// using the previous version of mxs_alloc etc. will continue
// to compile.
void* mxb_malloc(size_t size    /*, const char *caller*/);
void* mxb_calloc(size_t nmemb, size_t size    /*, const char *caller*/);
void* mxb_realloc(void* ptr, size_t size    /*, const char *caller*/);
void  mxb_free(void* ptr    /*, const char *caller*/);

char* mxb_strdup(const char* s    /*, const char *caller*/);
char* mxb_strdup_a(const char* s    /*, const char *caller*/);

/*
 * NOTE: USE these macros instead of the functions above.
 */
#define MXB_MALLOC(size)        mxb_malloc(size    /*, __func__*/)
#define MXB_CALLOC(nmemb, size) mxb_calloc(nmemb, size    /*, __func__*/)
#define MXB_REALLOC(ptr, size)  mxb_realloc(ptr, size    /*, __func__*/)
#define MXB_FREE(ptr)           mxb_free(ptr    /*, __func__*/)

#define MXB_STRDUP(s)     mxb_strdup(s    /*, __func__*/)
#define MXB_STRDUP_A(s)     mxb_strdup_a(s    /*, __func__*/)

/**
 * @brief Abort the process if the pointer is NULL.
 *
 * To be used in circumstances where a memory allocation failure
 * cannot - currently - be dealt with properly.
 */
#define MXB_ABORT_IF_NULL(p) do {if (!p) {abort();}} while (false)
