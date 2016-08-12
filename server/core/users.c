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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <users.h>
#include <maxscale/alloc.h>
#include <atomic.h>
#include <log_manager.h>

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

/**
 * Allocate a new users table
 *
 * @return The users table
 */
USERS *
users_alloc()
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

/**
 * Remove the users table
 *
 * @param users The users table to remove
 */
void
users_free(USERS *users)
{
    if (users)
    {
        hashtable_free(users->data);
        MXS_FREE(users);
    }
}

/**
 * Add a new user to the user table. The user name must be unique
 *
 * @param users         The users table
 * @param user          The user name
 * @param auth          The authentication data
 * @return      The number of users added to the table
 */
int
users_add(USERS *users, char *user, char *auth)
{
    int add;

    atomic_add(&users->stats.n_adds, 1);
    add = hashtable_add(users->data, user, auth);
    atomic_add(&users->stats.n_entries, add);
    return add;
}

/**
 * Delete a user from the user table.
 *
 * @param users         The users table
 * @param user          The user name
 * @return      The number of users deleted from the table
 */
int
users_delete(USERS *users, char *user)
{
    int del;

    atomic_add(&users->stats.n_deletes, 1);
    del = hashtable_delete(users->data, user);
    atomic_add(&users->stats.n_entries, -del);
    return del;
}

/**
 * Fetch the authentication data for a particular user from the users table
 *
 * @param users         The users table
 * @param user          The user name
 * @return      The authentication data or NULL on error
 */
char
*users_fetch(USERS *users, char *user)
{
    atomic_add(&users->stats.n_fetches, 1);
    return hashtable_fetch(users->data, user);
}

/**
 * Change the password data associated with a user in the users
 * table.
 *
 * @param users         The users table
 * @param user          The user name
 * @param auth          The new authentication details
 * @return Number of users updated
 */
int
users_update(USERS *users, char *user, char *auth)
{
    if (hashtable_delete(users->data, user) == 0)
    {
        return 0;
    }
    return hashtable_add(users->data, user, auth);
}

/**
 * Print details of the users storage mechanism
 *
 * @param users         The users table
 */
void
usersPrint(USERS *users)
{
    printf("Users table data\n");
    hashtable_stats(users->data);
}

/**
 * Print details of the users storage mechanism to a DCB
 *
 * @param dcb           DCB to print to
 * @param users         The users table
 */
void
dcb_usersPrint(DCB *dcb, USERS *users)
{
    if (users == NULL || users->data == NULL)
    {
        dcb_printf(dcb, "Users table is empty\n");
    }
    else
    {
        HASHITERATOR *iter;

        if ((iter = hashtable_iterator(users->data)) != NULL)
        {
            dcb_printf(dcb, "User names: ");
            char *sep = "";
            void *user;

            if (users->usersCustomUserFormat != NULL)
            {
                while ((user = hashtable_next(iter)) != NULL)
                {
                    char *custom_user;
                    custom_user = users->usersCustomUserFormat(user);
                    if (custom_user)
                    {
                        dcb_printf(dcb, "%s%s", sep, custom_user);
                        MXS_FREE(custom_user);
                        sep = ", ";
                    }
                }
            }
            else
            {
                while ((user = hashtable_next(iter)) != NULL)
                {
                    dcb_printf(dcb, "%s%s", sep, (char *)user);
                    sep = ", ";
                }
            }

            hashtable_iterator_free(iter);
        }
    }
    dcb_printf(dcb, "\n");
}

/**
 * @brief Default user loading function
 *
 * A generic key-value user table is allocated for the service.
 *
 * @param port Listener configuration
 * @return Always AUTH_LOADUSERS_OK
 */
int users_default_loadusers(SERV_LISTENER *port)
{
    users_free(port->users);
    port->users = users_alloc();
    return AUTH_LOADUSERS_OK;
}
