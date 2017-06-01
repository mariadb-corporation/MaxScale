#pragma once
#ifndef _MAXSCALE_MLIST_H
#define _MAXSCALE_MLIST_H
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

#include <maxscale/cdefs.h>
#include "skygw_utils.h"

MXS_BEGIN_DECLS

typedef struct mlist_node_st mlist_node_t;

typedef struct mlist_st
{
    skygw_chk_t    mlist_chk_top;
    char*          mlist_name;
    void (*mlist_datadel)(void *);  /**< clean-up function for data */
    simple_mutex_t mlist_mutex; /**< protect node updates and clean-up */
    bool           mlist_uselock;
    bool           mlist_islocked;
    bool           mlist_deleted;
    size_t         mlist_nodecount;
    size_t         mlist_nodecount_max; /**< size limit. 0 == no limit */
    size_t         mlist_versno;
    bool           mlist_flat;
    mlist_node_t*  mlist_first;
    mlist_node_t*  mlist_last;
    skygw_chk_t    mlist_chk_tail;
} mlist_t;

typedef struct mlist_cursor_st
{
    skygw_chk_t   mlcursor_chk_top;
    mlist_t*      mlcursor_list;
    mlist_node_t* mlcursor_pos;
    pthread_t*    mlcursor_owner_thr;
    skygw_chk_t   mlcursor_chk_tail;
} mlist_cursor_t;

struct mlist_node_st
{
    skygw_chk_t   mlnode_chk_top;
    mlist_t*      mlnode_list;
    mlist_node_t* mlnode_next;
    void*         mlnode_data;
    bool          mlnode_deleted;
    skygw_chk_t   mlnode_chk_tail;
};


mlist_t*      mlist_init(mlist_t*         mlist,
                         mlist_cursor_t** cursor,
                         char*            name,
                         void           (*datadel)(void*),
                         int              maxnodes);
void          mlist_done(mlist_t* list);
bool          mlist_add_data_nomutex(mlist_t* list, void* data);
bool          mlist_add_node_nomutex(mlist_t* list, mlist_node_t* newnode);
mlist_node_t* mlist_detach_first(mlist_t* ml);
mlist_node_t* mlist_detach_nodes(mlist_t* ml);
void*         mlist_node_get_data(mlist_node_t* node);
void          mlist_node_done(mlist_node_t* n);

mlist_cursor_t* mlist_cursor_init(mlist_t* ml);
void*           mlist_cursor_get_data_nomutex(mlist_cursor_t* c);
bool            mlist_cursor_move_to_first(mlist_cursor_t* c);

MXS_END_DECLS

#endif
