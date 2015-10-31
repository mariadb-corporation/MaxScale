#ifndef _MAXINFO_H
#define _MAXINFO_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
 */
#include <service.h>
#include <session.h>
#include <spinlock.h>

/**
 * @file maxinfo.h The MaxScale information schema provider
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 16/02/15	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
struct maxinfo_session;

/**
 * The INFO_INSTANCE structure. There is one instane of the maxinfo "router" for
 * each service that uses the MaxScale information schema.
 */
typedef struct maxinfo_instance {
	SPINLOCK	lock;		/*< The instance spinlock */
	SERVICE		*service;	/*< The debug cli service */
	struct maxinfo_session
			*sessions;	/*< Linked list of sessions within this instance */
	struct maxinfo_instance
			*next;		/*< The next pointer for the list of instances */
} INFO_INSTANCE;

/**
 * The INFO_SESSION structure. As INFO_SESSION is created for each user that logs into
 * the MaxScale information schema.
 */

typedef struct maxinfo_session {
	SESSION		*session;	/*< The MaxScale session */
	DCB		*dcb;		/*< DCB of the client side */
	GWBUF		*queue;		/*< Queue for building contiguous requests */
	struct maxinfo_session
			*next;		/*< The next pointer for the list of sessions */
} INFO_SESSION;

/**
 * The operators that can be in the parse tree
 */
typedef enum
{
	MAXOP_SHOW,
	MAXOP_SELECT,
	MAXOP_TABLE,
	MAXOP_COLUMNS,
	MAXOP_ALL_COLUMNS,
	MAXOP_LITERAL,
	MAXOP_PREDICATE,
	MAXOP_LIKE,
	MAXOP_EQUAL,
    MAXOP_FLUSH,
    MAXOP_SET,
    MAXOP_CLEAR,
    MAXOP_SHUTDOWN,
    MAXOP_RESTART
} MAXINFO_OPERATOR;

/**
 * The Parse tree nodes for the MaxInfo parser
 */
typedef struct maxinfo_tree {
	MAXINFO_OPERATOR	op;		/*< The operator */
	char			*value;		/*< The value */
	struct maxinfo_tree	*left;		/*< The left hand side of the operator */
	struct maxinfo_tree	*right;		/*< The right hand side of the operator */
} MAXINFO_TREE;



#define MYSQL_COMMAND(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 4))
/**
 * MySQL protocol OpCodes needed for replication
 */
#define	COM_QUIT				0x01
#define	COM_QUERY				0x03
#define	COM_STATISTICS				0x09
#define	COM_PING				0x0e

/**
 * Token values for the tokeniser used by the parser for maxinfo
 */
#define	LT_STRING		1
#define	LT_SHOW			2
#define LT_LIKE			3
#define LT_SELECT		4
#define LT_EQUAL		5
#define LT_COMMA		6
#define LT_FROM			7
#define LT_STAR			8
#define LT_VARIABLE		9
#define LT_FLUSH        10
#define LT_SET          11
#define LT_CLEAR        12
#define LT_SHUTDOWN     13
#define LT_RESTART      14


/**
 * Possible parse errors
 */
typedef enum  {
	PARSE_NOERROR,
	PARSE_MALFORMED_SHOW,
	PARSE_EXPECTED_LIKE,
	PARSE_SYNTAX_ERROR
} PARSE_ERROR;


extern MAXINFO_TREE	*maxinfo_parse(char *, PARSE_ERROR *);
extern void		maxinfo_execute(DCB *, MAXINFO_TREE *);
extern void 		maxinfo_send_error(DCB *, int, char  *);
extern void 		maxinfo_send_parse_error(DCB *, char *, PARSE_ERROR);
extern void		maxinfo_send_error(DCB *, int, char  *);
extern RESULTSET	*maxinfo_variables();
extern RESULTSET	*maxinfo_status();
#endif
