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
 * @file listmanager.c  -  Logic for list handling
 *
 * MaxScale contains a number of linked lists. This code attempts to provide
 * standard functions for handling them. Initially, the main work has been
 * on recyclable lists - lists of entries that use dynamically allocated
 * memory but are reused rather than freed. Some functions are not fully
 * tested - see comments.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 20/04/16     Martin Brampton         Initial implementation
 *
 * @endverbatim
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <maxscale/listmanager.h>
#include <maxscale/spinlock.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/alloc.h>

/**
 * Initialise a list configuration.
 *
 * @param list_config   Pointer to the configuration of the list to be initialised
 * @param type_of_list  Type of list to be initialised.
 * @param entry_size    The size of each list entry (typically from sizeof).
 *
 * @note    This is only required if a list is configured at execution time
 *          rather than being declared and statically initialized.
 */
void
list_initialise(LIST_CONFIG *list_config, list_type_t type_of_list, size_t entry_size)
{
    list_config->list_type = type_of_list;
    list_config->all_entries = NULL;
    list_config->last_entry = NULL;
    list_config->last_free = NULL;
    list_config->count = 0;
    list_config->maximum = 0;
    list_config->freecount = 0;
    list_config->num_malloc = 0;
    list_config->entry_size = entry_size;
    spinlock_init(&list_config->list_lock);
}

/**
 * Allocate memory for some initial list entries
 *
 * The caller must check the return value. If it is false, this most likely
 * indicates a memory allocation failure, and the caller should act
 * accordingly. (It could indicate that the requested number of entries
 * was less than 1, but this is fairly unlikely).
 *
 * @param list_config   Pointer to the configuration of the list to be initialised
 * @param num_entries   Number of list entries to be allocated
 * @return true if memory was allocated, false if not (or num_entries < 1)
 *
 */
bool
list_pre_alloc(LIST_CONFIG *list_config, int num_entries, void (*init_struct)(void *))
{
    uint8_t *entry_space;
    bool result;

    spinlock_acquire(&list_config->list_lock);
    if (num_entries <= 0)
    {
        MXS_ERROR("Attempt to preallocate space for recyclable list asked for no entries");
        result = false;
    }
    else if ((entry_space = (uint8_t *)MXS_CALLOC(num_entries, list_config->entry_size)) == NULL)
    {
        result = false;
    }
    else
    {
        list_entry_t *first_new_entry = (list_entry_t *)entry_space;
        list_entry_t *previous = first_new_entry;

        for (int i = 1; i <= num_entries; i++)
        {
            if (init_struct)
            {
                init_struct((void *)previous);
            }
            previous->list_entry_chk_top = CHK_NUM_MANAGED_LIST;
            previous->list_entry_chk_tail = CHK_NUM_MANAGED_LIST;
            list_entry_t *next_entry = (list_entry_t *)((uint8_t *)previous + list_config->entry_size);
            if (i < num_entries)
            {
                previous->next = next_entry;
                previous = next_entry;
            }
            else
            {
                previous->next = NULL;
            }
        }
        list_config->freecount += num_entries;
        list_add_to_end(list_config, first_new_entry);
        list_config->last_entry = previous;
        list_config->last_free = first_new_entry;
        result = true;
    }
    spinlock_release(&list_config->list_lock);
    return result;
}

/**
 * @brief   Find a free list entry or allocate memory for a new one.
 *
 * This routine looks to see whether there are free entries.
 * If not, new memory is allocated, if possible, and the new entry is added to
 * the list of all entries.
 *
 * @param   Pointer to the list configuration structure
 * @return  An available entry or NULL if none could be found or created.
 */
list_entry_t *
list_find_free(LIST_CONFIG *list_config, void (*init_struct)(void *))
{
    list_entry_t *available_entry;

    spinlock_acquire(&list_config->list_lock);
    if (list_config->freecount <= 0)
    {
        /* No free entries, need to allocate a new one */
        if ((available_entry = MXS_CALLOC(1, list_config->entry_size)) == NULL)
        {
            spinlock_release(&list_config->list_lock);
            return NULL;
        }
        list_config->num_malloc++;

        if (init_struct)
        {
            init_struct((void *)available_entry);
        }
        available_entry->list_entry_chk_top = CHK_NUM_MANAGED_LIST;
        available_entry->list_entry_chk_tail = CHK_NUM_MANAGED_LIST;
        available_entry->next = NULL;
        list_add_to_end(list_config, available_entry);
    }
    /* Starting at the last place a free DCB was found, loop through the */
    /* list of DCBs searching for one that is not in use. */
    else
    {
        list_entry_t *next_in_list;
        int loopcount = 0;

        while (list_config->last_free->entry_is_in_use)
        {
            list_config->last_free = list_config->last_free->next;
            if (NULL == list_config->last_free)
            {
                loopcount++;
                ss_dassert(loopcount == 1);
                if (loopcount > 1)
                {
                    /* Shouldn't need to loop round more than once */
                    MXS_ERROR("Find free list entry failed to find when count positive");
                    spinlock_release(&list_config->list_lock);
                    return NULL;
                }
                list_config->last_free = list_config->all_entries;
            }
        }
        list_config->freecount--;
        available_entry = list_config->last_free;
        /* Clear the old data, then reset the list forward link */
        next_in_list = available_entry->next;
        if (init_struct)
        {
            init_struct((void *)available_entry);
        }
        else
        {
            memset(available_entry, 0, list_config->entry_size);
        }
        available_entry->list_entry_chk_top = CHK_NUM_MANAGED_LIST;
        available_entry->list_entry_chk_tail = CHK_NUM_MANAGED_LIST;
        available_entry->next = next_in_list;
    }
    list_config->count++;
    if (list_config->count > list_config->maximum)
    {
        list_config->maximum = list_config->count;
    }
    available_entry->entry_is_in_use = true;
    spinlock_release(&list_config->list_lock);
    return available_entry;
}

/**
 * @brief   Display information about a recyclable list
 *
 * Should be called from a module that handles one specific list, passing
 * the name for the list as well as the print DCB and the list config.
 *
 * @param   Pointer to the print DCB
 * @param   List configuration pointer, the list to be displayed
 * @param   Name for the list
 */
void
dprintListStats(DCB *pdcb, LIST_CONFIG *list_config, const char *listname)
{
    dcb_printf(pdcb, "Recyclable list statistics\n");
    dcb_printf(pdcb, "--------------------------\n");
    dcb_printf(pdcb, "Name of list: %s\n", listname);
    dcb_printf(pdcb, "Size of entries:              %zu\n", list_config->entry_size);
    dcb_printf(pdcb, "Currently in use:             %d\n", list_config->count);
    dcb_printf(pdcb, "Maximum ever used at once:    %d\n", list_config->maximum);
    dcb_printf(pdcb, "Currently free for reuse:     %d\n", list_config->freecount);
    dcb_printf(pdcb, "Total in use + free:          %d\n",
               list_config->freecount + list_config->count);
    dcb_printf(pdcb, "Number of memory allocations: %d\n", list_config->num_malloc);
}

/**
 * @brief   Dispose of a list entry by making it available for reuse.
 *
 * Within spinlock control, the entry is marked not in use and the
 * counts are adjusted.
 *
 * @param   Pointer to the list configuration structure
 * @param   List entry pointer, the item to be "freed"
 */
void
list_free_entry(LIST_CONFIG *list_config, list_entry_t *to_be_freed)
{
    spinlock_acquire(&list_config->list_lock);
    to_be_freed->entry_is_in_use = false;
    list_config->freecount++;
    list_config->count--;
    spinlock_release(&list_config->list_lock);
}

/**
 * @brief   Find out whether a pointer points to a valid list entry
 *
 * Search the list for the given entry, under spinlock control.
 *
 * @param   Pointer to the list configuration structure
 * @param   Pointer to be searched for
 * @return  True if the pointer is in the list and it is in use
 */
bool
list_is_entry_in_use(LIST_CONFIG *list_config, list_entry_t *to_be_found)
{
    list_entry_t *entry;

    spinlock_acquire(&list_config->list_lock);
    entry = list_config->all_entries;
    while (entry && to_be_found != entry)
    {
        entry = entry->next;
    }
    spinlock_release(&list_config->list_lock);

    return (entry && entry->entry_is_in_use);
}

/**
 * @brief   Invoke a callback for every active member of list
 *
 * The list is locked, list entries that are in use are successively
 * submitted to the callback function. The list entry is supplied to the
 * callback function as the first parameters, followed by whatever other
 * parameters have been passed to list_map. The process will continue so
 * long as the callback function returns true, and will terminate either
 * at the end of the list or when the callback function returns false.
 *
 * Code to be developed.
 *
 * @param   Pointer to the list configuration structure
 * @param   Pointer to the callback function
 */
void
list_map(LIST_CONFIG *list_config, bool (*callback)(void *, ...))
{

}

/**
 * @brief   Start to iterate over a list
 *
 * The list is locked, and the first entry returned to the caller
 *
 * @param   Pointer to the list configuration structure
 * @return  Pointer to the first entry in the list
 */
list_entry_t *
list_start_iteration(LIST_CONFIG *list_config)
{
    spinlock_acquire(&list_config->list_lock);
    return list_config->all_entries;
}

/**
 * @brief   Iterate over a list from a given point
 *
 * The list is assumed locked through list_start_iteration having been
 * called. The next entry that is currently in use is returned to the caller.
 * If the end of the list is reached, the spinlock is freed.
 *
 * @param   Pointer to the list configuration structure
 * @param   Pointer to the entry to move forward from
 * @return  The next item in the list, or NULL if reached the end
 */
list_entry_t *
list_iterate(LIST_CONFIG *list_config, list_entry_t *current_entry)
{
    list_entry_t *next_entry = current_entry->next;
    while (next_entry && !(next_entry->entry_is_in_use && next_entry->entry_is_ready))
    {
        next_entry = next_entry->next;
    }
    if (NULL == next_entry)
    {
        spinlock_release(&list_config->list_lock);
    }
    return next_entry;
}

/**
 * @brief   Terminate list iteration before reaching the end
 *
 * The list is assumed locked through list_start_iteration having been
 * called, unless the last item is NULL, in which case it is assumed that
 * the iteration had already reached the end of the list. If this is not
 * the case, then the spinlock is released.
 *
 * @param   Pointer to the list configuration structure
 * @param   Pointer to the entry last reached in the iteration
 */
void
list_terminate_iteration_early(LIST_CONFIG *list_config, list_entry_t *current_entry)
{
    if (current_entry)
    {
        spinlock_release(&list_config->list_lock);
    }
}

/**
 * Add a new item to the end of a list.
 *
 * Must be called with the list lock held.
 *
 * A pointer, last_entry, is held to find the end of the list, and the new entry
 * is linked to the end of the list.  The pointer, last_free, that is used to
 * search for a free entry is initialised if not already set. There cannot be
 * any free entries until this routine has been called at least once.
 *
 * @param list_config    The configuration of the list.
 * @param new_entry      The new entry to be added to the end of the list
 *
 * @note    UNTESTED for simple or doubly linked lists, currently used
 *          internally for recyclable lists.
 */
void
list_add_to_end(LIST_CONFIG *list_config, list_entry_t *new_entry)
{
    if (NULL == list_config->all_entries)
    {
        list_config->all_entries = new_entry;
        if (LIST_TYPE_DOUBLE == list_config->list_type)
        {
            new_entry->previous = NULL;
        }
    }
    else
    {
        list_config->last_entry->next = new_entry;
        if (LIST_TYPE_DOUBLE == list_config->list_type)
        {
            new_entry->previous = list_config->last_entry;
        }
    }
    list_config->last_entry = new_entry;
    if (NULL == list_config->last_free)
    {
        list_config->last_free = new_entry;
    }
}

/**
 * @brief the list entry removed from the start of the list
 *
 * Must be called with the list lock held.
 *
 * @return The first list entry or NULL if the list is empty.
 *
 * @note UNTESTED! Intended for use on simple or doubly linked lists.
 */
list_entry_t *
list_remove_first(LIST_CONFIG *list_config)
{
    list_entry_t *first_in_list = NULL;
    if (list_config->all_entries)
    {
        first_in_list = list_config->all_entries;
        list_config->all_entries = first_in_list->next;
    }
    return first_in_list;
}

/**
 * @brief Return the list entry removed from the end of the list
 *
 * Must be called with the list lock held.
 *
 * @return The last list entry or NULL if the list is empty.
 *
 * @note UNTESTED! Intended for use only with doubly linked lists.
 */
list_entry_t *
list_remove_last(LIST_CONFIG *list_config)
{
    list_entry_t *last_in_list = NULL;
    if (list_config->list_type != LIST_TYPE_DOUBLE)
    {
        MXS_ERROR("Attempt to remove the last entry in a list that is not doubly linked");
        return NULL;
    }
    if (list_config->all_entries)
    {
        last_in_list = list_config->last_entry;
        list_config->last_entry = last_in_list->previous;
    }
    return last_in_list;
}
