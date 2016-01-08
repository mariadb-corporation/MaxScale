/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file maxinfo_parse.c - Parse the limited set of SQL that the MaxScale
 * information schema can use
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 16/02/15	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <service.h>
#include <session.h>
#include <router.h>
#include <modules.h>
#include <modinfo.h>
#include <modutil.h>
#include <atomic.h>
#include <spinlock.h>
#include <dcb.h>
#include <poll.h>
#include <maxinfo.h>
#include <skygw_utils.h>
#include <log_manager.h>

static MAXINFO_TREE *make_tree_node(MAXINFO_OPERATOR, char *, MAXINFO_TREE *, MAXINFO_TREE *);
static void free_tree(MAXINFO_TREE *);
static char *fetch_token(char *, int *, char **);
static MAXINFO_TREE *parse_column_list(char **sql);
static MAXINFO_TREE *parse_table_name(char **sql);
MAXINFO_TREE* maxinfo_parse_literals(MAXINFO_TREE *tree, int min_args, char *ptr,
                                     PARSE_ERROR *parse_error);

/**
 * Parse a SQL subset for the maxinfo plugin and return a parse tree
 *
 * @param sql		The SQL query
 * @return	Parse tree or NULL on error
 */
MAXINFO_TREE *
maxinfo_parse(char *sql, PARSE_ERROR *parse_error)
{
int		token;
char		*ptr, *text;
MAXINFO_TREE	*tree = NULL;
MAXINFO_TREE	*col, *table;

	*parse_error = PARSE_NOERROR;
	while ((ptr  = fetch_token(sql, &token, &text)) != NULL)
	{
		switch (token)
		{
		case	LT_SHOW:
			free(text);	// not needed
			ptr = fetch_token(ptr, &token, &text);
			if (ptr == NULL || token != LT_STRING)
			{
				// Expected show "name"
				*parse_error = PARSE_MALFORMED_SHOW;
				return NULL;
			}
			tree = make_tree_node(MAXOP_SHOW, text, NULL, NULL);
			if ((ptr = fetch_token(ptr, &token, &text)) == NULL)
				return tree;
			else if (token == LT_LIKE)
			{
				if ((ptr = fetch_token(ptr, &token, &text)) != NULL)
				{
					tree->right = make_tree_node(MAXOP_LIKE,
							text, NULL, NULL);
					return tree;
				}
				else
				{
					// Expected expression
					*parse_error = PARSE_EXPECTED_LIKE;
					free_tree(tree);
					return NULL;
				}
			}
			// Malformed show
			free(text);
			free_tree(tree);
			*parse_error = PARSE_MALFORMED_SHOW;
			return NULL;
#if 0
		case	LT_SELECT:
			free(text);	// not needed
			col = parse_column_list(&ptr);
			table = parse_table_name(&ptr);
			return make_tree_node(MAXOP_SELECT, NULL, col, table);
#endif
            case LT_FLUSH:
                free(text);	// not needed
                ptr = fetch_token(ptr, &token, &text);
                return make_tree_node(MAXOP_FLUSH, text, NULL, NULL);

            case LT_SHUTDOWN:
                free(text);
                ptr = fetch_token(ptr, &token, &text);
                tree = make_tree_node(MAXOP_SHUTDOWN, text, NULL, NULL);

                if ((ptr = fetch_token(ptr, &token, &text)) == NULL)
                {
                    /** Possibly SHUTDOWN MAXSCALE */
                    return tree;
                }
                tree->right = make_tree_node(MAXOP_LITERAL, text, NULL, NULL);

                if ((ptr = fetch_token(ptr, &token, &text)) != NULL)
                {
                    /** Unknown token after SHUTDOWN MONITOR|SERVICE */
                    *parse_error = PARSE_SYNTAX_ERROR;
                    free_tree(tree);
                    return NULL;
                }
                return tree;

            case LT_RESTART:
                free(text);
                ptr = fetch_token(ptr, &token, &text);
                tree = make_tree_node(MAXOP_RESTART, text, NULL, NULL);

                if ((ptr = fetch_token(ptr, &token, &text)) == NULL)
                {
                    /** Missing token for RESTART MONITOR|SERVICE */
                    *parse_error = PARSE_SYNTAX_ERROR;
                    free_tree(tree);
                    return NULL;
                }
                tree->right = make_tree_node(MAXOP_LITERAL, text, NULL, NULL);

                if ((ptr = fetch_token(ptr, &token, &text)) != NULL)
                {
                    /** Unknown token after RESTART MONITOR|SERVICE */
                    *parse_error = PARSE_SYNTAX_ERROR;
                    free(text);
                    free_tree(tree);
                    return NULL;
                }
                return tree;

            case LT_SET:
                free(text);	// not needed
                ptr = fetch_token(ptr, &token, &text);
                tree = make_tree_node(MAXOP_SET, text, NULL, NULL);
                return maxinfo_parse_literals(tree, 2, ptr, parse_error);

            case LT_CLEAR:
                free(text);	// not needed
                ptr = fetch_token(ptr, &token, &text);
                tree = make_tree_node(MAXOP_CLEAR, text, NULL, NULL);
                return maxinfo_parse_literals(tree, 2, ptr, parse_error);
                break;
		default:
			*parse_error = PARSE_SYNTAX_ERROR;
			return NULL;
		}
	}
	*parse_error = PARSE_SYNTAX_ERROR;
	return NULL;
}

/**
 * Parse a column list, may be a * or a valid list of string name
 * separated by a comma
 *
 * @param sql	Pointer to pointer to column list updated to point to the table name
 * @return	A tree of column names
 */
static MAXINFO_TREE *
parse_column_list(char **ptr)
{
int	token, lookahead;
char	*text, *text2;
MAXINFO_TREE	*tree = NULL;
MAXINFO_TREE * rval = NULL;
	*ptr = fetch_token(*ptr, &token, &text);
	*ptr = fetch_token(*ptr, &lookahead, &text2);
	switch (token)
	{
	case LT_STRING:
		switch (lookahead)
		{
		case LT_COMMA:
			rval = make_tree_node(MAXOP_COLUMNS, text, NULL,
				parse_column_list(ptr));
			break;
		case LT_FROM:
			rval = make_tree_node(MAXOP_COLUMNS, text, NULL,
				NULL);
			break;
		default:
		    break;
		}
		break;
	case LT_STAR:
		if (lookahead != LT_FROM)
			rval = make_tree_node(MAXOP_ALL_COLUMNS, NULL, NULL,
				NULL);
		break;
	default:
	    break;
	}
	free(text);
	free(text2);
	return rval;
}


/**
 * Parse a table name
 *
 * @param sql	Pointer to pointer to column list updated to point to the table name
 * @return	A tree of table names
 */
static MAXINFO_TREE *
parse_table_name(char **ptr)
{
int	token;
char	*text;
MAXINFO_TREE	*tree = NULL;

	*ptr = fetch_token(*ptr, &token, &text);
	if  (token == LT_STRING)
		return make_tree_node(MAXOP_TABLE, text, NULL, NULL);
	free(text);
	return NULL;
}

/**
 * Allocate and populate a parse tree node
 *
 * @param op	The node operator
 * @param value	The node value
 * @param left	The left branch of the parse tree
 * @param right	The right branch of the parse tree
 * @return The new parse tree node
 */
static MAXINFO_TREE *
make_tree_node(MAXINFO_OPERATOR op, char *value, MAXINFO_TREE *left, MAXINFO_TREE *right)
{
MAXINFO_TREE	*node;

	if ((node = (MAXINFO_TREE *)malloc(sizeof(MAXINFO_TREE))) == NULL)
		return NULL;
	node->op = op;
	node->value = value;
	node->left = left;
	node->right = right;

	return node;
}

/**
 * Recusrsively free the storage associated with a parse tree
 *
 * @param tree	The parse tree to free
 */
static void
free_tree(MAXINFO_TREE *tree)
{
	if (tree->left)
		free_tree(tree->left);
	if (tree->right)
		free_tree(tree->right);
	if (tree->value)
		free(tree->value);
	free(tree);
}

/**
 * The set of keywords known to the tokeniser
 */
static struct
{
    char *text;
    int token;
} keywords[] = {
    { "show",       LT_SHOW},
    { "select",     LT_SELECT},
    { "from",       LT_FROM},
    { "like",       LT_LIKE},
    { "=",          LT_EQUAL},
    { ",",          LT_COMMA},
    { "*",          LT_STAR},
    { "flush",      LT_FLUSH},
    { "set",        LT_SET},
    { "clear",      LT_CLEAR},
    { "shutdown",   LT_SHUTDOWN},
    { "restart",    LT_RESTART},
    { NULL, 0}
};

/**
 * Limited SQL tokeniser. Understands a limited set of key words and
 * quoted strings.
 *
 * @param sql	The SQL to tokenise
 * @param token	The returned token
 * @param text	The matching text
 * @return	The next position to tokenise from
 */
static char *
fetch_token(char *sql, int *token, char **text)
{
char	*s1, *s2, quote = '\0';
int	i;

	s1 = sql;
	while (*s1 && isspace(*s1))
	{
		s1++;
	}
	if (quote == '\0' && (*s1 == '\'' || *s1 == '\"'))
	{
		quote = *s1++;
	}
	if (*s1 == '/' && *(s1 + 1) == '*')
	{
		s1 += 2;
		// Skip the comment
		do {
			while (*s1 && *s1 != '*')
				s1++;
		} while (*(s1 + 1) && *(s1 + 1) != '/');
		s1 += 2;
		while (*s1 && isspace(*s1))
		{
			s1++;
		}
		if (quote == '\0' && (*s1 == '\'' || *s1 == '\"'))
		{
			quote = *s1++;
		}
	}
	s2 = s1;
	while (*s2)
	{
		if (quote == '\0' && (isspace(*s2)
				|| *s2 == ',' || *s2 == '='))
			break;
		else if (quote == *s2)
		{
			break;
		}
		s2++;
	}

	if (*s1 == '@' && *(s1 + 1) == '@')
	{
		*text = strndup(s1 + 2, (s2 - s1) - 2);
		*token = LT_VARIABLE;
		return s2;
	}

	if (s1 == s2)
	{
		*text = NULL;
		return NULL;
	}

	*text = strndup(s1, s2 - s1);
	for (i = 0; keywords[i].text; i++)
	{
		if (strcasecmp(keywords[i].text, *text) == 0)
		{
			*token = keywords[i].token;
			return s2;
		}
	}
	*token = LT_STRING;
	return s2;
}

/**
 * Parse the remaining arguments as literals.
 * @param tree Previous head of the parse tree
 * @param min_args Minimum required number of arguments
 * @param ptr Pointer to client command
 * @param parse_error Pointer to parsing error to fill
 * @return Parsed tree or NULL if parsing failed
 */
MAXINFO_TREE* maxinfo_parse_literals(MAXINFO_TREE *tree, int min_args, char *ptr,
                                      PARSE_ERROR *parse_error)
{
    int token;
    MAXINFO_TREE* node = tree;
    char *text;
    for(int i = 0; i < min_args; i++)
    {
        if((ptr = fetch_token(ptr, &token, &text)) == NULL ||
           (node->right = make_tree_node(MAXOP_LITERAL, text, NULL, NULL)) == NULL)
        {
            *parse_error = PARSE_SYNTAX_ERROR;
            free_tree(tree);
            if(ptr)
            {
                free(text);
            }
            return NULL;
        }
        node = node->right;
    }

    return tree;
}
