#ifndef _SLIST_H
#define _SLIST_H
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
 * Copyright MariaDB Corporation Ab 2016
 */

#include <skygw_utils.h>

EXTERN_C_BLOCK_BEGIN

typedef struct slist_node_st    slist_node_t;
typedef struct slist_st         slist_t;
typedef struct slist_cursor_st  slist_cursor_t;

/** Single-linked list */

struct slist_node_st
{
    skygw_chk_t   slnode_chk_top;
    slist_t*      slnode_list;
    slist_node_t* slnode_next;
    void*         slnode_data;
    size_t        slnode_cursor_refcount;
    skygw_chk_t   slnode_chk_tail;
};

struct slist_st
{
    skygw_chk_t   slist_chk_top;
    slist_node_t* slist_head;
    slist_node_t* slist_tail;
    int           slist_nelems;
    slist_t*      slist_cursors_list;
    skygw_chk_t   slist_chk_tail;
};

struct slist_cursor_st
{
    skygw_chk_t     slcursor_chk_top;
    slist_t*        slcursor_list;
    slist_node_t*   slcursor_pos;
    skygw_chk_t     slcursor_chk_tail;
};

slist_cursor_t* slist_init(void);
void            slist_done(slist_cursor_t* c);
size_t          slist_size(slist_cursor_t* c);

void            slcursor_add_data(slist_cursor_t* c, void* data);
void*           slcursor_get_data(slist_cursor_t* c);
void            slcursor_remove_data(slist_cursor_t* c);
bool            slcursor_move_to_begin(slist_cursor_t* c);
bool            slcursor_step_ahead(slist_cursor_t* c);

EXTERN_C_BLOCK_END

#endif
