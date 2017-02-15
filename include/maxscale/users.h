#pragma once
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

/**
 * @file users.h The functions to manipulate the table of users maintained
 * for each service
 */

#include <maxscale/cdefs.h>
#include <maxscale/hashtable.h>
#include <maxscale/dcb.h>
#include <maxscale/listener.h>
#include <maxscale/service.h>
#include <openssl/sha.h>

MXS_BEGIN_DECLS

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
 * A generic user table containing the username and authentication data
 */
typedef struct users
{
    HASHTABLE *data;                        /**< The hashtable containing the actual data */
    USERS_STATS stats;                      /**< The statistics for the users table */
} USERS;


/**
 * Allocate a new users table
 *
 * @return The users table
 */
USERS *users_alloc();

/**
 * Remove the users table
 *
 * @param users The users table to remove
 */
void users_free(USERS *);

/**
 * Add a new user to the user table. The user name must be unique
 *
 * @param users         The users table
 * @param user          The user name
 * @param auth          The authentication data
 * @return      The number of users added to the table
 */
int users_add(USERS *, const char *, const char *);

/**
 * Delete a user from the user table.
 *
 * @param users         The users table
 * @param user          The user name
 * @return      The number of users deleted from the table
 */
int users_delete(USERS *, const char *);

/**
 * Fetch the authentication data for a particular user from the users table
 *
 * @param users         The users table
 * @param user          The user name
 * @return      The authentication data or NULL on error
 */
const char *users_fetch(USERS *, const char *);

/**
 * Change the password data associated with a user in the users
 * table.
 *
 * @param users         The users table
 * @param user          The user name
 * @param auth          The new authentication details
 * @return Number of users updated
 */
int users_update(USERS *, const char *, const char *);

/**
 * @brief Default user loading function
 *
 * A generic key-value user table is allocated for the service.
 *
 * @param port Listener configuration
 * @return Always AUTH_LOADUSERS_OK
 */
int users_default_loadusers(SERV_LISTENER *port);

/**
 * @brief Default authenticator diagnostic function
 *
 * @param dcb DCB where data is printed
 * @param port Port whose data is to be printed
 */
void users_default_diagnostic(DCB *dcb, SERV_LISTENER *port);

/**
 * Print details of the users storage mechanism
 *
 * @param users         The users table
 */
void usersPrint(const USERS *);

MXS_END_DECLS
