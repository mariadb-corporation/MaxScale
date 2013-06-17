#include <stdio.h>
#include <stdlib.h>

#include "skygw_debug.h"
#include "skygw_types.h"
#include "skygw_utils.h"



/** Single-linked list for storing test cases */
typedef struct slist_node_st slist_node_t;
typedef struct slist_st slist_t;
typedef struct slist_cursor_st slist_cursor_t;

struct slist_node_st {
        skygw_chk_t   slnode_chk_top;
        slist_t*      slnode_list;
        slist_node_t* slnode_next;
        void*         slnode_data;
        size_t        slnode_cursor_refcount;
        skygw_chk_t   slnode_chk_tail;
};

struct slist_st {
        skygw_chk_t   slist_chk_top;
        slist_node_t* slist_head;
        slist_node_t* slist_tail;
        size_t        slist_nelems;
        slist_t*      slist_cursors_list;
        skygw_chk_t   slist_chk_tail;
};

struct slist_cursor_st {
        skygw_chk_t   slcursor_chk_top;
        slist_t*      slcursor_list;
        slist_node_t* slcursor_pos;
        skygw_chk_t   slcursor_chk_tail;
};


/** End of structs and types */


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

static slist_node_t* slist_node_get_next(
        slist_node_t* curr_node);

static slist_node_t* slist_get_first(
        slist_t* list);

static slist_cursor_t* slist_get_cursor(
        slist_t* list);

/** End of static function declarations */


static slist_t* slist_init_ex(
        bool create_cursors)
{
        slist_t* list;
        
        list = (slist_t*)calloc(1, sizeof(slist_t));
        list->slist_chk_top = CHK_NUM_SLIST;
        list->slist_chk_tail = CHK_NUM_SLIST;

        if (create_cursors) {
            list->slist_cursors_list = slist_init_ex(FALSE);
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

static slist_cursor_t* slist_get_cursor(
        slist_t* list)
{
        CHK_SLIST(list);
        
        slist_cursor_t* c;

        c = slist_cursor_init(list);
        return c;
}



/** 
 * @node Create a cursor and a list with cursors supported 
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

        list = slist_init_ex(TRUE);
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
 * @return TRUE if there is first node in the list
 * FALSE is the list is empty.
 *
 * 
 * @details (write detailed description here)
 *
 */
bool slcursor_move_to_begin(
        slist_cursor_t* c)
{
        bool     succp = TRUE;
        slist_t* list;
        
        CHK_SLIST_CURSOR(c);
        list = c->slcursor_list;
        CHK_SLIST(list);
        c->slcursor_pos = list->slist_head;
        if (c->slcursor_pos == NULL) {
            succp = FALSE;
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
 * @return TRUE in success, FALSE is there is no next node on the list.
 *
 * 
 * @details (write detailed description here)
 *
 */
bool slcursor_step_ahead(
        slist_cursor_t* c)
{
        bool          succp = FALSE;
        slist_node_t* node;
        CHK_SLIST_CURSOR(c);
        CHK_SLIST_NODE(c->slcursor_pos);
        
        node = c->slcursor_pos->slnode_next;
        
        if (node != NULL) {
            CHK_SLIST_NODE(node);
            c->slcursor_pos = node;
            succp = TRUE;
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
        pos = c->slcursor_pos;
        
        if (pos != NULL) {
            CHK_SLIST_NODE(pos);
            pos = list->slist_tail->slnode_next;
        }
        ss_dassert(pos == NULL);        
        pos = slist_node_init(data, c);
        slist_add_node(list, pos);
        CHK_SLIST(list);
        CHK_SLIST_CURSOR(c);
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
