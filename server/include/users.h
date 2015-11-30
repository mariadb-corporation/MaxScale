#ifndef _USERS_H
#define _USERS_H
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
#include <hashtable.h>
#include <dcb.h>
#include <openssl/sha.h>

/**
 * @file users.h The functions to manipulate the table of users maintained
 * for each service
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 23/06/13     Mark Riddoch            Initial implementation
 * 26/02/14     Massimiliano Pinto      Added checksum to users' table with SHA1
 * 27/02/14     Massimiliano Pinto      Added USERS_HASHTABLE_DEFAULT_SIZE
 * 28/02/14     Massimiliano Pinto      Added usersCustomUserFormat, optional username format routine
 *
 * @endverbatim
 */

#define USERS_HASHTABLE_DEFAULT_SIZE 52

/**
 * The users table statistics structure
 */
typedef struct
{
    int n_entries;              /**< The number of entries */
    int n_adds;                 /**< The number of inserts */
    int n_deletes;              /**< The number of deletes */
    int n_fetches;              /**< The number of fetchs */
} USERS_STATS;

/**
 * The user table, this contains the username and authentication data required
 * for the authentication implementation within the gateway.
 */
typedef struct users
{
    HASHTABLE *data;                        /**< The hashtable containing the actual data */
    char *(*usersCustomUserFormat)(void *); /**< Optional username format routine */
    USERS_STATS stats;                      /**< The statistics for the users table */
    unsigned char cksum[SHA_DIGEST_LENGTH]; /**< The users' table ckecksum */
} USERS;

extern USERS *users_alloc();                      /**< Allocate a users table */
extern void users_free(USERS *);                  /**< Free a users table */
extern int users_add(USERS *, char *, char *);    /**< Add a user to the users table */
extern int users_delete(USERS *, char *);         /**< Delete a user from the users table */
extern char *users_fetch(USERS *, char *);        /**< Fetch the authentication data for a user */
extern int users_update(USERS *, char *, char *); /**< Change the password data for a user in
                                                     the users table */
extern void usersPrint(USERS *);                  /**< Print data about the users loaded */
extern void dcb_usersPrint(DCB *, USERS *);       /**< Print data about the users loaded */

#endif
