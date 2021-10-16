/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file hint.h The generic hint data that may be attached to buffers
 */

#include <maxscale/ccdefs.hh>

enum HINT_TYPE
{
    HINT_NONE = 0,
    HINT_ROUTE_TO_MASTER,
    HINT_ROUTE_TO_SLAVE,
    HINT_ROUTE_TO_NAMED_SERVER,
    HINT_ROUTE_TO_UPTODATE_SERVER,  /**< not supported by RWSplit and HintRouter */
    HINT_ROUTE_TO_ALL,              /**< not supported by RWSplit, supported by HintRouter */
    HINT_ROUTE_TO_LAST_USED,
    HINT_PARAMETER,
};

const char* STRHINTTYPE(HINT_TYPE t);

/**
 * A generic hint.
 *
 * A hint has a type associated with it and may optionally have hint
 * specific data.
 * Multiple hints may be attached to a single buffer.
 */
struct HINT
{
    HINT_TYPE type {HINT_NONE}; /*< The Type of hint */
    void*     data {nullptr};   /*< Type specific data */
    void*     value {nullptr};  /*< Parameter value for hint */
    HINT*     next {nullptr};   /*< Another hint for this buffer */
};

HINT* hint_create_parameter(HINT*, const char*, const char*);
HINT* hint_create_route(HINT*, HINT_TYPE, const char*);
HINT* hint_splice(HINT* head, HINT* list);
void  hint_free(HINT*);
HINT* hint_dup(const HINT*);
bool hint_exists(HINT * *, HINT_TYPE);
