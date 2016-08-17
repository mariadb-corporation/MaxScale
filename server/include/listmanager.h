#ifndef _LISTMANAGER_H
#define _LISTMANAGER_H
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

/**
 * @file listmanager.h  The List Manager header file
 *
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 20/04/2016   Martin Brampton         Initial implementation
 *
 * @endverbatim
 */

#include <spinlock.h>
#include <skygw_debug.h>

struct dcb;

/*
 * The possible types of list that could be supported. At present, only
 * LIST_TYPE_RECYCLABLE is fully tested.
 */
typedef enum
{
    LIST_TYPE_SIMPLE,        /* A simple linked list with pointer to last entry */
    LIST_TYPE_RECYCLABLE,    /* A simple linked list, entries are recycled */
    LIST_TYPE_DOUBLE,        /* A doubly linked list, next and previous */
} list_type_t;

/*
 * The list entry structure
 *
 * Each list entry must have this format, but will typically be extended
 * well beyond these items.  The "previous" field is only required for
 * a doubly linked list, LIST_TYPE_DOUBLE.
 *
 * The first data to be used with the list manager is the DCB. Note that
 * the first few fields in the DCB structure correspond exactly to the
 * fields in the list entry structure. The pointers used need to be cast
 * as appropriate in order to be able to use the same data as either a DCB
 * or a list entry.
 *
 */
#define LIST_ENTRY_FIELDS \
    skygw_chk_t         list_entry_chk_top;     \
    struct  list_entry  *next;                  \
    struct  list_entry  *previous;              \
    bool                entry_is_in_use;        \
    bool                entry_is_ready;         \
    skygw_chk_t         list_entry_chk_tail;

typedef struct list_entry
{
    LIST_ENTRY_FIELDS
} list_entry_t;

/*
 * The list configuration structure
 *
 * This provides the basis for a list. It can be declared and initialised
 * statically, for example in server/core/dcb.c. It includes an anchor
 * pointer for the list, a pointer to the last entry in the list and the
 * last entry that was found to be free and reused.
 *
 * The count tells us the current number of entries in live use, and maximum
 * is the highest number ever observed in live use. The freecount is the
 * number of entries currently free and ready for reuse. The entry_size is
 * the actual size of the real entries, e.g. the DCB structure (NOT the size
 * of the list entry structure).
 *
 * A spinlock is declared for use during list manipulation operations.
 */
typedef struct
{
    list_type_t     list_type;
    size_t          entry_size;
    SPINLOCK        list_lock;
    list_entry_t    *all_entries;
    list_entry_t    *last_entry;
    list_entry_t    *last_free;
    int             count;
    int             maximum;
    int             freecount;
    int             num_malloc;
} LIST_CONFIG;

void list_initialise(LIST_CONFIG *list_config, list_type_t type_of_list, size_t entry_size);
bool list_pre_alloc(LIST_CONFIG *list_config, int num_entries, void (*init_struct)(void *));
list_entry_t *list_find_free(LIST_CONFIG *list_config, void (*init_struct)(void *));
void dprintListStats(struct dcb *pdcb, LIST_CONFIG *list_config, const char *listname);
void list_free_entry (LIST_CONFIG *list_config, list_entry_t *to_be_freed);
list_entry_t *list_start_iteration(LIST_CONFIG *list_config);
list_entry_t *list_iterate(LIST_CONFIG *list_config, list_entry_t *current_entry);
void list_terminate_iteration_early(LIST_CONFIG *list_config, list_entry_t *current_entry);
bool list_is_entry_in_use(LIST_CONFIG *list_config, list_entry_t *to_be_found);
void list_add_to_end(LIST_CONFIG *list_config, list_entry_t *new_entry);
void list_map(LIST_CONFIG *list_config, bool (*callback)(void *, ...));

/* The following UNTESTED! */
list_entry_t *list_remove_first(LIST_CONFIG *list_config);
list_entry_t *list_remove_last(LIST_CONFIG *list_config);


#endif /* LISTMANAGER_H */
