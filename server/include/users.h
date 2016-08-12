#ifndef _USERS_H
#define _USERS_H
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
#include <hashtable.h>
#include <dcb.h>
#include <listener.h>
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
extern int users_default_loadusers(SERV_LISTENER *port); /**< A generic implementation of the authenticator
                                                          * loadusers entry point */
extern void usersPrint(USERS *);                  /**< Print data about the users loaded */
extern void dcb_usersPrint(DCB *, USERS *);       /**< Print data about the users loaded */

#endif
