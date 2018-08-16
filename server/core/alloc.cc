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

#include <maxscale/alloc.h>
#include <stdlib.h>
#include <maxscale/log.h>

/**
 * @brief Allocates memory; behaves exactly like `malloc`.
 *
 * Usually `mxs_malloc` is not used, but the macro `MXS_MALLOC` instead.
 *
 * @note The returned pointer can be passed to `mxs_realloc`, `mxs_realloc_a`
 *       and `mxs_free`.
 *
 * @param size The amount of memory to allocate.
 * @param caller The name of the function calling this function.
 * @return A pointer to the allocated memory.
 */
void *mxs_malloc(size_t size/*, const char *caller*/)
{
    void *ptr = malloc(size);

    if (!ptr)
    {
        //MXS_OOM_MESSAGE(caller);
        MXS_OOM();
    }

    return ptr;
}

/**
 * @brief Allocates memory; behaves exactly like `calloc`.
 *
 * Usually `mxs_calloc` is not used, but the macro `MXS_CALLOC` instead.
 *
 * @note The returned pointer can be passed to `mxs_realloc`, `mxs_realloc_a`
 *       and `mxs_free`.
 *
 * @param nmemb The number of elements.
 * @param size The size of each element.
 * @param caller The name of the function calling this function.
 * @return A pointer to the allocated memory.
 */
void *mxs_calloc(size_t nmemb, size_t size/*, const char *caller*/)
{
    void *ptr = calloc(nmemb, size);

    if (!ptr)
    {
        //MXS_OOM_MESSAGE(caller);
        MXS_OOM();
    }

    return ptr;
}

/**
 * @brief Re-allocates memory; behaves exactly like `realloc`.
 *
 * Usually `mxs_realloc` is not used, but the macro `MXS_REALLOC` instead.
 *
 * @note The returned pointer can be passed to `mxs_realloc`, `mxs_realloc_a`
 *       and `mxs_free`.
 *
 * @param ptr Pointer to memory earlier allocated by `mxs_malloc`,
 *            `mxs_calloc`, `mxs_realloc`, `mxs_strdup`, `mxs_strndup`
              or or their `_a` equivalents.
 * @param size What size the memory block should be changed to.
 * @param caller The name of the function calling this function.
 * @return A pointer to the allocated memory.
 */
void *mxs_realloc(void *ptr, size_t size/*, const char *caller*/)
{
    ptr = realloc(ptr, size);

    if (!ptr)
    {
        //MXS_OOM_MESSAGE(caller);
        MXS_OOM();
    }

    return ptr;
}

/**
 * @brief Duplicates a string; behaves exactly like `strdup`.
 *
 * Usually `mxs_strdup` is not used, but the macro `MXS_STRDUP` instead.
 *
 * @note The returned pointer can be passed to `mxs_realloc` and `mxs_free`.
 *
 * @param s1 The string to be duplicated.
 * @param caller The name of the function calling this function.
 * @return A copy of the string.
 */
char *mxs_strdup(const char *s1/*, const char *caller*/)
{
    char *s2 = strdup(s1);

    if (!s2)
    {
        //MXS_OOM_MESSAGE(caller);
        MXS_OOM();
    }

    return s2;
}

/**
 * @brief Duplicates a string; behaves exactly like `strndup`.
 *
 * Usually `mxs_strndup` is not used, but the macro `MXS_STRNDUP` instead.
 *
 * @note The returned pointer can be passed to `mxs_realloc` and `mxs_free`.
 *
 * @param s1 The string to be duplicated.
 * @param n At most n bytes should be copied.
 * @param caller The name of the function calling this function.
 * @return A copy of the string.
 */
char *mxs_strndup(const char *s1, size_t n/*, const char *caller*/)
{
    char *s2 = strndup(s1, n);

    if (!s2)
    {
        //MXS_OOM_MESSAGE(caller);
        MXS_OOM();
    }

    return s2;
}

/**
 * @brief Frees memory.
 *
 * Usually `mxs_free` is not used, but the macro `MXS_FREE` instead.
 *
 * @note The memory must have earlier been allocated with `mxs_malloc`,
 *       `mxs_calloc`, `mxs_realloc`, `mxs_strdup`, `mxs_strndup`, or
 *       their `_a` equivalents.
 *
 * @param ptr Pointer to the memory to be freed.
 * @param caller The name of the function calling this function.
 */
void mxs_free(void *ptr/*, const char *caller*/)
{
    free(ptr);
}

/**
 * @brief Duplicates a string.
 *
 * Behaves exactly like `strdup`, except that `mxs_strdup_a` _always_
 * returns a non-NULL pointer. In case `mxs_strdup_a` cannot do that,
 * it _as_ the process.
 *
 * Usually `mxs_strdup_a` is not used, but the macro `MXS_STRDUP_A`
 * instead.
 *
 * @note The returned pointer can be passed to `mxs_realloc` and `mxs_free`.
 *
 * @param s1 The string to be duplicated.
 * @param caller The name of the function calling this function.
 * @return A copy of the string.
 */
char *mxs_strdup_a(const char *s1/*, const char *caller*/)
{
    char *s2 = mxs_strdup(s1/*, caller*/);

    if (!s2)
    {
        abort();
    }

    return s2;
}

/**
 * @breif Duplicates a string.
 *
 * Behaves exactly like `strndup` except that `mxs_strndup_a` _always_
 * returns a non-NULL pointer. In case `mxs_strndup_a` cannot do that, it
 * _as_ the process.
 *
 * Usually `mxs_strndup_a` is not used, but the macro MXS_STRNDUP_A
 * instead.
 *
 * @note The returned pointer can be passed to `mxs_realloc` and `mxs_free`.
 *
 * @param s1 The string to be duplicated.
 * @param n At most n bytes should be copied.
 * @param caller The name of the function calling this function.
 * @return A copy of the string.
 */
char *mxs_strndup_a(const char *s1, size_t n/*, const char *caller*/)
{
    char *s2 = mxs_strndup(s1, n/*, caller*/);

    if (!s2)
    {
        abort();
    }

    return s2;
}
