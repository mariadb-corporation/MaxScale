/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXS_MODULE_NAME "csmon"

#include <maxscale/ccdefs.hh>
#include <memory>
#include <maxscale/json_api.hh>
#include <maxscale/monitor.hh>
#include "csxml.hh"

#if defined(SS_DEBUG)
// This will expose the begin|commit|rollback as module call commands. Only
// intended for debugging and testing.
#define CSMON_EXPOSE_TRANSACTIONS
#endif

#define DEBUG_CSMON
//#undef DEBUG_CSMON

#if defined(DEBUG_CSMON)
#define CS_DEBUG(format, ...) MXS_NOTICE(format, ##__VA_ARGS__)
#else
#define CS_DEBUG(format, ...)
#endif

// Since the macro below obviously is expanded in place, the if will cause an
// "...will never be NULL" error if ppJson is a local variable.
inline bool cs_is_not_null_workaround(json_t** ppJson)
{
    return ppJson != nullptr;
}

/** Utility macros for printing both MXS_ERROR and json error */
#define LOG_APPEND_JSON_ERROR(ppJson, zFormat, ...) \
    do { \
        MXS_ERROR(zFormat, ##__VA_ARGS__); \
        if (cs_is_not_null_workaround(ppJson))  \
        { \
            *ppJson = mxs_json_error_append(*ppJson, zFormat, ##__VA_ARGS__); \
        } \
    } while (false)

#define LOG_PREPEND_JSON_ERROR(ppJson, zFormat, ...) \
    do { \
        MXS_ERROR(zFormat, ##__VA_ARGS__); \
        if (cs_is_not_null_workaround(ppJson))  \
        { \
            *ppJson = mxs_json_error_push_front_new(*ppJson, mxs_json_error(zFormat, ##__VA_ARGS__)); \
        } \
    } while (false)

namespace csmon
{
namespace keys
{
const char SUCCESS[] = "success";
const char MESSAGE[] = "message";
const char RESULT[]  = "result";
const char SERVERS[] = "servers";
}
}
