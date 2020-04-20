/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-04-23
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXS_MODULE_NAME "csmon"

#include <maxscale/ccdefs.hh>
#include <memory>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <maxscale/json_api.hh>
#include <maxscale/monitor.hh>

#if defined(SS_DEBUG)
// This will expose the begin|commit|rollback as module call commands. Only
// intended for debugging and testing.
#define CSMON_EXPOSE_TRANSACTIONS
#endif

// Since the macro below obviously is expanded in place, the if will cause an
// "...will never be NULL" error if ppJson is a local variable.
inline bool cs_is_not_null_workaround(json_t** ppJson)
{
    return ppJson != nullptr;
}

/** Utility macros for printing both MXS_ERROR and json error */
#define PRINT_MXS_JSON_ERROR(ppJson, zFormat, ...) \
    do { \
        MXS_ERROR(zFormat, ##__VA_ARGS__); \
        if (cs_is_not_null_workaround(ppJson))  \
        { \
            *ppJson = mxs_json_error_append(*ppJson, zFormat, ##__VA_ARGS__); \
        } \
    } while (false)

namespace std
{

template<>
struct default_delete<xmlDoc>
{
    void operator()(xmlDocPtr pDoc)
    {
        xmlFreeDoc(pDoc);
    }
};
}
