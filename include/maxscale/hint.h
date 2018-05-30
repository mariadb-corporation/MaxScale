#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file hint.h The generic hint data that may be attached to buffers
 */

#include <maxscale/cdefs.h>
#include <maxscale/debug.h>

MXS_BEGIN_DECLS

/**
 * The types of hint that are supported by the generic hinting mechanism.
 */
typedef enum
{
    HINT_ROUTE_TO_MASTER = 1,
    HINT_ROUTE_TO_SLAVE,
    HINT_ROUTE_TO_NAMED_SERVER,
    HINT_ROUTE_TO_UPTODATE_SERVER, /*< not supported by RWSplit and HintRouter */
    HINT_ROUTE_TO_ALL, /*< not supported by RWSplit, supported by HintRouter */
    HINT_PARAMETER
} HINT_TYPE;

/**
 * A generic hint.
 *
 * A hint has a type associated with it and may optionally have hint
 * specific data.
 * Multiple hints may be attached to a single buffer.
 */
typedef struct hint
{
    HINT_TYPE       type;   /*< The Type of hint */
    void            *data;  /*< Type specific data */
    void            *value; /*< Parameter value for hint */
    unsigned int    dsize;  /*< Size of the hint data */
    struct hint     *next;  /*< Another hint for this buffer */
} HINT;

HINT *hint_alloc(HINT_TYPE, void *, unsigned int);
HINT *hint_create_parameter(HINT *, char *, const char *);
HINT *hint_create_route(HINT *, HINT_TYPE, const char *);
HINT *hint_splice(HINT *head, HINT *list);
void hint_free(HINT *);
HINT *hint_dup(const HINT *);
bool hint_exists(HINT **, HINT_TYPE);

MXS_END_DECLS
