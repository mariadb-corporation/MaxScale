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

#include "internal/mlist.h"
#include <maxscale/alloc.h>

static void mlist_free_memory(mlist_t* ml, char* name);
static mlist_node_t* mlist_node_init(void* data, mlist_cursor_t* cursor);
//static mlist_node_t* mlist_node_get_next(mlist_node_t* curr_node);
//static mlist_node_t* mlist_get_first(mlist_t* list);
//static mlist_cursor_t* mlist_get_cursor(mlist_t* list);


/**
 * @node Cut off nodes of the list.
 *
 * Parameters:
 * @param ml - <usage>
 *          <description>
 *
 * @return Pointer to the first of the detached nodes.
 *
 *
 * @details (write detailed description here)
 *
 */
mlist_node_t* mlist_detach_nodes(mlist_t* ml)
{
    mlist_node_t* node;
    CHK_MLIST(ml);

    node = ml->mlist_first;
    ml->mlist_first = NULL;
    ml->mlist_last = NULL;
    ml->mlist_nodecount = 0;
    return node;
}

/**
 * @node Create a list with rwlock and optional read-only cursor
 *
 * Parameters:
 * @param listp - <usage>
 *          <description>
 *
 * @param cursor - <usage>
 *          <description>
 *
 * @param name - <usage>
 *          <description>
 *
 * @return Address of mlist_t struct.
 *
 *
 * @details Cursor must protect its reads with read lock, and after
 * acquiring read lock reader must check whether the list is deleted
 * (mlist_deleted).
 *
 */
mlist_t* mlist_init(mlist_t* listp, mlist_cursor_t** cursor, char* name,
                    void (*datadel)(void*), int maxnodes)
{
    mlist_cursor_t* c;
    mlist_t* list;

    if (cursor != NULL)
    {
        ss_dassert(*cursor == NULL);
    }
    /** listp is not NULL if caller wants flat list */
    if (listp == NULL)
    {
        list = (mlist_t*) MXS_CALLOC(1, sizeof (mlist_t));
    }
    else
    {
        /** Caller wants list flat, memory won't be freed */
        list = listp;
        list->mlist_flat = true;
    }
    ss_dassert(list != NULL);

    if (list == NULL)
    {
        mlist_free_memory(list, name);
        goto return_list;
    }
    list->mlist_chk_top = CHK_NUM_MLIST;
    list->mlist_chk_tail = CHK_NUM_MLIST;
    /** Set size limit for list. 0 means unlimited */
    list->mlist_nodecount_max = maxnodes;
    /** Set data deletion callback fun */
    list->mlist_datadel = datadel;

    if (name != NULL)
    {
        list->mlist_name = name;
    }
    /** Create mutex, return NULL if fails. */
    if (simple_mutex_init(&list->mlist_mutex, "writebuf mutex") == NULL)
    {
        ss_dfprintf(stderr, "* Creating rwlock for mlist failed\n");
        mlist_free_memory(list, name);
        list = NULL;
        goto return_list;
    }

    /** Create cursor for reading the list */
    if (cursor != NULL)
    {
        c = mlist_cursor_init(list);

        if (c == NULL)
        {
            simple_mutex_done(&list->mlist_mutex);
            mlist_free_memory(list, name);
            list = NULL;
            goto return_list;
        }
        CHK_MLIST_CURSOR(c);
        *cursor = c;
    }
    list->mlist_versno = 2; /*< vresno != 0 means that list is initialized */
    CHK_MLIST(list);

return_list:
    return list;
}

/**
 * @node Free mlist memory allocations. name must be explicitly
 * set if mlist has one.
 *
 * Parameters:
 * @param ml - <usage>
 *          <description>
 *
 * @param name - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details (write detailed description here)
 *
 */
static void mlist_free_memory(mlist_t* ml, char* name)
{
    mlist_node_t* node;

    /** name */
    if (name != NULL)
    {
        MXS_FREE(name);
    }
    if (ml != NULL)
    {
        /** list data */
        while (ml->mlist_first != NULL)
        {
            /** Scan list and free nodes and data inside nodes */
            node = ml->mlist_first->mlnode_next;
            mlist_node_done(ml->mlist_first);
            ml->mlist_first = node;
        }

        /** list structure */
        if (!ml->mlist_flat)
        {
            MXS_FREE(ml);
        }
    }
}

void* mlist_node_get_data(mlist_node_t* node)
{
    CHK_MLIST_NODE(node);
    return node->mlnode_data;
}

void mlist_node_done(mlist_node_t* n)
{
    CHK_MLIST_NODE(n);
    if (n->mlnode_data != NULL)
    {
        if (n->mlnode_list->mlist_datadel != NULL)
        {
            (n->mlnode_list->mlist_datadel(n->mlnode_data));
        }
        MXS_FREE(n->mlnode_data);
    }
    MXS_FREE(n);
}

/**
 * @node Mark list as deleted and free the memory.
 *
 * Parameters:
 * @param list - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details (write detailed description here)
 *
 */
void mlist_done(mlist_t* list)
{
    CHK_MLIST(list);
    simple_mutex_lock(&list->mlist_mutex, true);
    list->mlist_deleted = true;
    simple_mutex_unlock(&list->mlist_mutex);
    simple_mutex_done(&list->mlist_mutex);
    mlist_free_memory(list, list->mlist_name);
}

/**
 * @node Adds data to list by allocating node for it. Checks list size limit.
 *
 * Parameters:
 * @param list - <usage>
 *          <description>
 *
 * @param data - <usage>
 *          <description>
 *
 * @return true, if succeed, false, if list had node limit and it is full.
 *
 *
 * @details (write detailed description here)
 *
 */
bool mlist_add_data_nomutex(mlist_t* list, void* data)
{
    bool succp;

    succp = mlist_add_node_nomutex(list, mlist_node_init(data, NULL));

    return succp;
}

static mlist_node_t* mlist_node_init(void* data, mlist_cursor_t* cursor)
{
    mlist_node_t* node;

    node = (mlist_node_t*) MXS_CALLOC(1, sizeof (mlist_node_t));
    MXS_ABORT_IF_NULL(node);
    node->mlnode_chk_top = CHK_NUM_MLIST_NODE;
    node->mlnode_chk_tail = CHK_NUM_MLIST_NODE;
    node->mlnode_data = data;
    CHK_MLIST_NODE(node);

    if (cursor != NULL)
    {
        cursor->mlcursor_pos = node;
    }

    return node;
}

mlist_node_t* mlist_detach_first(mlist_t* ml)
{
    mlist_node_t* node;

    CHK_MLIST(ml);
    node = ml->mlist_first;
    CHK_MLIST_NODE(node);
    ml->mlist_first = node->mlnode_next;
    node->mlnode_next = NULL;

    ml->mlist_nodecount -= 1;
    if (ml->mlist_nodecount == 0)
    {
        ml->mlist_last = NULL;
    }
    else
    {
        CHK_MLIST_NODE(ml->mlist_first);
    }
    CHK_MLIST(ml);

    return (node);
}

/**
 * @node Add new node to end of list if there is space for it.
 *
 * Parameters:
 * @param list - <usage>
 *          <description>
 *
 * @param newnode - <usage>
 *          <description>
 *
 * @param add_last - <usage>
 *          <description>
 *
 * @return true, if succeede, false, if list size limit was exceeded.
 *
 *
 * @details (write detailed description here)
 *
 */
bool mlist_add_node_nomutex(mlist_t* list, mlist_node_t* newnode)
{
    bool succp = false;

    CHK_MLIST(list);
    CHK_MLIST_NODE(newnode);
    ss_dassert(!list->mlist_deleted);

    /** List is full already. */
    if (list->mlist_nodecount == list->mlist_nodecount_max)
    {
        goto return_succp;
    }
    /** Find location for new node */
    if (list->mlist_last != NULL)
    {
        ss_dassert(!list->mlist_last->mlnode_deleted);
        CHK_MLIST_NODE(list->mlist_last);
        CHK_MLIST_NODE(list->mlist_first);
        ss_dassert(list->mlist_last->mlnode_next == NULL);
        list->mlist_last->mlnode_next = newnode;
    }
    else
    {
        list->mlist_first = newnode;
    }
    list->mlist_last = newnode;
    newnode->mlnode_list = list;
    list->mlist_nodecount += 1;
    succp = true;
return_succp:
    CHK_MLIST(list);
    return succp;
}


/**
 * mlist_cursor_t
 */
mlist_cursor_t* mlist_cursor_init(mlist_t* list)
{
    CHK_MLIST(list);
    mlist_cursor_t* c;

    /** acquire shared lock to the list */
    simple_mutex_lock(&list->mlist_mutex, true);

    c = (mlist_cursor_t *) MXS_CALLOC(1, sizeof (mlist_cursor_t));

    if (c == NULL)
    {
        simple_mutex_unlock(&list->mlist_mutex);
        goto return_cursor;
    }
    c->mlcursor_chk_top = CHK_NUM_MLIST_CURSOR;
    c->mlcursor_chk_tail = CHK_NUM_MLIST_CURSOR;
    c->mlcursor_list = list;

    /** Set cursor position if list is not empty */
    if (list->mlist_first != NULL)
    {
        c->mlcursor_pos = list->mlist_first;
    }
    simple_mutex_unlock(&list->mlist_mutex);

    CHK_MLIST_CURSOR(c);

return_cursor:
    return c;
}

void* mlist_cursor_get_data_nomutex(mlist_cursor_t* mc)
{
    CHK_MLIST_CURSOR(mc);
    return (mc->mlcursor_pos->mlnode_data);
}

bool mlist_cursor_move_to_first(mlist_cursor_t* mc)
{
    bool succp = false;
    mlist_t* list;

    CHK_MLIST_CURSOR(mc);
    list = mc->mlcursor_list;
    CHK_MLIST(list);
    simple_mutex_lock(&list->mlist_mutex, true);

    if (mc->mlcursor_list->mlist_deleted)
    {
        simple_mutex_unlock(&list->mlist_mutex);
        return false;
    }
    /** Set position point to first node */
    mc->mlcursor_pos = list->mlist_first;

    if (mc->mlcursor_pos != NULL)
    {
        CHK_MLIST_NODE(mc->mlcursor_pos);
        succp = true;
    }
    simple_mutex_unlock(&list->mlist_mutex);
    return succp;
}
