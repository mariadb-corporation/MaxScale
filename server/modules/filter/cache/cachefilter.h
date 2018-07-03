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
#include <limits.h>
#include "cache_storage_api.h"
#include "rules.h"


#define CACHE_DEBUG_NONE          0  /* 0b00000 */
#define CACHE_DEBUG_MATCHING      1  /* 0b00001 */
#define CACHE_DEBUG_NON_MATCHING  2  /* 0b00010 */
#define CACHE_DEBUG_USE           4  /* 0b00100 */
#define CACHE_DEBUG_NON_USE       8  /* 0b01000 */
#define CACHE_DEBUG_DECISIONS    16  /* 0b10000 */

#define CACHE_DEBUG_RULES        (CACHE_DEBUG_MATCHING | CACHE_DEBUG_NON_MATCHING)
#define CACHE_DEBUG_USAGE        (CACHE_DEBUG_USE | CACHE_DEBUG_NON_USE)
#define CACHE_DEBUG_MIN          CACHE_DEBUG_NONE
#define CACHE_DEBUG_MAX          (CACHE_DEBUG_RULES | CACHE_DEBUG_USAGE | CACHE_DEBUG_DECISIONS)

#if !defined(UINT32_MAX)
#define UINT32_MAX      (4294967295U)
#endif

#if !defined(UINT64_MAX)
#define UINT64_MAX      (18446744073709551615UL)
#endif

typedef enum cache_selects
{
    CACHE_SELECTS_ASSUME_CACHEABLE,
    CACHE_SELECTS_VERIFY_CACHEABLE,
} cache_selects_t;

// Count
#define CACHE_ZDEFAULT_MAX_RESULTSET_ROWS             "0"
// Bytes
#define CACHE_ZDEFAULT_MAX_RESULTSET_SIZE             "0"
// Seconds
#define CACHE_ZDEFAULT_HARD_TTL                       "0"
// Seconds
#define CACHE_ZDEFAULT_SOFT_TTL                       "0"
// Integer value
#define CACHE_ZDEFAULT_DEBUG                          "0"
// Positive integer
#define CACHE_ZDEFAULT_MAX_COUNT                      "0"
// Positive integer
#define CACHE_ZDEFAULT_MAX_SIZE                       "0"
// Thread model
#define CACHE_ZDEFAULT_THREAD_MODEL                   "thread_specific"
const cache_thread_model CACHE_DEFAULT_THREAD_MODEL = CACHE_THREAD_MODEL_ST;
// Cacheable selects
#define CACHE_ZDEFAULT_SELECTS                        "assume_cacheable"
const cache_selects_t CACHE_DEFAULT_SELECTS =         CACHE_SELECTS_ASSUME_CACHEABLE;
// Storage
#define CACHE_ZDEFAULT_STORAGE                        "storage_inmemory"
// Transaction behaviour
#define CACHE_ZDEFAULT_CACHE_IN_TRXS                  "all_transactions"
// Enabled
#define CACHE_ZDEFAULT_ENABLED                        "true"

typedef enum cache_in_trxs
{
    // Do NOT change the order. Code relies upon NEVER < READ_ONLY < ALL.
    CACHE_IN_TRXS_NEVER,
    CACHE_IN_TRXS_READ_ONLY,
    CACHE_IN_TRXS_ALL,
} cache_in_trxs_t;

typedef struct cache_config
{
    uint64_t max_resultset_rows;       /**< The maximum number of rows of a resultset for it to be cached. */
    uint64_t max_resultset_size;       /**< The maximum size of a resultset for it to be cached. */
    char* rules;                       /**< Name of rules file. */
    char* storage;                     /**< Name of storage module. */
    char* storage_options;             /**< Raw options for storage module. */
    char** storage_argv;               /**< Cooked options for storage module. */
    int storage_argc;                  /**< Number of cooked options. */
    uint32_t hard_ttl;                 /**< Hard time to live. */
    uint32_t soft_ttl;                 /**< Soft time to live. */
    uint64_t max_count;                /**< Maximum number of entries in the cache.*/
    uint64_t max_size;                 /**< Maximum size of the cache.*/
    uint32_t debug;                    /**< Debug settings. */
    cache_thread_model_t thread_model; /**< Thread model. */
    cache_selects_t selects;           /**< Assume/verify that selects are cacheable. */
    cache_in_trxs_t cache_in_trxs;     /**< To cache or not to cache inside transactions. */
    bool enabled;                      /**< Whether the cache is enabled or not. */
} CACHE_CONFIG;
