#if !defined(SKYGW_UTILS_H)
#define SKYGW_UTILS_H

#include "skygw_types.h"
#include "skygw_debug.h"

typedef struct slist_node_st slist_node_t;
typedef struct slist_st slist_t;
typedef struct slist_cursor_st slist_cursor_t;
typedef struct simple_mutex_st simple_mutex_t;
typedef struct skygw_thread_st skygw_thread_t;
typedef struct skygw_message_st skygw_message_t;

typedef enum { THR_INIT, THR_RUNNING, THR_EXIT } skygw_thr_state_t;
typedef enum { MES_RC_FAIL, MES_RC_SUCCESS, MES_RC_TIMEOUT } skygw_mes_rc_t;

EXTERN_C_BLOCK_BEGIN

slist_cursor_t* slist_init(void);
void slist_done(slist_cursor_t* c);

void slcursor_add_data(slist_cursor_t* c, void* data);
void* slcursor_get_data(slist_cursor_t* c);

bool slcursor_move_to_begin(slist_cursor_t* c);
bool slcursor_step_ahead(slist_cursor_t* c);

skygw_thread_t* skygw_thread_init(
        char* name,
        void* (*sth_thrfun)(void* data),
        void* data);

EXTERN_C_BLOCK_END

void skygw_thread_start(skygw_thread_t* thr);
skygw_thr_state_t skygw_thread_get_state(skygw_thread_t* thr);
void skygw_thread_set_state(
        skygw_thread_t*  thr,
        skygw_thr_state_t state);
void* skygw_thread_get_data(skygw_thread_t* thr);
bool skygw_thread_must_exit(skygw_thread_t* thr);

simple_mutex_t* simple_mutex_init(char* name);
int simple_mutex_done(simple_mutex_t* sm);
int simple_mutex_lock(simple_mutex_t* sm, bool block);
int simple_mutex_unlock(simple_mutex_t* sm);

skygw_message_t* skygw_message_init(void);

void skygw_message_done(
        skygw_message_t* mes);

skygw_mes_rc_t skygw_message_send(
        skygw_message_t* mes);

void skygw_message_wait(
        skygw_message_t* mes);

skygw_mes_rc_t skygw_message_request(
        skygw_message_t* mes);
        
void skygw_message_reset(
        skygw_message_t* mes);


#endif /* SKYGW_UTILS_H */
