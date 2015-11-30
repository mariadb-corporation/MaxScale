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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <users.h>
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

    if ((rval = calloc(1, sizeof(USERS))) == NULL)
    {
        MXS_ERROR("[%s:%d]: Memory allocation failed.", __FUNCTION__, __LINE__);
        return NULL;
    }

    if ((rval->data = hashtable_alloc(USERS_HASHTABLE_DEFAULT_SIZE, simple_str_hash, strcmp)) == NULL)
    {
        MXS_ERROR("[%s:%d]: Memory allocation failed.", __FUNCTION__, __LINE__);
        free(rval);
        return NULL;
    }

    hashtable_memory_fns(rval->data,
                         (HASHMEMORYFN)strdup,
                         (HASHMEMORYFN)strdup,
                         (HASHMEMORYFN)free,
                         (HASHMEMORYFN)free);

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
    if (users == NULL)
    {
        MXS_ERROR("[%s:%d]: NULL parameter.", __FUNCTION__, __LINE__);
        return;
    }

    if (users->data)
    {
        hashtable_free(users->data);
    }
    free(users);
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
 * The last user in the table can not be deleted
 *
 * @param users         The users table
 * @param user          The user name
 * @return      The number of users deleted from the table
 */
int
users_delete(USERS *users, char *user)
{
    int del;

    if (users->stats.n_entries == 1)
    {
        return 0;
    }
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
    HASHITERATOR *iter;
    char *sep;
    void *user;

    dcb_printf(dcb, "Users table data\n");

    if (users == NULL || users->data == NULL)
    {
        dcb_printf(dcb, "Users table is empty\n");
    }
    else
    {
        dcb_hashtable_stats(dcb, users->data);

        if ((iter = hashtable_iterator(users->data)) != NULL)
        {
            dcb_printf(dcb, "User names: ");
            sep = "";

            if (users->usersCustomUserFormat != NULL)
            {
                while ((user = hashtable_next(iter)) != NULL)
                {
                    char *custom_user;
                    custom_user = users->usersCustomUserFormat(user);
                    if (custom_user)
                    {
                        dcb_printf(dcb, "%s%s", sep, custom_user);
                        free(custom_user);
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
