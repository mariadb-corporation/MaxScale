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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hint.h>

/**
 * @file hint.c generic support routines for hints.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 25/07/14     Mark Riddoch    Initial implementation
 *
 * @endverbatim
 */


/**
 * Duplicate a list of hints
 *
 * @param hint          The hint list to duplicate
 * @return      A duplicate of the list
 *
 * Note : Optimize this to use version numbering instead of copying memory
 */
HINT *
hint_dup(HINT *hint)
{
    HINT *nlhead = NULL, *nltail = NULL, *ptr1, *ptr2;

    ptr1 = hint;
    while (ptr1)
    {
        if ((ptr2 = (HINT *)malloc(sizeof(HINT))) == NULL)
        {
            return nlhead;
        }
        ptr2->type = ptr1->type;
        if (ptr1->data)
        {
            ptr2->data = strdup(ptr1->data);
        }
        else
        {
            ptr2->data = NULL;
        }
        if (ptr1->value)
        {
            ptr2->value = strdup(ptr1->value);
        }
        else
        {
            ptr2->value = NULL;
        }
        ptr2->next = NULL;
        if (nltail)
        {
            nltail->next = ptr2;
            nltail = ptr2;
        }
        else
        {
            nlhead = ptr2;
            nltail = ptr2;
        }
        ptr1 = ptr1->next;
    }
    return nlhead;
}

/**
 * Create a ROUTE TO type hint
 *
 * @param head  The current hint list
 * @param type  The HINT_TYPE
 * @param data  Data may be NULL or the name of a server to route to
 * @return The result hint list
 */
HINT *
hint_create_route(HINT *head, HINT_TYPE type, char *data)
{
    HINT *hint;

    if ((hint = (HINT *)malloc(sizeof(HINT))) == NULL)
    {
        return head;
    }
    hint->next = head;
    hint->type = type;
    if (data)
    {
        hint->data = strdup(data);
    }
    else
    {
        hint->data = NULL;
    }
    hint->value = NULL;
    return hint;
}

/**
 * Create name/value parameter hint
 *
 * @param head  The current hint list
 * @param pname The parameter name
 * @param value The parameter value
 * @return The result hint list
 */
HINT *
hint_create_parameter(HINT *head, char *pname, char *value)
{
    HINT *hint;

    if ((hint = (HINT *)malloc(sizeof(HINT))) == NULL)
    {
        return head;
    }
    hint->next = head;
    hint->type = HINT_PARAMETER;
    hint->data = strdup(pname);
    hint->value = strdup(value);
    return hint;
}

/**
 * free_hint - free a hint
 *
 * @param hint          The hint to free
 */
void
hint_free(HINT *hint)
{
    if (hint->data)
    {
        free(hint->data);
    }
    if (hint->value)
    {
        free(hint->value);
    }
    free(hint);
}

bool hint_exists(HINT**    p_hint,
                 HINT_TYPE type)
{
    bool succp = false;

    while (*p_hint != NULL)
    {
        if ((*p_hint)->type == type)
        {
            succp = true;
        }
        p_hint = &(*p_hint)->next;
    }
    return succp;
}
