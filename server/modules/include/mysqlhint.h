#ifndef _MYSQLHINT_H
#define _MYSQLHINT_H
/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright SkySQL Ab 2013
 */

/*
 * Revision History
 *
 * Date		Who		Description
 * 17-07-2014	Mark Riddoch	Initial implementation
 */
#include <hint.h>

/* Parser tokens for the hint parser */
typedef enum {
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
	TOK_EOL
} TOKEN_VALUE;

/* The tokenising return type */
typedef struct {
	TOKEN_VALUE	token;		// The token itself
	char		*value;		// The string version of the token
} HINT_TOKEN;

/**
 * A named hint set.
 *
 * The hint "MaxScale name PREPARE ..." can be used to defined a named set
 * of hints that can be later applied.
 */
typedef struct namedhints {
	char		*name;	/*< Hintsets name */
	HINT		*hints;
	struct namedhints
			*next;	/*< Next named hint */
} NAMEDHINTS;

/**
 * A session meaintains a stack of hints, the hints BEGIN and STOP are used
 * push hints on and off the stack. The current top of the stack is added to
 * any statement that does not explicitly define a hint for that signle
 * statement.
 */
typedef struct hintstack {
	HINT		*hint;
	struct hintstack
			*next;
} HINTSTACK;

/**
 * The hint instance structure
 */
typedef struct {
	int	sessions;
} HINT_INSTANCE;

/**
 * A hint parser session structure
 */
typedef struct {
	DOWNSTREAM	down;
	GWBUF		*request;
	int		query_len;
	HINTSTACK	*stack;
	NAMEDHINTS	*named_hints;	/* The named hints defined in this session */
} HINT_SESSION;

/* Some useful macros */
#define	CURRENT_HINT(session)	((session)->stack ? \
					(session)->stack->hints : NULL)

/* Hint Parser State Machine */
#define	HS_INIT		0
#define	HS_ROUTE	1
#define	HS_ROUTE1	2
#define	HS_ROUTE_SERVER	3
#define	HS_NAME		4
#define	HS_PVALUE	5
#define	HS_PREPARE	6


extern HINT *hint_parser(HINT_SESSION *session, GWBUF *request);

#endif
