#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include "skygw_debug.h"
#include "skygw_types.h"
#include "skygw_utils.h"



/** Single-linked list for storing test cases */

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

struct simple_mutex_st {
        skygw_chk_t      sm_chk_top;
        pthread_mutex_t  sm_mutex;
        pthread_t        sm_owner;
        bool             sm_locked;
        bool             sm_enabled;
        char*            sm_name;
        skygw_chk_t      sm_chk_tail;
};
        
struct skygw_thread_st {
        skygw_chk_t       sth_chk_top;
        bool              sth_must_exit;
        pthread_t         sth_parent;
        pthread_t         sth_thr;
        int               sth_errno;
        skygw_thr_state_t sth_state;
        char*             sth_name;
        void* (*sth_thrfun)(void* data);
        void*             sth_data;
        skygw_chk_t       sth_chk_tail;
};

struct skygw_message_st {
        skygw_chk_t     mes_chk_top;
        bool            mes_sent;
        pthread_mutex_t mes_mutex;
        pthread_cond_t  mes_cond;
        skygw_chk_t     mes_chk_tail;
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

/** 
 * @node Initialize thread data structure 
 *
 * Parameters:
 * @param void - <usage>
 *          <description>
 *
 * @param sth_thrfun - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
skygw_thread_t* skygw_thread_init(
        char* name,
        void* (*sth_thrfun)(void* data),
        void* data)
{
        skygw_thread_t* th =
            (skygw_thread_t *)calloc(1, sizeof(skygw_thread_t));
        th->sth_chk_top = CHK_NUM_THREAD;
        th->sth_chk_tail = CHK_NUM_THREAD;
        th->sth_parent = pthread_self();
        th->sth_state = THR_INIT;
        th->sth_name = name;
        th->sth_thrfun = sth_thrfun;
        th->sth_data = data;
        CHK_THREAD(th);
        
        return th;
}

void skygw_thread_start(
    skygw_thread_t* thr)
{
        int err;
        
        CHK_THREAD(thr);
        err = pthread_create(&thr->sth_thr,
                             NULL,
                             thr->sth_thrfun,
                             thr);
        
        if (err != 0) {
            fprintf(stderr,
                    "FATAL: starting file writer thread failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
            perror("file writer thread : ");
        }
        ss_dfprintf(stderr, "Started %s thread\n", thr->sth_name);
}

skygw_thr_state_t skygw_thread_get_state(
        skygw_thread_t* thr)
{
        CHK_THREAD(thr);
        return thr->sth_state;
}


void skygw_thread_set_state(
        skygw_thread_t*   thr,
        skygw_thr_state_t state)
{
        CHK_THREAD(thr);
        ss_dassert(!thr->sth_must_exit);
        thr->sth_state = state;
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
        return thr->sth_must_exit;
}

simple_mutex_t* simple_mutex_init(
    char* name)
{
        int err;
        
        simple_mutex_t* sm;

        sm = (simple_mutex_t *)calloc(1, sizeof(simple_mutex_t));
        ss_dassert(sm != NULL);
        err = pthread_mutex_init(&sm->sm_mutex, NULL);
        
        if (err != 0) {
            fprintf(stderr,
                    "FATAL : initializing simple mutex %s failed, "
                    "errno %d : %s\n",
                    name, 
                    err,
                    strerror(errno));
            perror("simple_mutex : ");
            sm = NULL;
        }
        sm->sm_chk_top = CHK_NUM_SIMPLE_MUTEX;
        sm->sm_chk_tail = CHK_NUM_SIMPLE_MUTEX;
        sm->sm_name = strdup(name);
        sm->sm_enabled = TRUE;
        CHK_SIMPLE_MUTEX(sm);
        ss_dfprintf(stderr, "Initialized simple mutex %s.\n", name);
        return sm;
}

int simple_mutex_done(
        simple_mutex_t* sm)
{
        int err;
        
        CHK_SIMPLE_MUTEX(sm);
        err = simple_mutex_lock(sm, FALSE);

        if (err != 0) {
            goto return_err;
        }
        sm->sm_enabled = FALSE;
        err = simple_mutex_unlock(sm);

        if (err != 0) {
            goto return_err;
        }
        err = pthread_mutex_destroy(&sm->sm_mutex);

return_err:
        if (err != 0) {
            fprintf(stderr,
                    "FATAL : destroying simple mutex %s failed, "
                    "errno %d : %s\n",
                    sm->sm_name, 
                    err,
                    strerror(errno));
            perror("simple_mutex : ");
        }
        return err;
}

int simple_mutex_lock(
        simple_mutex_t* sm,
        bool            block)
{
        int err;

        if (block) {
            err = pthread_mutex_lock(&sm->sm_mutex);
        } else {
            err = pthread_mutex_trylock(&sm->sm_mutex);
        }

        if (err != 0) {
            fprintf(stderr,
                    "INFO : Locking simple mutex %s failed, "
                    "errno %d : %s\n",
                    sm->sm_name, 
                    err,
                    strerror(errno));
            perror("simple_mutex : ");
        }
        return err;
}

int simple_mutex_unlock(
        simple_mutex_t* sm)
{
        int err;

        err = pthread_mutex_unlock(&sm->sm_mutex);

        if (err != 0) {
            fprintf(stderr,
                    "INFO : locking simple mutex %s failed, "
                    "errno %d : %s\n",
                    sm->sm_name, 
                    err,
                    strerror(errno));
            perror("simple_mutex : ");
        }
        return err;
}

skygw_message_t* skygw_message_init(void)
{
        int err;
        skygw_message_t* mes;

        mes = (skygw_message_t*)calloc(1, sizeof(skygw_message_t));
        mes->mes_chk_top = CHK_NUM_MESSAGE;
        mes->mes_chk_tail = CHK_NUM_MESSAGE;
        err = pthread_mutex_init(&(mes->mes_mutex), NULL);
        
        if (err != 0) {
            fprintf(stderr,
                    "FATAL : initializing pthread mutex failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
            mes = NULL;
            goto return_mes;
        }
        err = pthread_cond_init(&(mes->mes_cond), NULL);

        if (err != 0) {
            fprintf(stderr,
                    "FATAL : initializing pthread cond var failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
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
        
        CHK_MESSAGE(mes);
        err = pthread_cond_destroy(&(mes->mes_cond));

        if (err != 0) {
            fprintf(stderr,
                    "FATAL : destroying cond var failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
        }
        ss_dassert(err == 0);
        err = pthread_mutex_destroy(&(mes->mes_mutex));

        if (err != 0) {
            fprintf(stderr,
                    "FATAL : destroying pthread mutex failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
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
            fprintf(stderr,
                    "INFO : Locking pthread mutex failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
            goto return_mes_rc;
        }
        mes->mes_sent = TRUE;
        err = pthread_cond_signal(&(mes->mes_cond));

        if (err != 0) {
            fprintf(stderr,
                    "INFO : Signaling pthread cond var failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
            goto return_mes_rc;
        }
        err = pthread_mutex_unlock(&(mes->mes_mutex));

        if (err != 0) {
            fprintf(stderr,
                    "INFO : Unlocking pthread mutex failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
            goto return_mes_rc;
        }
        rc = MES_RC_SUCCESS;
        
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
            fprintf(stderr,
                    "INFO : Locking pthread mutex failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
        }
        ss_dassert(err == 0);
        
        while (!mes->mes_sent) {
            err = pthread_cond_wait(&(mes->mes_cond), &(mes->mes_mutex));

            if (err != 0) {
                fprintf(stderr,
                        "INFO : Locking pthread cond wait failed, "
                        "errno %d : %s\n",
                        err,
                        strerror(errno));
            }
        }
        mes->mes_sent = FALSE;
        err = pthread_mutex_unlock(&(mes->mes_mutex));
        
        if (err != 0) {
            fprintf(stderr,
                    "INFO : Unlocking pthread mutex failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
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
            fprintf(stderr,
                    "INFO : Locking pthread mutex failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
            goto return_mes_rc;
        }
        ss_dassert(err == 0);
        mes->mes_sent = FALSE;
        err = pthread_mutex_unlock(&(mes->mes_mutex));

        if (err != 0) {
            fprintf(stderr,
                    "INFO : Unlocking pthread mutex failed, "
                    "errno %d : %s\n",
                    err,
                    strerror(errno));
            goto return_mes_rc;
        }
return_mes_rc:
        ss_dassert(err == 0);
}
