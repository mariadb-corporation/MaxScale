/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/alloc.hh>
#include <stdlib.h>
#include <string.h>
#include <maxbase/log.hh>

/**
 * @brief Allocates memory; behaves exactly like `malloc`.
 *
 * Usually `mxb_malloc` is not used, but the macro `MXB_MALLOC` instead.
 *
 * @note The returned pointer can be passed to `mxb_realloc`, `mxb_realloc_a`
 *       and `mxb_free`.
 *
 * @param size The amount of memory to allocate.
 * @param caller The name of the function calling this function.
 * @return A pointer to the allocated memory.
 */
void* mxb_malloc(size_t size    /*, const char *caller*/)
{
    void* ptr = malloc(size);
    if (!ptr)
    {
        // MXB_OOM_MESSAGE(caller);
        MXB_OOM();
    }
    return ptr;
}

/**
 * @brief Allocates memory; behaves exactly like `calloc`.
 *
 * Usually `mxb_calloc` is not used, but the macro `MXB_CALLOC` instead.
 *
 * @note The returned pointer can be passed to `mxb_realloc`, `mxb_realloc_a`
 *       and `mxb_free`.
 *
 * @param nmemb The number of elements.
 * @param size The size of each element.
 * @param caller The name of the function calling this function.
 * @return A pointer to the allocated memory.
 */
void* mxb_calloc(size_t nmemb, size_t size    /*, const char *caller*/)
{
    void* ptr = calloc(nmemb, size);
    if (!ptr)
    {
        // MXB_OOM_MESSAGE(caller);
        MXB_OOM();
    }
    return ptr;
}

/**
 * @brief Re-allocates memory; behaves exactly like `realloc`.
 *
 * Usually `mxb_realloc` is not used, but the macro `MXB_REALLOC` instead.
 *
 * @note The returned pointer can be passed to `mxb_realloc`, `mxb_realloc_a`
 *       and `mxb_free`.
 *
 * @param ptr Pointer to memory earlier allocated by `mxb_malloc`,
 *            `mxb_calloc`, `mxb_realloc`, `mxb_strdup`, `mxb_strndup`
 *             or or their `_a` equivalents.
 * @param size What size the memory block should be changed to.
 * @param caller The name of the function calling this function.
 * @return A pointer to the allocated memory.
 */
void* mxb_realloc(void* ptr, size_t size    /*, const char *caller*/)
{
    ptr = realloc(ptr, size);
    if (!ptr)
    {
        // MXB_OOM_MESSAGE(caller);
        MXB_OOM();
    }
    return ptr;
}

/**
 * @brief Duplicates a string; behaves exactly like `strdup`.
 *
 * Usually `mxb_strdup` is not used, but the macro `MXB_STRDUP` instead.
 *
 * @note The returned pointer can be passed to `mxb_realloc` and `mxb_free`.
 *
 * @param s1 The string to be duplicated.
 * @param caller The name of the function calling this function.
 * @return A copy of the string.
 */
char* mxb_strdup(const char* s1    /*, const char *caller*/)
{
    char* s2 = strdup(s1);
    if (!s2)
    {
        // MXB_OOM_MESSAGE(caller);
        MXB_OOM();
    }
    return s2;
}

/**
 * @brief Duplicates a string; behaves exactly like `strndup`.
 *
 * Usually `mxb_strndup` is not used, but the macro `MXB_STRNDUP` instead.
 *
 * @note The returned pointer can be passed to `mxb_realloc` and `mxb_free`.
 *
 * @param s1 The string to be duplicated.
 * @param n At most n bytes should be copied.
 * @param caller The name of the function calling this function.
 * @return A copy of the string.
 */
char* mxb_strndup(const char* s1, size_t n    /*, const char *caller*/)
{
    char* s2 = strndup(s1, n);
    if (!s2)
    {
        // MXB_OOM_MESSAGE(caller);
        MXB_OOM();
    }
    return s2;
}

/**
 * @brief Frees memory.
 *
 * Usually `mxb_free` is not used, but the macro `MXB_FREE` instead.
 *
 * @note The memory must have earlier been allocated with `mxb_malloc`,
 *       `mxb_calloc`, `mxb_realloc`, `mxb_strdup`, `mxb_strndup`, or
 *       their `_a` equivalents.
 *
 * @param ptr Pointer to the memory to be freed.
 * @param caller The name of the function calling this function.
 */
void mxb_free(void* ptr    /*, const char *caller*/)
{
    free(ptr);
}

/**
 * @brief Duplicates a string.
 *
 * Behaves exactly like `strdup`, except that `mxb_strdup_a` _always_
 * returns a non-NULL pointer. In case `mxb_strdup_a` cannot do that,
 * it _as_ the process.
 *
 * Usually `mxb_strdup_a` is not used, but the macro `MXB_STRDUP_A`
 * instead.
 *
 * @note The returned pointer can be passed to `mxb_realloc` and `mxb_free`.
 *
 * @param s1 The string to be duplicated.
 * @param caller The name of the function calling this function.
 * @return A copy of the string.
 */
char* mxb_strdup_a(const char* s1    /*, const char *caller*/)
{
    char* s2 = mxb_strdup(s1    /*, caller*/);
    if (!s2)
    {
        abort();
    }
    return s2;
}

/**
 * @brief Duplicates a string.
 *
 * Behaves exactly like `strndup` except that `mxb_strndup_a` _always_
 * returns a non-NULL pointer. In case `mxb_strndup_a` cannot do that, it
 * _as_ the process.
 *
 * Usually `mxb_strndup_a` is not used, but the macro MXB_STRNDUP_A
 * instead.
 *
 * @note The returned pointer can be passed to `mxb_realloc` and `mxb_free`.
 *
 * @param s1 The string to be duplicated.
 * @param n At most n bytes should be copied.
 * @param caller The name of the function calling this function.
 * @return A copy of the string.
 */
char* mxb_strndup_a(const char* s1, size_t n    /*, const char *caller*/)
{
    char* s2 = mxb_strndup(s1, n    /*, caller*/);
    if (!s2)
    {
        abort();
    }
    return s2;
}
