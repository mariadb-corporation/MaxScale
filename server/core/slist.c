
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

#include <slist.h>
#include <atomic.h>

static slist_cursor_t* slist_cursor_init(slist_t* list);
static slist_t* slist_init_ex(bool create_cursors);
static slist_node_t* slist_node_init(void* data, slist_cursor_t* cursor);
static void slist_add_node(slist_t* list, slist_node_t* node);

#if defined(NOT_USED)
static slist_node_t* slist_node_get_next(slist_node_t* curr_node);
static slist_node_t* slist_get_first(slist_t* list);
static slist_cursor_t* slist_get_cursor(slist_t* list);
#endif /*< NOT_USED */

/** End of static function declarations */

static slist_t* slist_init_ex(bool create_cursors)
{
    slist_t* list;

    list = (slist_t*) calloc(1, sizeof (slist_t));
    list->slist_chk_top = CHK_NUM_SLIST;
    list->slist_chk_tail = CHK_NUM_SLIST;

    if (create_cursors)
    {
        list->slist_cursors_list = slist_init_ex(false);
    }

    return list;
}

static slist_node_t* slist_node_init(void* data, slist_cursor_t* cursor)
{
    slist_node_t* node;

    node = (slist_node_t*) calloc(1, sizeof (slist_node_t));
    node->slnode_chk_top = CHK_NUM_SLIST_NODE;
    node->slnode_chk_tail = CHK_NUM_SLIST_NODE;
    node->slnode_data = data;
    CHK_SLIST_NODE(node);

    if (cursor != NULL)
    {
        node->slnode_cursor_refcount += 1;
        cursor->slcursor_pos = node;
    }

    return node;
}

static void slist_add_node(slist_t* list, slist_node_t* node)
{
    CHK_SLIST(list);
    CHK_SLIST_NODE(node);

    if (list->slist_tail != NULL)
    {
        CHK_SLIST_NODE(list->slist_tail);
        CHK_SLIST_NODE(list->slist_head);
        ss_dassert(list->slist_tail->slnode_next == NULL);
        list->slist_tail->slnode_next = node;
    }
    else
    {
        list->slist_head = node;
    }
    list->slist_tail = node;
    node->slnode_list = list;
    list->slist_nelems += 1;
    CHK_SLIST(list);
}


#if defined(NOT_USED)

static slist_node_t* slist_node_get_next(slist_node_t* curr_node)
{
    CHK_SLIST_NODE(curr_node);

    if (curr_node->slnode_next != NULL)
    {
        CHK_SLIST_NODE(curr_node->slnode_next);
        return (curr_node->slnode_next);
    }

    return NULL;
}

static slist_node_t* slist_get_first(slist_t* list)
{
    CHK_SLIST(list);

    if (list->slist_head != NULL)
    {
        CHK_SLIST_NODE(list->slist_head);
        return list->slist_head;
    }
    return NULL;
}

static slist_cursor_t* slist_get_cursor(slist_t* list)
{
    CHK_SLIST(list);

    slist_cursor_t* c;

    c = slist_cursor_init(list);
    return c;
}
#endif /*< NOT_USED */

static slist_cursor_t* slist_cursor_init(slist_t* list)
{
    CHK_SLIST(list);
    slist_cursor_t* c;

    c = (slist_cursor_t *) calloc(1, sizeof (slist_cursor_t));
    c->slcursor_chk_top = CHK_NUM_SLIST_CURSOR;
    c->slcursor_chk_tail = CHK_NUM_SLIST_CURSOR;
    c->slcursor_list = list;
    /** Set cursor position is list is not empty */
    if (list->slist_head != NULL)
    {
        list->slist_head->slnode_cursor_refcount += 1;
        c->slcursor_pos = list->slist_head;
    }
    /** Add cursor to cursor list */
    slist_add_node(list->slist_cursors_list, slist_node_init(c, NULL));

    CHK_SLIST_CURSOR(c);
    return c;
}

/**
 * @node Create a cursor and a list with cursors supported. 19.6.2013 :
 * supports only cursor per list.
 *
 * Parameters:
 * @param void - <usage>
 *          <description>
 *
 * @return returns a pointer to cursor, which is not positioned
 * because the list is empty.
 *
 *
 * @details (write detailed description here)
 *
 */
slist_cursor_t* slist_init(void)
{
    slist_t* list;
    slist_cursor_t* slc;

    list = slist_init_ex(true);
    CHK_SLIST(list);
    slc = slist_cursor_init(list);
    CHK_SLIST_CURSOR(slc);

    return slc;
}

/**
 * @node moves cursor to the first node of list.
 *
 * Parameters:
 * @param c - <usage>
 *          <description>
 *
 * @return true if there is first node in the list
 * false is the list is empty.
 *
 *
 * @details (write detailed description here)
 *
 */
bool slcursor_move_to_begin(slist_cursor_t* c)
{
    bool succp = true;
    slist_t* list;

    CHK_SLIST_CURSOR(c);
    list = c->slcursor_list;
    CHK_SLIST(list);
    c->slcursor_pos = list->slist_head;
    if (c->slcursor_pos == NULL)
    {
        succp = false;
    }
    return succp;
}

/**
 * @node moves cursor to next node
 *
 * Parameters:
 * @param c - <usage>
 *          <description>
 *
 * @return true in success, false is there is no next node on the list.
 *
 *
 * @details (write detailed description here)
 *
 */
bool slcursor_step_ahead(slist_cursor_t* c)
{
    bool succp = false;
    slist_node_t* node;
    CHK_SLIST_CURSOR(c);
    CHK_SLIST_NODE(c->slcursor_pos);

    node = c->slcursor_pos->slnode_next;

    if (node != NULL)
    {
        CHK_SLIST_NODE(node);
        c->slcursor_pos = node;
        succp = true;
    }
    return succp;
}

void* slcursor_get_data(slist_cursor_t* c)
{
    slist_node_t* node;
    void* data = NULL;

    CHK_SLIST_CURSOR(c);
    node = c->slcursor_pos;

    if (node != NULL)
    {
        CHK_SLIST_NODE(node);
        data = node->slnode_data;
    }
    return data;
}

/**
 * @node Add data to the list by using cursor.
 *
 * Parameters:
 * @param c - <usage>
 *          <description>
 *
 * @param data - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details (write detailed description here)
 *
 */
void slcursor_add_data(slist_cursor_t* c, void* data)
{
    slist_t* list;
    slist_node_t* pos;

    CHK_SLIST_CURSOR(c);
    list = c->slcursor_list;
    CHK_SLIST(list);
    if (c->slcursor_pos != NULL)
    {
        CHK_SLIST_NODE(c->slcursor_pos);
    }
    ss_dassert(list->slist_tail->slnode_next == NULL);
    pos = slist_node_init(data, c);
    slist_add_node(list, pos);
    CHK_SLIST(list);
    CHK_SLIST_CURSOR(c);
}

/**
 * Remove the node currently pointed by the cursor from the slist. This does not delete the data in the
 * node but will delete the structure pointing to that data. This is useful when
 * the user wants to free the allocated memory. After node removal, the cursor
 * will point to the node before the removed node.
 * @param c Cursor pointing to the data node to be removed
 */
void slcursor_remove_data(slist_cursor_t* c)
{
    slist_node_t* node = c->slcursor_pos;
    int havemore = slist_size(c);
    slcursor_move_to_begin(c);

    if (node == c->slcursor_pos)
    {
        c->slcursor_list->slist_head = c->slcursor_list->slist_head->slnode_next;
        slcursor_move_to_begin(c);
        atomic_add((int*) &node->slnode_list->slist_nelems, -1);
        atomic_add((int*) &node->slnode_cursor_refcount, -1);
        if (node->slnode_cursor_refcount == 0)
        {
            free(node);
        }
        return;
    }

    while (havemore)
    {
        if (c->slcursor_pos->slnode_next == node)
        {
            c->slcursor_pos->slnode_next = node->slnode_next;
            atomic_add((int*) &node->slnode_list->slist_nelems, -1);
            atomic_add((int*) &node->slnode_cursor_refcount, -1);
            if (node->slnode_cursor_refcount == 0)
            {
                free(node);
            }
            return;
        }
        havemore = slcursor_step_ahead(c);
    }
}

/**
 * Return the size of the slist.
 * @param c slist cursor which refers to a list
 * @return nummber of elements in the list
 */
size_t slist_size(slist_cursor_t* c)
{
    return c->slcursor_list->slist_nelems;
}

void slist_done(slist_cursor_t* c)
{
    bool succp;
    void* data;

    succp = slcursor_move_to_begin(c);

    while (succp)
    {
        data = slcursor_get_data(c);
        free(data);
        succp = slcursor_step_ahead(c);
    }
    free(c->slcursor_list);
    free(c);
}


/** End of list implementation */

