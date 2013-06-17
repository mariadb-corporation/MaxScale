#if !defined(SKYGW_UTILS_H)
#define SKYGW_UTILS_H

#include "skygw_types.h"
#include "skygw_debug.h"

EXTERN_C_BLOCK_BEGIN

typedef struct slist_node_st slist_node_t;
typedef struct slist_st slist_t;
typedef struct slist_cursor_st slist_cursor_t;

slist_cursor_t* slist_init(void);
void slist_done(slist_cursor_t* c);

void slcursor_add_data(slist_cursor_t* c, void* data);
void* slcursor_get_data(slist_cursor_t* c);

bool slcursor_move_to_begin(slist_cursor_t* c);
bool slcursor_step_ahead(slist_cursor_t* c);

EXTERN_C_BLOCK_END



#endif /* SKYGW_UTILS_H */
