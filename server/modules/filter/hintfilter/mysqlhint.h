#pragma once
#ifndef _MYSQLHINT_H
#define _MYSQLHINT_H
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

/*
 * Revision History
 *
 * Date         Who             Description
 * 17-07-2014   Mark Riddoch    Initial implementation
 */

#include <maxscale/cdefs.h>
#include <maxscale/hint.h>

MXS_BEGIN_DECLS

/* Parser tokens for the hint parser */
typedef enum
{
    TOK_MAXSCALE = 1,
    TOK_PREPARE,
    TOK_START,
    TOK_STOP,
    TOK_EQUAL,
    TOK_STRING,
    TOK_ROUTE,
    TOK_TO,
    TOK_MASTER,
    TOK_SLAVE,
    TOK_SERVER,
    TOK_LINEBRK,
    TOK_END
} TOKEN_VALUE;

/* The tokenising return type */
typedef struct
{
    TOKEN_VALUE token;      // The token itself
    char        *value;     // The string version of the token
} HINT_TOKEN;

/**
 * A named hint set.
 *
 * The hint "MaxScale name PREPARE ..." can be used to defined a named set
 * of hints that can be later applied.
 */
typedef struct namedhints
{
    char        *name;  /*< Hintsets name */
    HINT        *hints;
    struct namedhints
        *next;  /*< Next named hint */
} NAMEDHINTS;

/**
 * A session meaintains a stack of hints, the hints BEGIN and STOP are used
 * push hints on and off the stack. The current top of the stack is added to
 * any statement that does not explicitly define a hint for that signle
 * statement.
 */
typedef struct hintstack
{
    HINT        *hint;
    struct hintstack
        *next;
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
    MXS_DOWNSTREAM  down;
    GWBUF          *request;
    int             query_len;
    HINTSTACK      *stack;
    NAMEDHINTS     *named_hints;   /* The named hints defined in this session */
} HINT_SESSION;

/* Some useful macros */
#define CURRENT_HINT(session)   ((session)->stack ? \
                    (session)->stack->hints : NULL)

/* Hint Parser State Machine */
#define HS_INIT         0
#define HS_ROUTE        1
#define HS_ROUTE1       2
#define HS_ROUTE_SERVER 3
#define HS_NAME         4
#define HS_PVALUE       5
#define HS_PREPARE      6

extern HINT *hint_parser(HINT_SESSION *session, GWBUF *request);
NAMEDHINTS* free_named_hint(NAMEDHINTS* named_hint);
HINTSTACK*  free_hint_stack(HINTSTACK* hint_stack);

MXS_END_DECLS

#endif
