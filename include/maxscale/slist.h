#ifndef _SLIST_H
#define _SLIST_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/skygw_utils.h>

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
