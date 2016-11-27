#pragma once
#ifndef _MAXSCALE_FILTER_CACHE_CACHE_H
#define _MAXSCALE_FILTER_CACHE_CACHE_H
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

#include <maxscale/cdefs.h>
#include <limits.h>
#include <exception>
#include <maxscale/hashtable.h>
#include <maxscale/spinlock.h>
#include "rules.h"
#include "cache_storage_api.h"

class Storage;
class StorageFactory;

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

// Count
#define CACHE_DEFAULT_MAX_RESULTSET_ROWS UINT_MAX
// Bytes
#define CACHE_DEFAULT_MAX_RESULTSET_SIZE 64 * 1024
// Seconds
#define CACHE_DEFAULT_TTL                10
// Integer value
#define CACHE_DEFAULT_DEBUG              0
// Thread model
#define CACHE_DEFAULT_THREAD_MODEL       CACHE_THREAD_MODEL_MT

typedef struct cache_config
{
    uint32_t max_resultset_rows;       /**< The maximum number of rows of a resultset for it to be cached. */
    uint32_t max_resultset_size;       /**< The maximum size of a resultset for it to be cached. */
    char* rules;                       /**< Name of rules file. */
    char* storage;                     /**< Name of storage module. */
    char* storage_options;             /**< Raw options for storage module. */
    char** storage_argv;               /**< Cooked options for storage module. */
    int storage_argc;                  /**< Number of cooked options. */
    uint32_t ttl;                      /**< Time to live. */
    uint32_t debug;                    /**< Debug settings. */
    cache_thread_model_t thread_model; /**< Thread model. */
} CACHE_CONFIG;

void cache_config_finish(CACHE_CONFIG& config);
void cache_config_free(CACHE_CONFIG* pConfig);
void cache_config_reset(CACHE_CONFIG& config);

#define CPP_GUARD(statement)\
    do { try { statement; }                                              \
    catch (const std::exception& x) { MXS_ERROR("Caught standard exception: %s", x.what()); }\
    catch (...) { MXS_ERROR("Caught unknown exception."); } } while (false)

#endif
