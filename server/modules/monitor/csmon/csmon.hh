/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
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

/** Utility macros for printing both MXS_ERROR and json error */
#define PRINT_MXS_JSON_ERROR(pJson, zFormat, ...) \
    do { \
        MXS_ERROR(zFormat, ##__VA_ARGS__); \
        if (pJson) \
        { \
            *pJson = mxs_json_error_append(*pJson, zFormat, ##__VA_ARGS__); \
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
