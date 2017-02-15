/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/users.h>
#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/log_manager.h>

/**
 * @file users.c User table maintenance routines
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 23/06/2013   Mark Riddoch            Initial implementation
 * 08/01/2014   Massimiliano Pinto      In user_alloc now we can pass function pointers for
 *                                      copying/freeing keys and values independently via
 *                                      hashtable_memory_fns() routine
 *
 * @endverbatim
 */

USERS *users_alloc()
{
    USERS *rval;

    if ((rval = MXS_CALLOC(1, sizeof(USERS))) == NULL)
    {
        return NULL;
    }

    if ((rval->data = hashtable_alloc(USERS_HASHTABLE_DEFAULT_SIZE,
                                      hashtable_item_strhash, hashtable_item_strcmp)) == NULL)
    {
        MXS_ERROR("[%s:%d]: Memory allocation failed.", __FUNCTION__, __LINE__);
        MXS_FREE(rval);
        return NULL;
    }

    hashtable_memory_fns(rval->data,
                         hashtable_item_strdup, hashtable_item_strdup,
                         hashtable_item_free, hashtable_item_free);

    return rval;
}

void users_free(USERS *users)
{
    if (users)
    {
        hashtable_free(users->data);
        MXS_FREE(users);
    }
}

int users_add(USERS *users, const char *user, const char *auth)
{
    int add;

    atomic_add(&users->stats.n_adds, 1);
    add = hashtable_add(users->data, (char*)user, (char*)auth);
    atomic_add(&users->stats.n_entries, add);
    return add;
}

int users_delete(USERS *users, const char *user)
{
    int del;

    atomic_add(&users->stats.n_deletes, 1);
    del = hashtable_delete(users->data, (char*)user);
    atomic_add(&users->stats.n_entries, -del);
    return del;
}

const char *users_fetch(USERS *users, const char *user)
{
    atomic_add(&users->stats.n_fetches, 1);
    // TODO: Returning data from the hashtable is not threadsafe.
    return hashtable_fetch(users->data, (char*)user);
}

int users_update(USERS *users, const char *user, const char *auth)
{
    if (hashtable_delete(users->data, (char*)user) == 0)
    {
        return 0;
    }
    return hashtable_add(users->data, (char*)user, (char*)auth);
}


void usersPrint(const USERS *users)
{
    printf("Users table data\n");
    hashtable_stats(users->data);
}

void users_default_diagnostic(DCB *dcb, SERV_LISTENER *port)
{
    if (port->users && port->users->data)
    {
        HASHITERATOR *iter = hashtable_iterator(port->users->data);

        if (iter)
        {
            dcb_printf(dcb, "User names: ");
            char *sep = "";
            void *user;

            while ((user = hashtable_next(iter)) != NULL)
            {
                dcb_printf(dcb, "%s%s", sep, (char *)user);
                sep = ", ";
            }

            dcb_printf(dcb, "\n");
            hashtable_iterator_free(iter);
        }
    }
    else
    {
        dcb_printf(dcb, "Users table is empty\n");
    }
}

int users_default_loadusers(SERV_LISTENER *port)
{
    users_free(port->users);
    port->users = users_alloc();
    return MXS_AUTH_LOADUSERS_OK;
}
