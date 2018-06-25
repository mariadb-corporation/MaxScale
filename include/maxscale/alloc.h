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

#include <maxscale/cdefs.h>
#include <stdlib.h>
#include <string.h>

MXS_BEGIN_DECLS

/*
 * NOTE: Do not use these functions directly, use the macros below.
 */

// "caller" arg temporarily disabled so that existing code
// using the previous version of mxs_alloc etc. will continue
// to compile.
void *mxs_malloc(size_t size/*, const char *caller*/);
void *mxs_calloc(size_t nmemb, size_t size/*, const char *caller*/);
void *mxs_realloc(void *ptr, size_t size/*, const char *caller*/);
void mxs_free(void *ptr/*, const char *caller*/);

char *mxs_strdup(const char *s/*, const char *caller*/);
char *mxs_strndup(const char *s, size_t n/*, const char *caller*/);

char *mxs_strdup_a(const char *s/*, const char *caller*/);
char *mxs_strndup_a(const char *s, size_t n/*, const char *caller*/);


/*
 * NOTE: USE these macros instead of the functions above.
 */
#define MXS_MALLOC(size)         mxs_malloc(size/*, __func__*/)
#define MXS_CALLOC(nmemb, size)  mxs_calloc(nmemb, size/*, __func__*/)
#define MXS_REALLOC(ptr, size)   mxs_realloc(ptr, size/*, __func__*/)
#define MXS_FREE(ptr)            mxs_free(ptr/*, __func__*/)

#define MXS_STRDUP(s)            mxs_strdup(s/*, __func__*/)
#define MXS_STRNDUP(s, n)        mxs_strndup(s, n/*, __func__*/)

#define MXS_STRDUP_A(s)          mxs_strdup_a(s/*, __func__*/)
#define MXS_STRNDUP_A(s, n)      mxs_strndup_a(s, n/*, __func__*/)


/**
 * @brief Abort the process if the pointer is NULL.
 *
 * To be used in circumstances where a memory allocation failure
 * cannot - currently - be dealt with properly.
 */
#define MXS_ABORT_IF_NULL(p) do { if (!p) { abort(); } } while (false)

/**
 * @brief Abort the process if the provided value is non-zero.
 *
 * To be used in circumstances where a memory allocation or other
 * fatal error cannot - currently - be dealt with properly.
 */
#define MXS_ABORT_IF_TRUE(b) do { if (b) { abort(); } } while (false)

/**
 * @brief Abort the process if the provided value is zero.
 *
 * To be used in circumstances where a memory allocation or other
 * fatal error cannot - currently - be dealt with properly.
 */
#define MXS_ABORT_IF_FALSE(b) do { if (!b) { abort(); } } while (false)

MXS_END_DECLS
