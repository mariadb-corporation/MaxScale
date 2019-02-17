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

#include <maxscale/ccdefs.hh>
#include <maxscale/hint.h>

MXS_BEGIN_DECLS

/**
 * A named hint set.
 *
 * The hint "MaxScale name PREPARE ..." can be used to defined a named set
 * of hints that can be later applied.
 */
typedef struct namedhints
{
    char* name;     /*< Hintsets name */
    HINT* hints;
    struct namedhints
    * next;     /*< Next named hint */
} NAMEDHINTS;

/**
 * A session meaintains a stack of hints, the hints BEGIN and STOP are used
 * push hints on and off the stack. The current top of the stack is added to
 * any statement that does not explicitly define a hint for that signle
 * statement.
 */
typedef struct hintstack
{
    HINT* hint;
    struct hintstack
    * next;
} HINTSTACK;

/**
 * The hint instance structure
 */
typedef struct
{
    int sessions;
} HINT_INSTANCE;

/**
 * A hint parser session structure
 */
typedef struct
{
    MXS_DOWNSTREAM down;
    HINTSTACK*     stack;
    NAMEDHINTS*    named_hints;     /* The named hints defined in this session */
} HINT_SESSION;

NAMEDHINTS*  free_named_hint(NAMEDHINTS* named_hint);
HINTSTACK*   free_hint_stack(HINTSTACK* hint_stack);
void         process_hints(HINT_SESSION* session, GWBUF* buffer);

MXS_END_DECLS
