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

#define MXS_MODULE_NAME "hintfilter"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <maxscale/log.h>
#include <maxscale/filter.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include "mysqlhint.h"
#include <maxscale/alloc.h>

/**
 * hintparser.c - Find any comment in the SQL packet and look for MAXSCALE
 * hints in that comment.
 */

/**
 * The keywords in the hint syntax
 */
static struct
{
    const char *keyword;
    TOKEN_VALUE token;
} keywords[] =
{
    { "maxscale", TOK_MAXSCALE},
    { "prepare", TOK_PREPARE},
    { "start", TOK_START},
    { "begin", TOK_START},
    { "stop", TOK_STOP},
    { "end", TOK_STOP},
    { "=", TOK_EQUAL},
    { "route", TOK_ROUTE},
    { "to", TOK_TO},
    { "master", TOK_MASTER},
    { "slave", TOK_SLAVE},
    { "server", TOK_SERVER},
    { "last" , TOK_LAST},
    { NULL, static_cast<TOKEN_VALUE>(0)}
};

static HINT_TOKEN *hint_next_token(GWBUF **buf, char **ptr);
static void hint_pop(HINT_SESSION *);
static HINT *lookup_named_hint(HINT_SESSION *, char *);
static void create_named_hint(HINT_SESSION *, char *, HINT *);
static void hint_push(HINT_SESSION *, HINT *);
static const char* token_get_keyword(HINT_TOKEN* token);
static void token_free(HINT_TOKEN* token);

typedef enum
{
    HM_EXECUTE, HM_START, HM_PREPARE
} HINT_MODE;

void token_free(HINT_TOKEN* token)
{
    if (token->value != NULL)
    {
        MXS_FREE(token->value);
    }
    MXS_FREE(token);
}

static const char* token_get_keyword(
    HINT_TOKEN* token)
{
    switch (token->token)
    {
    case TOK_END:
        return "End of hint";
        break;

    case TOK_LINEBRK:
        return "End of line";
        break;

    case TOK_STRING:
        return token->value;
        break;

    default:
        {
            int i = 0;
            while (i < TOK_LINEBRK && keywords[i].token != token->token)
            {
                i++;
            }

            ss_dassert(i != TOK_LINEBRK);

            if (i == TOK_LINEBRK)
            {
                return "Unknown token";
            }
            else
            {
                return keywords[i].keyword;
            }
        }
        break;
    }
}

/**
 * Parse the hint comments in the MySQL statement passed in request.
 * Add any hints to the buffer for later processing.
 *
 * @param session   The filter session
 * @param request   The MySQL request buffer
 * @return      The hints parsed in this statement or active on the
 *          stack
 */
HINT *
hint_parser(HINT_SESSION *session, GWBUF *request)
{
    char *ptr, lastch = ' ';
    int len, residual, state;
    int found, escape, quoted, squoted;
    HINT *rval = NULL;
    char *pname, *lvalue, *hintname = NULL;
    GWBUF *buf;
    HINT_TOKEN *tok;
    HINT_MODE mode = HM_EXECUTE;
    bool multiline_comment = false;
    /* First look for any comment in the SQL */
    modutil_MySQL_Query(request, &ptr, &len, &residual);
    buf = request;
    found = 0;
    escape = 0;
    quoted = 0;
    squoted = 0;
    do
    {
        while (len--)
        {
            if (*ptr == '\\')
            {
                escape = 1;
            }
            else if (*ptr == '\"' && quoted)
            {
                quoted = 0;
            }
            else if (*ptr == '\"' && quoted == 0)
            {
                quoted = 0;
            }
            else if (*ptr == '\'' && squoted)
            {
                squoted = 0;
            }
            else if (*ptr == '\"' && squoted == 0)
            {
                squoted = 0;
            }
            else if (quoted || squoted)
            {
                ;
            }
            else if (escape)
            {
                escape = 0;
            }
            else if (*ptr == '#')
            {
                found = 1;
                break;
            }
            else if (*ptr == '/')
            {
                lastch = '/';
            }
            else if (*ptr == '*' && lastch == '/')
            {
                found = 1;
                multiline_comment = true;
                break;
            }
            else if (*ptr == '-' && lastch == '-')
            {
                found = 1;
                break;
            }
            else if (*ptr == '-')
            {
                lastch = '-';
            }
            else
            {
                lastch = *ptr;
            }
            ptr++;
        }
        if (found)
        {
            break;
        }

        buf = buf->next;
        if (buf)
        {
            len = GWBUF_LENGTH(buf);
            ptr = (char*)GWBUF_DATA(buf);
        }
    }
    while (buf);

    if (!found) /* No comment so we need do no more */
    {
        goto retblock;
    }

    /*
     * If we have got here then we have a comment, ptr point to
     * the comment character if it is a '#' comment or the second
     * character of the comment if it is a -- or \/\* comment
     *
     * Move to the next character in the SQL.
     */
    ptr++;
    if (ptr > (char *)(buf->end))
    {
        buf = buf->next;
        if (buf)
        {
            ptr = (char*)GWBUF_DATA(buf);
        }
        else
        {
            goto retblock;
        }
    }

    tok = hint_next_token(&buf, &ptr);

    if (tok == NULL)
    {
        goto retblock;
    }

    /** This is not MaxScale hint because it doesn't start with 'maxscale' */
    if (tok->token != TOK_MAXSCALE)
    {
        token_free(tok);
        goto retblock;
    }
    token_free(tok);

    state = HS_INIT;

    while (((tok = hint_next_token(&buf, &ptr)) != NULL) &&
           (tok->token != TOK_END))
    {
        if (tok->token == TOK_LINEBRK)
        {
            if (multiline_comment)
            {
                // Skip token
                token_free(tok);
                continue;
            }
            else
            {
                // Treat as TOK_END
                tok->token = TOK_END;
                break;
            }
        }
        switch (state)
        {
        case HS_INIT:
            switch (tok->token)
            {
            case TOK_ROUTE:
                state = HS_ROUTE;
                break;
            case TOK_STRING:
                state = HS_NAME;
                lvalue = MXS_STRDUP_A(tok->value);
                break;
            case TOK_STOP:
                /* Action: pop active hint */
                hint_pop(session);
                state = HS_INIT;
                break;
            case TOK_START:
                hintname = NULL;
                mode = HM_START;
                state = HS_INIT;
                break;
            default:
                /* Error: expected hint, name or STOP */
                MXS_ERROR("Syntax error in hint. Expected "
                          "'route', 'stop' or hint name instead of "
                          "'%s'. Hint ignored.",
                          token_get_keyword(tok));
                token_free(tok);
                goto retblock;
            }
            break;
        case HS_ROUTE:
            if (tok->token != TOK_TO)
            {
                /* Error, expect TO */;
                MXS_ERROR("Syntax error in hint. Expected "
                          "'to' instead of '%s'. Hint ignored.",
                          token_get_keyword(tok));
                token_free(tok);
                goto retblock;
            }
            state = HS_ROUTE1;
            break;
        case HS_ROUTE1:
            switch (tok->token)
            {
            case TOK_MASTER:
                rval = hint_create_route(rval,
                                         HINT_ROUTE_TO_MASTER, NULL);
                break;
            case TOK_SLAVE:
                rval = hint_create_route(rval,
                                         HINT_ROUTE_TO_SLAVE, NULL);
                break;

            case TOK_LAST:
                rval = hint_create_route(rval,
                                         HINT_ROUTE_TO_LAST_USED, NULL);
                break;

            case TOK_SERVER:
                state = HS_ROUTE_SERVER;
                break;
            default:
                /* Error expected MASTER, SLAVE or SERVER */
                MXS_ERROR("Syntax error in hint. Expected "
                          "'master', 'slave', or 'server' instead "
                          "of '%s'. Hint ignored.",
                          token_get_keyword(tok));

                token_free(tok);
                goto retblock;
            }
            break;
        case HS_ROUTE_SERVER:
            if (tok->token == TOK_STRING)
            {
                rval = hint_create_route(rval,
                                         HINT_ROUTE_TO_NAMED_SERVER, tok->value);
            }
            else
            {
                /* Error: Expected server name */
                MXS_ERROR("Syntax error in hint. Expected "
                          "server name instead of '%s'. Hint "
                          "ignored.",
                          token_get_keyword(tok));
                token_free(tok);
                goto retblock;
            }
            break;
        case HS_NAME:
            switch (tok->token)
            {
            case TOK_EQUAL:
                pname = lvalue;
                lvalue = NULL;
                state = HS_PVALUE;
                break;
            case TOK_PREPARE:
                pname = lvalue;
                state = HS_PREPARE;
                break;
            case TOK_START:
                /* Action start(lvalue) */
                hintname = lvalue;
                mode = HM_START;
                state = HS_INIT;
                break;
            default:
                /* Error, token tok->value not expected */
                MXS_ERROR("Syntax error in hint. Expected "
                          "'=', 'prepare', or 'start' instead of "
                          "'%s'. Hint ignored.",
                          token_get_keyword(tok));
                token_free(tok);
                goto retblock;
            }
            break;
        case HS_PVALUE:
            /* Action: pname = tok->value */
            rval = hint_create_parameter(rval, pname, tok->value);
            MXS_FREE(pname);
            pname = NULL;
            state = HS_INIT;
            break;
        case HS_PREPARE:
            mode = HM_PREPARE;
            hintname = lvalue;
            switch (tok->token)
            {
            case TOK_ROUTE:
                state = HS_ROUTE;
                break;
            case TOK_STRING:
                state = HS_NAME;
                lvalue = tok->value;
                break;
            default:
                /* Error, token tok->value not expected */
                MXS_ERROR("Syntax error in hint. Expected "
                          "'route' or hint name instead of "
                          "'%s'. Hint ignored.",
                          token_get_keyword(tok));
                token_free(tok);
                goto retblock;
            }
            break;
        }
        token_free(tok);
    } /*< while */

    if (tok && tok->token == TOK_END)
    {
        token_free(tok);
    }

    switch (mode)
    {
    case HM_START:
        /*
         * We are starting either a predefined set of hints,
         * creating a new set of hints and starting in a single
         * operation or starting an anonymous block of hints.
         */
        if (hintname == NULL && rval != NULL)
        {
            /* We are starting an anonymous block of hints */
            hint_push(session, rval);
            rval = NULL;
        }
        else if (hintname && rval)
        {
            /* We are creating and starting a block of hints */
            if (lookup_named_hint(session, hintname) != NULL)
            {
                /* Error hint with this name already exists */
            }
            else
            {
                create_named_hint(session, hintname, rval);
                hint_push(session, hint_dup(rval));
            }
        }
        else if (hintname && rval == NULL)
        {
            /* We starting an already define set of named hints */
            rval = lookup_named_hint(session, hintname);
            hint_push(session, hint_dup(rval));
            MXS_FREE(hintname);
            rval = NULL;
        }
        else if (hintname == NULL && rval == NULL)
        {
            /* Error case */
        }
        break;
    case HM_PREPARE:
        /*
         * We are preparing a named set of hints. Note this does
         * not trigger the usage of these hints currently.
         */
        if (hintname == NULL || rval == NULL)
        {
            /* Error case, name and hints must be defined */
        }
        else
        {
            create_named_hint(session, hintname, rval);
        }
        /* We are not starting the hints now, so return an empty
         * hint set.
         */
        rval = NULL;
        break;
    case HM_EXECUTE:
        /*
         * We have a one-off hint for the statement we are
         * currently forwarding.
         */
        break;
    }

retblock:
    if (rval == NULL)
    {
        /* No new hint parsed in this statement, apply the current
         * top of stack if there is one.
         */
        if (session->stack)
        {
            rval = hint_dup(session->stack->hint);
        }
    }
    return rval;
}

/**
 * Return the next token in the inout stream
 *
 * @param buf   A pointer to the buffer point, will be updated if a
 *      new buffer is used.
 * @param ptr   The pointer within the buffer we are processing
 * @return A HINT token
 */
static HINT_TOKEN *
hint_next_token(GWBUF **buf, char **ptr)
{
    char word[100], *dest;
    int inword = 0;
    int endtag = 0;
    char inquote = '\0';
    int i, found;
    HINT_TOKEN *tok;

    if ((tok = (HINT_TOKEN *)MXS_MALLOC(sizeof(HINT_TOKEN))) == NULL)
    {
        return NULL;
    }
    tok->value = NULL;
    dest = word;
    while (*ptr < (char *)((*buf)->end) || (*buf)->next)
    {
        /** word ends, don't move ptr but return with read word */
        if (inword && inquote == '\0' &&
            (isspace(**ptr) || **ptr == '='))
        {
            inword = 0;
            break;
        }
        /** found '=' or '\n', move ptr and return with the char */
        else if (!inword && inquote == '\0' && ((**ptr == '=') || (**ptr == '\n')))
        {
            *dest = **ptr;
            dest++;
            (*ptr)++;
            break;
        }
        else if (**ptr == '\'' && inquote == '\'')
        {
            inquote = '\0';
        }
        else if (**ptr == '\'')
        {
            inquote = **ptr;
        }
        /** Any other character which belongs to the word, move ahead */

        else if (**ptr == '/' && endtag)
        {
            /** Comment end tag found, rewind the pointer back and return the token */
            inword = 0;
            (*ptr)--;
            break;
        }
        else if (**ptr == '*' && !endtag)
        {
            endtag = 1;
        }
        else if (inword || (isspace(**ptr) == 0))
        {
            *dest++ = **ptr;
            inword = 1;
        }
        (*ptr)++;

        if (*ptr > (char *)((*buf)->end) && (*buf)->next)
        {
            *buf = (*buf)->next;
            *ptr = static_cast<char*>((*buf)->start);
        }

        if (dest - word > 98)
        {
            break;
        }
    } /*< while */
    *dest = 0;

    /* We now have a word in the local word, check to see if it is a
     * token we recognise.
     */
    if (word[0] == '\0' || (word[0] == '*' && word[1] == '/'))
    {
        tok->token = TOK_END;
        return tok;
    }
    if (word[0] == '\n')
    {
        tok->token = TOK_LINEBRK;
        return tok;
    }
    found = 0;
    for (i = 0; keywords[i].keyword; i++)
    {
        if (strcasecmp(word, keywords[i].keyword) == 0)
        {
            tok->token = keywords[i].token;
            found = 1;
            break;
        }
    }
    if (found == 0)
    {
        tok->token = TOK_STRING;
        tok->value = MXS_STRDUP_A(word);
    }

    return tok;
}

/**
 * hint_pop - pop the hint off the top of the stack if it is not empty
 *
 * @param   session The filter session.
 */
void
hint_pop(HINT_SESSION *session)
{
    HINTSTACK *ptr;
    HINT *hint;

    if ((ptr = session->stack) != NULL)
    {
        session->stack = ptr->next;
        while (ptr->hint)
        {
            hint = ptr->hint;
            ptr->hint = hint->next;
            hint_free(hint);
        }
        MXS_FREE(ptr);
    }
}

/**
 * Push a hint onto the stack of actie hints
 *
 * @param session   The filter session
 * @param hint      The hint to push, the hint ownership is retained
 *          by the stack and should not be freed by the caller
 */
static void
hint_push(HINT_SESSION *session, HINT *hint)
{
    HINTSTACK *item;

    if ((item = (HINTSTACK *)MXS_MALLOC(sizeof(HINTSTACK))) == NULL)
    {
        return;
    }
    item->hint = hint;
    item->next = session->stack;
    session->stack = item;
}

/**
 * Search for a hint block that already exists with this name
 *
 * @param session   The filter session
 * @param name      The name to lookup
 * @return the HINT or NULL if the name was not found.
 */
static HINT *
lookup_named_hint(HINT_SESSION *session, char *name)
{
    NAMEDHINTS *ptr = session->named_hints;

    while (ptr)
    {
        if (strcmp(ptr->name, name) == 0)
        {
            return ptr->hints;
        }
        ptr = ptr->next;
    }
    return NULL;
}

/**
 * Create a named hint block
 *
 * @param session   The filter session
 * @param name      The name of the block to ceate
 * @param hint      The hints themselves
 */
static void
create_named_hint(HINT_SESSION *session, char *name, HINT *hint)
{
    NAMEDHINTS *block;

    if ((block = (NAMEDHINTS *)MXS_MALLOC(sizeof(NAMEDHINTS))) == NULL)
    {
        return;
    }

    block->name = name;
    block->hints = hint_dup(hint);
    block->next = session->named_hints;
    session->named_hints = block;
}

/**
 * Release the given NAMEDHINTS struct and all included hints.
 *
 * @param named_hint NAMEDHINTS struct to be released
 *
 * @return pointer to next NAMEDHINTS struct.
 */
NAMEDHINTS* free_named_hint(
    NAMEDHINTS* named_hint)
{
    NAMEDHINTS* next;

    if (named_hint != NULL)
    {
        HINT* hint;

        next = named_hint->next;

        while (named_hint->hints != NULL)
        {
            hint = named_hint->hints->next;
            hint_free(named_hint->hints);
            named_hint->hints = hint;
        }
        MXS_FREE(named_hint->name);
        MXS_FREE(named_hint);
        return next;
    }
    else
    {
        return NULL;
    }
}

/**
 * Release the given HINTSTACK struct and all included hints.
 *
 * @param hint_stack HINTSTACK struct to be released
 *
 * @return pointer to next HINTSTACK struct.
 */
HINTSTACK* free_hint_stack(
    HINTSTACK* hint_stack)
{
    HINTSTACK* next;

    if (hint_stack != NULL)
    {
        HINT* hint;

        next = hint_stack->next;

        while (hint_stack->hint != NULL)
        {
            hint = hint_stack->hint->next;
            hint_free(hint_stack->hint);
            hint_stack->hint = hint;
        }
        MXS_FREE(hint_stack);
        return next;
    }
    else
    {
        return NULL;
    }
}
