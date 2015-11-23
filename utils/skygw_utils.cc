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

#ifndef PCRE2_CODE_UNIT_WIDTH
#define PCRE2_CODE_UNIT_WIDTH 8
#endif

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stddef.h>
#include <regex.h>
#include "skygw_debug.h"
#include <skygw_types.h>
#include <sys/time.h>
#include "skygw_utils.h"
#include <atomic.h>
#include <random_jkiss.h>
#include <pcre2.h>

#if defined(MLIST)


static mlist_node_t* mlist_node_init(void* data, mlist_cursor_t* cursor);
//static mlist_node_t* mlist_node_get_next(mlist_node_t* curr_node);
//static mlist_node_t* mlist_get_first(mlist_t* list);
//static mlist_cursor_t* mlist_get_cursor(mlist_t* list);


#endif /* MLIST */

static slist_cursor_t* slist_cursor_init(
        slist_t* list);

static slist_t* slist_init_ex(
        bool create_cursors);

static slist_node_t* slist_node_init(
        void*           data,
        slist_cursor_t* cursor);

static void slist_add_node(
        slist_t*      list,
        slist_node_t* node);

#if defined(NOT_USED)
static slist_node_t* slist_node_get_next(
        slist_node_t* curr_node);

static slist_node_t* slist_get_first(
        slist_t* list);
static slist_cursor_t* slist_get_cursor(
        slist_t* list);
#endif /*< NOT_USED */
        
static bool file_write_header(skygw_file_t* file);
static void simple_mutex_free_memory(simple_mutex_t* sm);
static void mlist_free_memory(mlist_t* ml, char* name);
static void thread_free_memory(skygw_thread_t* th, char* name);
/** End of static function declarations */

/** mutexed list, mlist */

#if defined(MLIST)

int skygw_rwlock_rdlock(
        skygw_rwlock_t* rwlock)
{
        int err = pthread_rwlock_rdlock(rwlock->srw_rwlock);

        if (err == 0) {
                rwlock->srw_rwlock_thr = pthread_self();
        } else {
                rwlock->srw_rwlock_thr = 0;
                char errbuf[STRERROR_BUFLEN];
                ss_dfprintf(stderr,
                            "* pthread_rwlock_rdlock : %s\n",
                            strerror_r(err, errbuf, sizeof(errbuf)));
        }
        return err; 
}

int skygw_rwlock_wrlock(
        skygw_rwlock_t* rwlock)
{
        int err = pthread_rwlock_wrlock(rwlock->srw_rwlock);

        if (err == 0) {
                rwlock->srw_rwlock_thr = pthread_self();
        } else {
                rwlock->srw_rwlock_thr = 0;
                char errbuf[STRERROR_BUFLEN];
                ss_dfprintf(stderr,
                            "* pthread_rwlock_wrlock : %s\n",
                            strerror_r(err, errbuf, sizeof(errbuf)));
        }
        return err; 
}

int skygw_rwlock_unlock(
        skygw_rwlock_t* rwlock)
{
        int err = pthread_rwlock_rdlock(rwlock->srw_rwlock);

        if (err == 0) {
                rwlock->srw_rwlock_thr = 0;
        } else {
                char errbuf[STRERROR_BUFLEN];
                ss_dfprintf(stderr,
                            "* pthread_rwlock_unlock : %s\n",
                            strerror_r(err, errbuf, sizeof(errbuf)));
        }
        return err; 
}



int skygw_rwlock_destroy(
        skygw_rwlock_t* rwlock)
{
	int err;
	/** Lock */
	if ((err = pthread_rwlock_wrlock(rwlock->srw_rwlock)) != 0)
	{
                char errbuf[STRERROR_BUFLEN];
		fprintf(stderr, 
			"* Error : pthread_rwlock_wrlock failed due to %d, %s.\n",
			err, 
			strerror_r(err, errbuf, sizeof(errbuf)));
		goto retblock;
	}
	/** Clean the struct */
	rwlock->srw_rwlock_thr = 0;
	/** Unlock */
	pthread_rwlock_unlock(rwlock->srw_rwlock);
	/** Destroy */
        if ((err = pthread_rwlock_destroy(rwlock->srw_rwlock)) != 0)
	{
                char errbuf[STRERROR_BUFLEN];
		fprintf(stderr,
			"* Error : pthread_rwlock_destroy failed due to %d,%s\n",
			err,
			strerror_r(err, errbuf, sizeof(errbuf)));
        }
        else
	{
		rwlock->srw_rwlock = NULL;
	}
retblock:
        return err; 
}

int skygw_rwlock_init(
        skygw_rwlock_t** rwlock)
{
        skygw_rwlock_t* rwl;
        int             err;
        
        rwl = (skygw_rwlock_t *)calloc(1, sizeof(skygw_rwlock_t));
	
	if (rwl == NULL)
	{
		err = 1;
		goto return_err;
	}
	rwl->srw_chk_top = CHK_NUM_RWLOCK;
	rwl->srw_chk_tail = CHK_NUM_RWLOCK;
	err = pthread_rwlock_init(rwl->srw_rwlock, NULL);
	ss_dassert(err == 0);
        
        if (err != 0) 
	{
		free(rwl);
                char errbuf[STRERROR_BUFLEN];
                ss_dfprintf(stderr,
                            "* Creating pthread_rwlock failed : %s\n",
                            strerror_r(err, errbuf, sizeof(errbuf)));
                goto return_err;
        }
        *rwlock = rwl;
	
return_err:
        return err;
}

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
mlist_node_t* mlist_detach_nodes(
        mlist_t* ml)
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
mlist_t* mlist_init(
        mlist_t*         listp,
        mlist_cursor_t** cursor,
        char*            name,
        void (*datadel)(void*),
        int              maxnodes)
{
        mlist_cursor_t* c;
        mlist_t*        list;

        if (cursor != NULL) {
                ss_dassert(*cursor == NULL);
        }
        /** listp is not NULL if caller wants flat list */
        if (listp == NULL) { 
                list = (mlist_t*)calloc(1, sizeof(mlist_t));
        } else {
                /** Caller wants list flat, memory won't be freed */
                list = listp;
                list->mlist_flat = true;
        }
        ss_dassert(list != NULL);

        if (list == NULL) {
                fprintf(stderr, "* Allocating memory for mlist failed\n");
                mlist_free_memory(list, name);
                goto return_list;
        }
        list->mlist_chk_top = CHK_NUM_MLIST;
        list->mlist_chk_tail = CHK_NUM_MLIST;
        /** Set size limit for list. 0 means unlimited */
        list->mlist_nodecount_max = maxnodes;
        /** Set data deletion callback fun */
        list->mlist_datadel = datadel;
	
        if (name != NULL) {
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
        if (cursor != NULL) {
                c = mlist_cursor_init(list);

                if (c == NULL) {
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
static void mlist_free_memory(
        mlist_t* ml,
        char*    name)
{
        mlist_node_t* node;
        
        /** name */
        if (name != NULL) {
                free(name);
        }
        if (ml != NULL) {
                /** list data */
                while(ml->mlist_first != NULL) {
                        /** Scan list and free nodes and data inside nodes */
                        node = ml->mlist_first->mlnode_next;
                        mlist_node_done(ml->mlist_first);
                        ml->mlist_first = node;
                }
            
                /** list structure */
                if (!ml->mlist_flat) {
                        free(ml);
                }
        }
}

void mlist_node_done(
        mlist_node_t* n)
{
        CHK_MLIST_NODE(n);
        if (n->mlnode_data != NULL) {
                if (n->mlnode_list->mlist_datadel != NULL) {
                        (n->mlnode_list->mlist_datadel(n->mlnode_data));
                }
                free(n->mlnode_data);
        }
        free(n);
}

void* mlist_node_get_data(
        mlist_node_t* node)
{
        CHK_MLIST_NODE(node);
        return node->mlnode_data;
}

mlist_cursor_t* mlist_cursor_init(
        mlist_t* list)
{
        CHK_MLIST(list);
        mlist_cursor_t* c;

        /** acquire shared lock to the list */
        simple_mutex_lock(&list->mlist_mutex, true);

        c = (mlist_cursor_t *)calloc(1, sizeof(mlist_cursor_t));

        if (c == NULL) {
                simple_mutex_unlock(&list->mlist_mutex);
                goto return_cursor;
        }
        c->mlcursor_chk_top = CHK_NUM_MLIST_CURSOR;
        c->mlcursor_chk_tail = CHK_NUM_MLIST_CURSOR;
        c->mlcursor_list = list;

        /** Set cursor position if list is not empty */
        if (list->mlist_first != NULL) {
                c->mlcursor_pos = list->mlist_first;
        }
        simple_mutex_unlock(&list->mlist_mutex);

        CHK_MLIST_CURSOR(c);

return_cursor:
        return c;
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
void mlist_done(
        mlist_t* list)
{
        CHK_MLIST(list);
        simple_mutex_lock(&list->mlist_mutex, true);
        list->mlist_deleted = true;
        simple_mutex_unlock(&list->mlist_mutex);
        simple_mutex_done(&list->mlist_mutex);
        mlist_free_memory(list, list->mlist_name);
}


void* mlist_cursor_get_data_nomutex(
        mlist_cursor_t* mc)
{
        CHK_MLIST_CURSOR(mc);
        return (mc->mlcursor_pos->mlnode_data);
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
bool mlist_add_data_nomutex(
        mlist_t* list,
        void*    data)
{
        bool succp;
        
        succp = mlist_add_node_nomutex(list, mlist_node_init(data, NULL));

        return succp;
}


static mlist_node_t* mlist_node_init(
        void*           data,
        mlist_cursor_t* cursor)
{
        mlist_node_t* node;

        node = (mlist_node_t*)calloc(1, sizeof(mlist_node_t));
        node->mlnode_chk_top = CHK_NUM_MLIST_NODE;
        node->mlnode_chk_tail = CHK_NUM_MLIST_NODE;
        node->mlnode_data = data;
        CHK_MLIST_NODE(node);

        if (cursor != NULL) {
                cursor->mlcursor_pos = node;
        }
        
        return node;
}

mlist_node_t* mlist_detach_first(
        mlist_t* ml)
{
        mlist_node_t* node;
        
        CHK_MLIST(ml);
        node = ml->mlist_first;
        CHK_MLIST_NODE(node);
        ml->mlist_first = node->mlnode_next;
        node->mlnode_next = NULL;
        
        ml->mlist_nodecount -= 1;
        if (ml->mlist_nodecount == 0) {
                ml->mlist_last = NULL;
        } else {
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
bool mlist_add_node_nomutex(
        mlist_t*      list,
        mlist_node_t* newnode)
{
        bool succp = false;
        
        CHK_MLIST(list);
        CHK_MLIST_NODE(newnode);
        ss_dassert(!list->mlist_deleted);

        /** List is full already. */
        if (list->mlist_nodecount == list->mlist_nodecount_max) {
                goto return_succp;
        }
        /** Find location for new node */
        if (list->mlist_last != NULL) {
                ss_dassert(!list->mlist_last->mlnode_deleted);
                CHK_MLIST_NODE(list->mlist_last);
                CHK_MLIST_NODE(list->mlist_first);
                ss_dassert(list->mlist_last->mlnode_next == NULL);
                list->mlist_last->mlnode_next = newnode;
        } else {
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



bool mlist_cursor_move_to_first(
        mlist_cursor_t* mc)
{
        bool     succp = false;
        mlist_t* list;

        CHK_MLIST_CURSOR(mc);
        list = mc->mlcursor_list;
        CHK_MLIST(list);
        simple_mutex_lock(&list->mlist_mutex, true);

        if (mc->mlcursor_list->mlist_deleted) {
                simple_mutex_unlock(&list->mlist_mutex);
                return false;
        }
        /** Set position point to first node */
        mc->mlcursor_pos = list->mlist_first;
        
        if (mc->mlcursor_pos != NULL) {
                CHK_MLIST_NODE(mc->mlcursor_pos);
                succp = true;
        }
        simple_mutex_unlock(&list->mlist_mutex);
        return succp;
}



#endif /* MLIST */


/** End of mlist */


size_t get_timestamp_len(void)
{
        return timestamp_len;
}

size_t get_timestamp_len_hp(void)
{
        return timestamp_len_hp;
}
/** 
 * @node Generate and write a timestamp to location passed as argument
 * by using at most tslen characters. 
 *
 * Parameters:
 * @param p_ts - in, use
 *          Write position in memory. Must be filled with at least
 *          <timestamp_len> zeroes 
 *
 * @return Length of string written to p_ts. Length includes terminating '\0'.
 *
 * 
 * @details (write detailed description here)
 *
 */
size_t snprint_timestamp(
        char* p_ts,
        size_t   tslen)
{
        time_t       t;
        struct tm    tm;
	size_t          rval;
        struct timeval tv;
        if (p_ts == NULL) {
		rval = 0;
                goto retblock;
        }

        /** Generate timestamp */

        t = time(NULL);
        localtime_r(&t, &tm);
        snprintf(p_ts,
                 MIN(tslen,timestamp_len),
                 timestamp_formatstr,
                 tm.tm_year+1900,
                 tm.tm_mon+1,
                 tm.tm_mday,
                 tm.tm_hour,
                 tm.tm_min,
                 tm.tm_sec);
	rval = strlen(p_ts)*sizeof(char);
retblock:
        return rval;
}


/**
 * @node Generate and write a timestamp to location passed as argument
 * by using at most tslen characters. This will use millisecond precision.
 *
 * Parameters:
 * @param p_ts - in, use
 *          Write position in memory. Must be filled with at least
 *          <timestamp_len> zeroes
 *
 * @return Length of string written to p_ts. Length includes terminating '\0'.
 *
 *
 * @details (write detailed description here)
 *
 */
size_t snprint_timestamp_hp(
        char* p_ts,
        size_t   tslen)
{
        time_t       t;
        struct tm    tm;
	size_t          rval;
        struct timeval tv;
        int usec;
        if (p_ts == NULL) {
		rval = 0;
                goto retblock;
        }

        /** Generate timestamp */

        gettimeofday(&tv,NULL);
        localtime_r(&tv.tv_sec, &tm);
        usec = tv.tv_usec/1000;
        snprintf(p_ts,
                 MIN(tslen,timestamp_len_hp),
                 timestamp_formatstr_hp,
                 tm.tm_year+1900,
                 tm.tm_mon+1,
                 tm.tm_mday,
                 tm.tm_hour,
                 tm.tm_min,
                 tm.tm_sec,
                 usec);
	rval = strlen(p_ts)*sizeof(char);
retblock:
        return rval;
}


static slist_t* slist_init_ex(
        bool create_cursors)
{
        slist_t* list;
        
        list = (slist_t*)calloc(1, sizeof(slist_t));
        list->slist_chk_top = CHK_NUM_SLIST;
        list->slist_chk_tail = CHK_NUM_SLIST;

        if (create_cursors) {
                list->slist_cursors_list = slist_init_ex(false);
        }
        
        return list; 
}


static slist_node_t* slist_node_init(
        void*           data,
        slist_cursor_t* cursor)
{
        slist_node_t* node;

        node = (slist_node_t*)calloc(1, sizeof(slist_node_t));
        node->slnode_chk_top = CHK_NUM_SLIST_NODE;
        node->slnode_chk_tail = CHK_NUM_SLIST_NODE;
        node->slnode_data = data;
        CHK_SLIST_NODE(node);

        if (cursor != NULL) {
                node->slnode_cursor_refcount += 1;
                cursor->slcursor_pos = node;
        }
        
        return node;
}

static void slist_add_node(
        slist_t*      list,
        slist_node_t* node)
{
        CHK_SLIST(list);
        CHK_SLIST_NODE(node);
        
        if (list->slist_tail != NULL) {
                CHK_SLIST_NODE(list->slist_tail);
                CHK_SLIST_NODE(list->slist_head);
                ss_dassert(list->slist_tail->slnode_next == NULL);
                list->slist_tail->slnode_next = node;
        } else {
                list->slist_head = node;
        }
        list->slist_tail = node;
        node->slnode_list = list;
        list->slist_nelems += 1;
        CHK_SLIST(list);
}


#if defined(NOT_USED)
static slist_node_t* slist_node_get_next(
        slist_node_t* curr_node)
{
        CHK_SLIST_NODE(curr_node);

        if (curr_node->slnode_next != NULL) {
                CHK_SLIST_NODE(curr_node->slnode_next);
                return (curr_node->slnode_next);
        }

        return NULL;
}
        
static slist_node_t* slist_get_first(
        slist_t* list)
{
        CHK_SLIST(list);

        if (list->slist_head != NULL) {
                CHK_SLIST_NODE(list->slist_head);
                return list->slist_head;
        }
        return NULL;
}

static slist_cursor_t* slist_get_cursor(
        slist_t* list)
{
        CHK_SLIST(list);
        
        slist_cursor_t* c;

        c = slist_cursor_init(list);
        return c;
}
#endif /*< NOT_USED */

static slist_cursor_t* slist_cursor_init(
        slist_t* list)
{
        CHK_SLIST(list);
        slist_cursor_t* c;

        c = (slist_cursor_t *)calloc(1, sizeof(slist_cursor_t));
        c->slcursor_chk_top = CHK_NUM_SLIST_CURSOR;
        c->slcursor_chk_tail = CHK_NUM_SLIST_CURSOR;
        c->slcursor_list = list;
        /** Set cursor position is list is not empty */
        if (list->slist_head != NULL) {
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
bool slcursor_move_to_begin(
        slist_cursor_t* c)
{
        bool     succp = true;
        slist_t* list;
        
        CHK_SLIST_CURSOR(c);
        list = c->slcursor_list;
        CHK_SLIST(list);
        c->slcursor_pos = list->slist_head;
        if (c->slcursor_pos == NULL) {
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
bool slcursor_step_ahead(
        slist_cursor_t* c)
{
        bool          succp = false;
        slist_node_t* node;
        CHK_SLIST_CURSOR(c);
        CHK_SLIST_NODE(c->slcursor_pos);
        
        node = c->slcursor_pos->slnode_next;
        
        if (node != NULL) {
                CHK_SLIST_NODE(node);
                c->slcursor_pos = node;
                succp = true;
        }
        return succp;
}


void* slcursor_get_data(
        slist_cursor_t* c)
{
        slist_node_t* node;
        void*         data = NULL;
        
        CHK_SLIST_CURSOR(c);
        node = c->slcursor_pos;
        
        if (node != NULL) {
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
void slcursor_add_data(
        slist_cursor_t* c,
        void*           data)
{
        slist_t*      list;
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
    slcursor_move_to_begin (c);

    if(node == c->slcursor_pos)
    {
        c->slcursor_list->slist_head = c->slcursor_list->slist_head->slnode_next;
        slcursor_move_to_begin (c);
        atomic_add((int*)&node->slnode_list->slist_nelems,-1);
        atomic_add((int*)&node->slnode_cursor_refcount,-1);
        if(node->slnode_cursor_refcount == 0)
        {
            free(node);
        }
        return;
    }

    while(havemore)
    {
        if( c->slcursor_pos->slnode_next == node)
        {
            c->slcursor_pos->slnode_next = node->slnode_next;
            atomic_add((int*)&node->slnode_list->slist_nelems,-1);
            atomic_add((int*)&node->slnode_cursor_refcount,-1);
            if(node->slnode_cursor_refcount == 0)
            {
                free(node);
            }
            return;
        }
        havemore = slcursor_step_ahead (c);
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


void slist_done(
        slist_cursor_t* c)
{
        bool  succp;
        void* data;

        succp = slcursor_move_to_begin(c);

        while (succp) {
                data = slcursor_get_data(c);
                free(data);
                succp = slcursor_step_ahead(c);
        }
        free(c->slcursor_list);
        free(c);
}


/** End of list implementation */

/** 
 * @node Initialize thread data structure 
 *
 * Parameters:
 * @param name copy is taken and stored to thread structure
 *
 * @param sth_thrfun - <usage>
 *          <description>
 *
 * @param data thread data pointer
 *
 * @return thread pointer or NULL in case of failure
 *
 * 
 * @details (write detailed description here)
 *
 */
skygw_thread_t* skygw_thread_init(
        const char* name,
        void* (*sth_thrfun)(void* data),
        void* data)
{
        skygw_thread_t* th =
                (skygw_thread_t *)calloc(1, sizeof(skygw_thread_t));

        if (th == NULL) {
                fprintf(stderr, "* Memory allocation for thread failed\n");
                goto return_th;
        }
        ss_dassert(th != NULL);
        th->sth_chk_top = CHK_NUM_THREAD;
        th->sth_chk_tail = CHK_NUM_THREAD;
        th->sth_parent = pthread_self();
        ss_debug(th->sth_state = THR_INIT;)
        th->sth_name = strndup(name, PATH_MAX);
        th->sth_mutex = simple_mutex_init(NULL, name);

        if (th->sth_mutex == NULL) {
                thread_free_memory(th, th->sth_name);
		th = NULL;
                goto return_th;
        }
        th->sth_thrfun = sth_thrfun;
        th->sth_data = data;
        CHK_THREAD(th);

return_th:
        return th;
}

static void thread_free_memory(
        skygw_thread_t* th,
        char*           name)
{
        if (name != NULL) {
                free(name);
        }
        free(th);
}

/** 
 * @node Release skygw_thread data except filewriter. 
 *
 * Parameters:
 * @param th - <usage>
 *          <description>
 *
 * @return void
 *
 * 
 * @details (write detailed description here)
 *
 */
void skygw_thread_done(
        skygw_thread_t* th)
{
        if (th != NULL) {
                CHK_THREAD(th);
                ss_dassert(th->sth_state == THR_STOPPED);
                ss_debug(th->sth_state = THR_DONE;)
                        simple_mutex_done(th->sth_mutex);
                pthread_join(th->sth_thr, NULL);
                thread_free_memory(th, th->sth_name);
        }
}
        

pthread_t skygw_thread_gettid(
        skygw_thread_t* thr)
{
        CHK_THREAD(thr);
        return thr->sth_thr;
}

int skygw_thread_start(
        skygw_thread_t* thr)
{
        int err;
        
        CHK_THREAD(thr);
        err = pthread_create(&thr->sth_thr,
                             NULL,
                             thr->sth_thrfun,
                             thr);
        ss_dassert(err == 0);
        
        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Starting file writer thread failed due error, "
                        "%d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                goto return_err;
        }

return_err:
        return err;
}

#if defined(SS_DEBUG)
skygw_thr_state_t skygw_thread_get_state(
        skygw_thread_t* thr)
{
        CHK_THREAD(thr);
        return thr->sth_state;
}
#endif

/** 
 * @node Update thread state 
 *
 * Parameters:
 * @param thr - <usage>
 *          <description>
 *
 * @param state - <usage>
 *          <description>
 *
 * @return void
 *
 * 
 * @details Thread must check state with mutex.
 *
 */
#if defined(SS_DEBUG)
void skygw_thread_set_state(
        skygw_thread_t*   thr,
        skygw_thr_state_t state)
{
        CHK_THREAD(thr);
        simple_mutex_lock(thr->sth_mutex, true);
        thr->sth_state = state;
        simple_mutex_unlock(thr->sth_mutex);
}
#endif
/** 
 * @node Set exit flag for thread from other thread 
 *
 * Parameters:
 * @param thr - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details This call informs thread about exit flag and waits the response.
 *
 */
bool skygw_thread_set_exitflag(
        skygw_thread_t*  thr,
        skygw_message_t* sendmes,
        skygw_message_t* recmes)
{
        bool succp = false;

        /**
         * If thread struct pointer is NULL there's running thread
         * neither.
         */
        if (thr == NULL) {
                succp = true;
                goto return_succp;
        }
        CHK_THREAD(thr);
        CHK_MESSAGE(sendmes);
        CHK_MESSAGE(recmes);
        
        simple_mutex_lock(thr->sth_mutex, true);
        succp = !thr->sth_must_exit;
        thr->sth_must_exit = true;
        simple_mutex_unlock(thr->sth_mutex);
        
        /** Inform thread and wait for response */
        if (succp) {
                skygw_message_send(sendmes);
                skygw_message_wait(recmes);
        }
        
        ss_dassert(thr->sth_state == THR_STOPPED);
        
return_succp:
        return succp;
}

void* skygw_thread_get_data(
        skygw_thread_t* thr)
{
        CHK_THREAD(thr);
        return thr->sth_data;
}

bool skygw_thread_must_exit(
        skygw_thread_t* thr)
{
        CHK_THREAD(thr);
        return thr->sth_must_exit;
}

void acquire_lock(
        int* l)
{
        register int misscount = 0;
	struct timespec ts1;
	ts1.tv_sec = 0;
        
        while (atomic_add(l, 1) != 0) {
                atomic_add(l, -1);
                misscount += 1;
                if (misscount > 10) 
		{
			ts1.tv_nsec = (random_jkiss()%misscount)*1000000;
			nanosleep(&ts1, NULL);
                }
        }
}

void release_lock(
        int* l)
{
        atomic_add(l, -1);
}


/** 
 * @node Create a simple_mutex structure which encapsulates pthread_mutex. 
 *
 * Parameters:
 * @param mutexptr if mutex is initialized within caller's memory, this is
 * the address for it. If mutex is flat, there is value, otherwise it is NULL. 
 *
 * @param name name of mutex, passed argument is copied and pointer is stored
 *  to mutex struct.
 *
 * @return simple_mutex pointer or NULL in case of failure.
 *
 * 
 * @details If mutex is flat, sm_enabled can be read if the memory is not freed.
 * If flat mutex exists, sm_enabled is true.
 * If mutex allocates its own memory, the pointer is NULL if mutex doesn't
 * exist.
 *
 */
simple_mutex_t* simple_mutex_init(
        simple_mutex_t* mutexptr,
        const char*     name)
{
        int err;      
        simple_mutex_t* sm;
        
        /** Copy pointer only if flat, allocate memory otherwise. */
        if (mutexptr != NULL) {
                sm = mutexptr;
                sm->sm_flat = true;
        } else {
                sm = (simple_mutex_t *)calloc(1, sizeof(simple_mutex_t));
        }
        ss_dassert(sm != NULL);
#if defined(SS_DEBUG)
        sm->sm_chk_top = CHK_NUM_SIMPLE_MUTEX;
        sm->sm_chk_tail = CHK_NUM_SIMPLE_MUTEX;
#endif
        sm->sm_name = strndup(name, PATH_MAX);
        
        /** Create pthread mutex */
        err = pthread_mutex_init(&sm->sm_mutex, NULL);
        
        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Initializing simple mutex %s failed due error "
                        "%d, %s\n",
                        name, 
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                perror("simple_mutex : ");
                
                /** Write zeroes if flat, free otherwise. */
                if (sm->sm_flat) {
                        memset(sm, 0, sizeof(*sm));
                } else {
                        simple_mutex_free_memory(sm);
                        sm = NULL;
                }
                goto return_sm;
        }
        sm->sm_enabled = true;
        CHK_SIMPLE_MUTEX(sm);
        
return_sm:
        return sm;
}

int simple_mutex_done(
        simple_mutex_t* sm)
{
        int err = 0;
        
        CHK_SIMPLE_MUTEX(sm);

        if (atomic_add(&sm->sm_enabled, -1) != 1) {
                atomic_add(&sm->sm_enabled, 1);
        }
        err = pthread_mutex_destroy(&sm->sm_mutex);

#if defined(NOT_USED)
        if (err != 0) {
                perror("simple_mutex : ");
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Destroying simple mutex %s failed due "
                        "%d, %s\n",
                        sm->sm_name, 
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                goto return_err;
        }
#endif
        simple_mutex_free_memory(sm);

#if defined(NOT_USED)  
return_err:
#endif
        return err;
}

static void simple_mutex_free_memory(
        simple_mutex_t* sm)
{
        if (sm->sm_name != NULL) {
                free(sm->sm_name);
        }
        if (!sm->sm_flat) {
                free(sm);
        }
}

int simple_mutex_lock(
        simple_mutex_t* sm,
        bool            block)
{
        int err;

        /**
         * Leaving the following serves as a reminder. It may assert
         * any given time because sm_lock_thr is not protected.
         *
         * ss_dassert(sm->sm_lock_thr != pthread_self());
         */
        if (block) {
                err = pthread_mutex_lock(&sm->sm_mutex);
        } else {
                err = pthread_mutex_trylock(&sm->sm_mutex);
        }

        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Locking simple mutex %s failed due error, "
                        "%d, %s\n",
                        sm->sm_name, 
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                perror("simple_mutex : ");
        } else {
                /**
                 * Note that these updates are not protected.
                 */
                sm->sm_locked = true;
                sm->sm_lock_thr = pthread_self();
        }
        return err;
}

int simple_mutex_unlock(
        simple_mutex_t* sm)
{
        int err;
        /**
         * Leaving the following serves as a reminder. It may assert
         * any given time because sm_lock_thr is not protected.
         *
         * ss_dassert(sm->sm_lock_thr == pthread_self());
         */
        err = pthread_mutex_unlock(&sm->sm_mutex);

        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Unlocking simple mutex %s failed due error "
                        "%d, %s\n",
                        sm->sm_name, 
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                perror("simple_mutex : ");
        } else {
                /**
                 * Note that these updates are not protected.
                 */
                sm->sm_locked = false;
                sm->sm_lock_thr = 0;
        }
        return err;
}

skygw_message_t* skygw_message_init(void)
{
        int err;
        skygw_message_t* mes;

        mes = (skygw_message_t*)calloc(1, sizeof(skygw_message_t));
	
	if (mes == NULL)
	{
		err = 1;
		goto return_mes;
	}
        mes->mes_chk_top = CHK_NUM_MESSAGE;
        mes->mes_chk_tail = CHK_NUM_MESSAGE;
        err = pthread_mutex_init(&(mes->mes_mutex), NULL);
        
        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Initializing pthread mutex failed due error "
                        "%d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
		free(mes);
                mes = NULL;
                goto return_mes;
        }
        err = pthread_cond_init(&(mes->mes_cond), NULL);

        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Initializing pthread cond var failed, "
                        "due error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
		pthread_mutex_destroy(&mes->mes_mutex);
		free(mes);
                mes = NULL;
                goto return_mes;
        }
        CHK_MESSAGE(mes);
return_mes:
        return mes;
}

void skygw_message_done(
        skygw_message_t* mes)
{
        int err;

        /**
         * If message struct pointer is NULL there's nothing to free.
         */
        if (mes == NULL) {
                return;
        }
        CHK_MESSAGE(mes);
        err = pthread_cond_destroy(&(mes->mes_cond));

        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Destroying cond var failed due error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
        }
        ss_dassert(err == 0);
        err = pthread_mutex_destroy(&(mes->mes_mutex));

        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Destroying pthread mutex failed, "
                        "due error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
        }
        ss_dassert(err == 0);
        free(mes);
}

skygw_mes_rc_t skygw_message_send(
        skygw_message_t* mes)
{
        int err;
        skygw_mes_rc_t rc = MES_RC_FAIL;

        CHK_MESSAGE(mes);
        err = pthread_mutex_lock(&(mes->mes_mutex));

        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Locking pthread mutex failed, "
                        "due to error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                goto return_mes_rc;
        }
        mes->mes_sent = true;
        err = pthread_cond_signal(&(mes->mes_cond));

	if (err == 0)
	{
		rc = MES_RC_SUCCESS;
	}
	else
	{
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Signaling pthread cond var failed, "
                        "due to error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
        }
        err = pthread_mutex_unlock(&(mes->mes_mutex));

        if (err != 0) 
	{
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Unlocking pthread mutex failed, "
                        "due to error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
        }
        
return_mes_rc:
        return rc;
}

void skygw_message_wait(
        skygw_message_t* mes)
{
        int err;

        CHK_MESSAGE(mes);
        err = pthread_mutex_lock(&(mes->mes_mutex));

        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Locking pthread mutex failed, "
                        "due error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
        }
        ss_dassert(err == 0);
        
        while (!mes->mes_sent) {
                err = pthread_cond_wait(&(mes->mes_cond), &(mes->mes_mutex));

                if (err != 0) {
                        char errbuf[STRERROR_BUFLEN];
                        fprintf(stderr,
                                "* Locking pthread cond wait failed, "
                                "due error %d, %s\n",
                                err,
                                strerror_r(errno, errbuf, sizeof(errbuf)));
                }
        }
        mes->mes_sent = false;
        err = pthread_mutex_unlock(&(mes->mes_mutex));
        
        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Unlocking pthread mutex failed, "
                        "due error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
        }
        ss_dassert(err == 0);
}

        
void skygw_message_reset(
        skygw_message_t* mes)
{
        int err;
        
        CHK_MESSAGE(mes);
        err = pthread_mutex_lock(&(mes->mes_mutex));

        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Locking pthread mutex failed, "
                        "due error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                goto return_mes_rc;
        }
        ss_dassert(err == 0);
        mes->mes_sent = false;
        err = pthread_mutex_unlock(&(mes->mes_mutex));

        if (err != 0) {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Unlocking pthread mutex failed, "
                        "due error %d, %s\n",
                        err,
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                goto return_mes_rc;
        }
return_mes_rc:
        ss_dassert(err == 0);
}

static bool file_write_header(
        skygw_file_t* file)
{
        bool        succp = false;
        size_t      wbytes1;
        size_t      wbytes2;
        size_t      wbytes3;
        size_t      wbytes4;
        size_t      len1;
        size_t      len2;
        size_t      len3;
        size_t      len4;
        const char* header_buf1;
        char*       header_buf2 = NULL;
        char*       header_buf3 = NULL;
        const char* header_buf4;
        time_t*     t;
        struct tm*  tm;
#if defined(LAPTOP_TEST)
	struct timespec ts1;
	ts1.tv_sec = 0;
	ts1.tv_nsec = DISKWRITE_LATENCY*1000000;
#endif
	
        t = (time_t *)malloc(sizeof(time_t));
        tm = (struct tm *)malloc(sizeof(struct tm));
        *t = time(NULL); 
        localtime_r(t, tm);
        
        CHK_FILE(file);
        header_buf1 = "\n\nMariaDB Corporation MaxScale\t";
        header_buf2 = (char *)calloc(1, strlen(file->sf_fname)+2);
        snprintf(header_buf2, strlen(file->sf_fname)+2, "%s ", file->sf_fname);
        header_buf3 = strdup(asctime(tm));
        header_buf4 = "------------------------------------------------------"
                "-----------------\n";

        if (header_buf2 == NULL) {
                goto return_succp;
        }
        if (header_buf3 == NULL) {
                goto return_succp;
        }

        len1 = strlen(header_buf1);
        len2 = strlen(header_buf2);
        len3 = strlen(header_buf3);
        len4 = strlen(header_buf4);
#if defined(LAPTOP_TEST)
	nanosleep(&ts1, NULL);
#else
        wbytes1=fwrite((void*)header_buf1, len1, 1, file->sf_file);
        wbytes2=fwrite((void*)header_buf2, len2, 1, file->sf_file);
        wbytes3=fwrite((void*)header_buf3, len3, 1, file->sf_file);
        wbytes4=fwrite((void*)header_buf4, len4, 1, file->sf_file);
        
        if (wbytes1 != 1 || wbytes2 != 1 || wbytes3 != 1 || wbytes4 != 1) {
                fprintf(stderr,
                        "\nError : Writing header %s %s %s %s failed.\n",
                        header_buf1,
                        header_buf2,
                        header_buf3,
                        header_buf4);
                perror("Logfile header write");
                goto return_succp;
        }
#endif
        CHK_FILE(file);

        succp = true;
return_succp:
        if (header_buf2 != NULL) {
                free(header_buf2);
        }
        if (header_buf3 != NULL) {
                free(header_buf3);
        }
        free(t);
        free(tm);
        return succp;
}

static bool file_write_footer(
        skygw_file_t* file,
	bool          shutdown)
{
        bool        succp = false;
        size_t      wbytes1;
        size_t      wbytes3;
        size_t      wbytes4;
        size_t      len1;
        size_t      len4;
        int         tslen;
        const char* header_buf1;
        char*       header_buf3 = NULL;
        const char* header_buf4;
#if defined(LAPTOP_TEST)
	struct timespec ts1;
	ts1.tv_sec = 0;
	ts1.tv_nsec = DISKWRITE_LATENCY*1000000;
#endif
	
        CHK_FILE(file);
	
	if (shutdown)
	{
		header_buf1 = "MaxScale is shut down.\t";
	}
	else
	{
		header_buf1 = "Closed file due log rotation.\t";
	}
        tslen = get_timestamp_len();
        header_buf3 = (char *)malloc(tslen);
	
        if (header_buf3 == NULL) 
	{
                goto return_succp;
        }
        tslen = snprint_timestamp(header_buf3, tslen);
        header_buf4 = "\n--------------------------------------------"
                "---------------------------\n";

        len1 = strlen(header_buf1);
        len4 = strlen(header_buf4);
#if defined(LAPTOP_TEST)
	nanosleep(&ts1, NULL);
#else
        wbytes3=fwrite((void*)header_buf3, tslen, 1, file->sf_file);
        wbytes1=fwrite((void*)header_buf1, len1, 1, file->sf_file);
        wbytes4=fwrite((void*)header_buf4, len4, 1, file->sf_file);
        
        if (wbytes1 != 1 || wbytes3 != 1 || wbytes4 != 1) 
	{
                fprintf(stderr,
                        "\nError : Writing header %s %s to %s failed.\n",
                        header_buf1,
                        header_buf3,
                        header_buf4);
                perror("Logfile header write");
                goto return_succp;
        }
#endif
        CHK_FILE(file);

        succp = true;
return_succp:
        if (header_buf3 != NULL) 
	{
                free(header_buf3);
        }
        return succp;
}

/**
 * Write data to a file.
 * 
 * @param file		write target
 * @param data		pointer to contiguous memory buffer
 * @param nbytes	amount of bytes to be written
 * @param flush		ensure that write is permanent
 * 
 * @return 0 if succeed, errno if failed.
 */
int skygw_file_write(
        skygw_file_t* file,
        void*         data,
        size_t        nbytes,
        bool          flush)
{
        int    rc;
        size_t nwritten;
        int    fd;
        static int writecount;
  
        CHK_FILE(file);

        nwritten = fwrite(data, nbytes, 1, file->sf_file);
        
        if (nwritten != 1) {
		rc = errno;
                perror("Logfile write.\n");
                fprintf(stderr,
                        "* Writing %ld bytes,\n%s\n to %s failed.\n",
                        nbytes,
                        (char *)data,
                        file->sf_fname);
                goto return_rc;
        }
        writecount += 1;
        
        if (flush || writecount == FSYNCLIMIT) 
	{
                fd = fileno(file->sf_file);
                fflush(file->sf_file);
                fsync(fd);
                writecount = 0;
        }

        rc = 0;
        CHK_FILE(file);
return_rc:
        return rc;
}

skygw_file_t* skygw_file_alloc(
        char* fname)
{
    skygw_file_t* file;

    if ((file = (skygw_file_t *)calloc(1, sizeof(skygw_file_t))) == NULL)
    {
        fprintf(stderr,
                "* Error : Memory allocation for file %s failed.\n",
                fname);
        perror("SkyGW file allocation\n");
        return NULL;
    }
    ss_dassert(file != NULL);
    file->sf_chk_top = CHK_NUM_FILE;
    file->sf_chk_tail = CHK_NUM_FILE;
    file->sf_fname = strdup(fname);
    return file;
}

skygw_file_t* skygw_file_init(
        char* fname,
        char* symlinkname)
{
        skygw_file_t* file;
        
        if ((file = skygw_file_alloc (fname)) == NULL)
	{
            /** Error was reported in skygw_file_alloc */
		goto return_file;
        }

        if ((file->sf_file = fopen(file->sf_fname, "a")) == NULL)
	{
                int eno = errno;
                errno = 0;
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "* Opening file %s failed due %d, %s.\n",
                        file->sf_fname,
                        eno,
                        strerror_r(eno, errbuf, sizeof(errbuf)));
                free(file);
                file = NULL;
                goto return_file;
        }
        setvbuf(file->sf_file, NULL, _IONBF, 0);
        
        if (!file_write_header(file)) 
	{
                int eno = errno;
                errno = 0;
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr,
                        "\nError : Writing header of log file %s failed due %d, %s.\n",
                        file->sf_fname,
                        eno,
                        strerror_r(eno, errbuf, sizeof(errbuf)));
                free(file);
                file = NULL;
                goto return_file;
        }
        CHK_FILE(file);
        ss_dfprintf(stderr, "Opened %s\n", file->sf_fname);

        /**
         * Create symlink to newly created file if name was provided.
         */
        if (symlinkname != NULL)
        {
                int rc;

                unlink(symlinkname);
                rc = symlink(fname, symlinkname);

                if (rc != 0)
                {
                        int eno = errno;
                        errno = 0;
                        char errbuf[STRERROR_BUFLEN];
                        fprintf(stderr,
                                "failed to create symlink %s -> "
                                "%s due %d, %s. Exiting.",
                                fname,
                                symlinkname,
                                eno,
                                strerror_r(eno, errbuf, sizeof(errbuf)));
                        free(file);
                        file = NULL;
                        goto return_file;
                }
        }

return_file:
        return file;
}

void skygw_file_free(skygw_file_t* file)
{
    if(file)
    {
        free(file->sf_fname);
        free(file);
    }
}

void skygw_file_close(
	skygw_file_t* file,
	bool          shutdown)
{
	int fd;
	int err;
	
	if (file != NULL) 
	{
		CHK_FILE(file);
		
		if (!file_write_footer(file, shutdown)) 
		{
			fprintf(stderr,
				"* Writing footer to log file %s failed.\n",
				file->sf_fname);
			perror("Write fike footer\n");
		}
		fd = fileno(file->sf_file);
		fsync(fd);

		if ((err = fclose(file->sf_file)) != 0)
		{
                        char errbuf[STRERROR_BUFLEN];
			fprintf(stderr,
				"* Closing file %s failed due to %d, %s.\n",
				file->sf_fname,
				errno,
				strerror_r(errno, errbuf, sizeof(errbuf)));
		}
		else
		{
			ss_dfprintf(stderr, "Closed %s\n", file->sf_fname);        
			skygw_file_free (file);
		}
	}
}

/**
 * Find the given needle - user-provided literal -  and replace it with 
 * replacement string. Separate user-provided literals from matching table names 
 * etc. by searching only substrings preceded by non-letter and non-number. 
 * 
 * @param haystack      Plain text query string, not to be freed
 * @param needle        Substring to be searched, not to be freed
 * @param replacement   Replacement text, not to be freed
 * 
 * @return newly allocated string where needle is replaced 
 */ 
char* replace_literal(
        char* haystack,
        const char* needle,
        const char* replacement)
{
        const char* prefix = "[ ='\",\\(]"; /*< ' ','=','(',''',''"',',' are allowed before needle */
        const char* suffix = "([^[:alnum:]]|$)"; /*< alpha-num chars aren't allowed after the needle */
        char*       search_re;
        char*       newstr;
        regex_t     re; 
        regmatch_t  match;
        int         rc;
        size_t      rlen = strlen(replacement);
        size_t      nlen = strlen(needle);
        size_t      hlen = strlen(haystack);
        
        search_re = (char *)malloc(strlen(prefix)+nlen+strlen(suffix)+1);
        
        if (search_re == NULL)
        {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr, "Regex memory allocation failed : %s\n", 
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                newstr = haystack;
                goto retblock;
        }                
        
        sprintf(search_re, "%s%s%s", prefix, needle, suffix);
        /** Allocate memory for new string +1 for terminating byte */
        newstr = (char *)malloc(hlen-nlen+rlen+1);
        
        if (newstr == NULL)
        {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr, "Regex memory allocation failed : %s\n", 
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                free(search_re);
		free(newstr);
                newstr = haystack;
                goto retblock;
        }                
        
        rc = regcomp(&re, search_re, REG_EXTENDED|REG_ICASE);
        ss_info_dassert(rc == 0, "Regex check");
        
        if (rc != 0)
        {
                char error_message[MAX_ERROR_MSG];
                regerror (rc, &re, error_message, MAX_ERROR_MSG);
                fprintf(stderr, 
                        "Regex error compiling '%s': %s\n",
                        search_re, 
                        error_message);
                free(search_re);
		free(newstr);
                newstr = haystack;
                goto retblock;
        }
        rc = regexec(&re, haystack, 1, &match, 0);
        
        if (rc != 0)
        {
                free(search_re);
		free(newstr);
                regfree(&re);
                newstr = haystack;
                goto retblock;
        }
        memcpy(newstr, haystack, match.rm_so+1);
        memcpy(newstr+match.rm_so+1, replacement, rlen);
        /** +1 is terminating byte */
        memcpy(newstr+match.rm_so+1+rlen, haystack+match.rm_so+1+nlen, hlen-(match.rm_so+1)-nlen+1);      
        
        regfree(&re);
        free(haystack);
	free(search_re);
retblock:
        return newstr;
}

/**
 * Replace everything inside single or double quotes with question marks.
 * @param str String to modify
 * @return Pointer to new modified string or NULL if memory allocation failed
 */
char* replace_quoted(const char* str)
{
    PCRE2_SIZE erroffset;
    int errcore;
    static const PCRE2_SPTR pattern = (PCRE2_SPTR) "(['\"])[^'\"]+(['\"])";
    static const PCRE2_SPTR replace = (PCRE2_SPTR) "$1?$2";
    pcre2_code* re;
    pcre2_match_data* mdata;
    int orig_len = strlen(str);
    size_t len = strlen(str);
    char* output;

    if ((output = (char*) malloc(len * sizeof(char))))
    {
         /** TODO: Consider moving pattern compilation to some init function. */
        if ((re = pcre2_compile(pattern, PCRE2_ZERO_TERMINATED,
                                0, &errcore, &erroffset, NULL)))
        {
            if ((mdata = pcre2_match_data_create_from_pattern(re, NULL)))
            {
                while (pcre2_substitute(re, (PCRE2_SPTR) str, orig_len, 0,
                                        PCRE2_SUBSTITUTE_GLOBAL, mdata, NULL,
                                        replace, PCRE2_ZERO_TERMINATED,
                                        (PCRE2_UCHAR8*) output, &len) == PCRE2_ERROR_NOMEMORY)
                {
                    char* tmp = (char*) realloc(output, len *= 2);
                    if (tmp == NULL)
                    {
                        free(output);
                        output = NULL;
                        break;
                    }
                    output = tmp;
                }
                pcre2_match_data_free(mdata);
            }
            else
            {
                free(output);
                output = NULL;
            }
            pcre2_code_free(re);
        }
        else
        {
            free(output);
            output = NULL;
        }
    }
    return output;
}

/** 
 * Calculate the number of decimal numbers from a size_t value.
 * 
 * @param	value	value
 * 
 * @return	number of decimal numbers of which the value consists of
 * 		value==123 returns 3, for example.
 * @note 	Does the same as UINTLEN macro 
 */
size_t get_decimal_len(
	size_t value)
{
	return value > 0 ? (size_t) log10 ((double) value) + 1 : 1;
}

/**
 * Check if the provided pathname is POSIX-compliant. The valid characters
 * are [a-z A-Z 0-9._-].
 * @param path A null-terminated string
 * @return true if it is a POSIX-compliant pathname, otherwise false
 */
bool is_valid_posix_path(char* path)
{
  char* ptr = path;
  while (*ptr != '\0')
    {
      if (isalnum (*ptr) ||
          *ptr == '/' ||
          *ptr == '.' ||
          *ptr == '-' ||
          *ptr == '_')
        {
          ptr++;
        }
      else
        {
          return false;
        }
    }
  return true;
}

/**
 * Strip escape characters from a character string.
 * @param String to parse.
 * @return True if parsing was successful, false on errors.
 */
bool
strip_escape_chars (char* val)
{
  int cur, end;

  if (val == NULL)
    return false;

  end = strlen (val) + 1;
  cur = 0;

  while (cur < end)
    {
      if (val[cur] == '\\')
	{
	  memmove (val + cur, val + cur + 1,end - cur - 1);
	  end--;
	}
      cur++;
    }
  return true;
}

/**
* Calculate a hash value for a null-terminated string.
* @param key String to hash
* @return Hash value of the string
*/
int simple_str_hash(char* key)
{
  if(key == NULL){
    return 0;
  }
  int hash = 0,c = 0;
  char* ptr = key;
  while((c = *ptr++)){
    hash = c + (hash << 6) + (hash << 16) - hash;
  }

  return hash;
}
