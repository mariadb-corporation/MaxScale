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

/**
 * @file dbusers.c  - Loading MySQL users from a MySQL backend server, this needs libmysqlclient.so and header files
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 24/06/2013	Massimiliano Pinto	Initial implementation
 * 08/08/2013	Massimiliano Pinto	Fixed bug for invalid memory access in row[1]+1 when row[1] is ""
 * 06/02/2014	Massimiliano Pinto	Mysql user root selected based on configuration flag
 * 26/02/2014	Massimiliano Pinto	Addd: replace_mysql_users() routine may replace users' table based on a checksum
 * 28/02/2014	Massimiliano Pinto	Added Mysql user@host authentication
 * 29/09/2014	Massimiliano Pinto	Added Mysql user@host authentication with wildcard in IPv4 hosts:
 *					x.y.z.%, x.y.%.%, x.%.%.%
 * 03/10/14	Massimiliano Pinto	Added netmask to user@host authentication for wildcard in IPv4 hosts
 * 13/10/14	Massimiliano Pinto	Added (user@host)@db authentication
 *
 * @endverbatim
 */

#include <stdio.h>
#include <mysql.h>

#include <dcb.h>
#include <service.h>
#include <users.h>
#include <dbusers.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <secrets.h>
#include <mysql_client_server_protocol.h>

#define USERS_QUERY_NO_ROOT " AND user NOT IN ('root')"
#define LOAD_MYSQL_USERS_QUERY "SELECT user, host, password, concat(user,host,password,Select_priv) AS userdata, Select_priv AS anydb FROM mysql.user WHERE user IS NOT NULL AND user <> ''"
#define MYSQL_USERS_COUNT "SELECT COUNT(1) AS nusers FROM mysql.user"

#define MYSQL_USERS_WITH_DB_ORDER " ORDER BY host DESC"
#define LOAD_MYSQL_USERS_WITH_DB_QUERY "SELECT user.user AS user,user.host AS host,user.password AS password,concat(user.user,user.host,user.password,user.Select_priv) AS userdata, user.Select_priv AS anydb,db.db AS db FROM mysql.user LEFT JOIN mysql.db ON user.user=db.user AND user.host=db.host WHERE user.user IS NOT NULL AND user.user <> ''" MYSQL_USERS_WITH_DB_ORDER

#define LOAD_MYSQL_USERS_WITH_DB_QUERY_NO_ROOT "SELECT * FROM (" LOAD_MYSQL_USERS_WITH_DB_QUERY ") AS t1 WHERE user NOT IN ('root')" MYSQL_USERS_WITH_DB_ORDER

extern int lm_enabled_logfiles_bitmask;

static int getUsers(SERVICE *service, USERS *users);
static int uh_cmpfun( void* v1, void* v2);
static void *uh_keydup(void* key);
static void uh_keyfree( void* key);
static int uh_hfun( void* key);
void *mysql_users_fetch(USERS *users, MYSQL_USER_HOST *key);
char *mysql_format_user_entry(void *data);
int add_mysql_users_with_host_ipv4(USERS *users, char *user, char *host, char *passwd, char *anydb, char *db);
static int getDatabases(SERVICE *);

/**
 * Load the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 *
 * @param service   The current service
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 */
int 
load_mysql_users(SERVICE *service)
{
	return getUsers(service, service->users);
}

int mysql_users_load_dbs(SERVICE *service) {
	return getDatabases(service);
}

/**
 * Reload the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 *
 * @param service   The current service
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 */
int 
reload_mysql_users(SERVICE *service)
{
int		i;
USERS		*newusers, *oldusers;

	if ((newusers = mysql_users_alloc()) == NULL)
		return 0;
	i = getUsers(service, newusers);
	spinlock_acquire(&service->spin);
	oldusers = service->users;
	service->users = newusers;
	spinlock_release(&service->spin);
	users_free(oldusers);

	return i;
}

/**
 * Replace the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 * The replacement is succesful only if the users' table checksums differ
 *
 * @param service   The current service
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 */
int 
replace_mysql_users(SERVICE *service)
{
int		i;
USERS		*newusers, *oldusers;

	if ((newusers = mysql_users_alloc()) == NULL)
		return -1;

	i = getUsers(service, newusers);

	if (i <= 0) {
		users_free(newusers);
		return i;
	}

	spinlock_acquire(&service->spin);
	oldusers = service->users;

	/* digest compare */
	if (memcmp(oldusers->cksum, newusers->cksum, SHA_DIGEST_LENGTH) == 0) {
		/* same data, nothing to do */
		LOGIF(LD, (skygw_log_write_flush(
			LOGFILE_DEBUG,
			"%lu [replace_mysql_users] users' tables not switched, checksum is the same",
			pthread_self())));
		/* free the new table */
		users_free(newusers);
		i = 0;
	} else {
		/* replace the service with effective new data */
		LOGIF(LD, (skygw_log_write_flush(
			LOGFILE_DEBUG,
			"%lu [replace_mysql_users] users' tables replaced, checksum differs",
			pthread_self())));
		service->users = newusers;
	}

	spinlock_release(&service->spin);

	if (i)
		users_free(oldusers);

	return i;
}


/**
 * Add a new MySQL user with host, password and netmask into the service users table
 *
 * The netmask values are:
 * 0 for any, 32 for single IPv4
 * 24 for a class C from a.b.c.%, 16 for a Class B from a.b.%.% and 8 for a Class A from a.%.%.%
 *
 * @param users         The users table
 * @param user          The user name
 * @param host          The host to add, with possible wildcards
 * @param passwd	The sha1(sha1(passoword)) to add
 * @return              1 on success, 0 on failure
 */

int add_mysql_users_with_host_ipv4(USERS *users, char *user, char *host, char *passwd, char *anydb, char *db) {
	struct sockaddr_in	serv_addr;
	MYSQL_USER_HOST		key;
	char ret_ip[INET_ADDRSTRLEN + 1]="";
	int found_range=0;
	int found_any=0;
	int ret = 0;

	fprintf(stderr, "Current %s@%s has anydb access %s OR specific db %s\n", user, host, anydb, db);

	/* prepare the user@host data struct */
	memset(&serv_addr, 0, sizeof(serv_addr));
	memset(&key, '\0', sizeof(key));

	/* set user */
	key.user = strdup(user);

	if(key.user == NULL) {
		return ret;
	}

	/* for anydb == Y key.resource is '\0' as set by memset */
	if (strcmp(anydb, "N") == 0) {
		if (db)
			key.resource = strdup(db);
		else
			key.resource = NULL;
	} else {
		key.resource = strdup("");
	}

	/* handle ANY, Class C,B,A */

	/* ANY */
	if (strcmp(host, "%") == 0) {
		strcpy(ret_ip, "0.0.0.0");
		found_any = 1;
	} else {
		char *tmp;
		strcpy(ret_ip, host);
		tmp = ret_ip+strlen(ret_ip)-1;

		/* start from Class C */
		while(*tmp) {
			if (*tmp == '%') {
				/* set only the last IPv4 byte to 1
				 * avoiding setipadress() failure
				 * for Class C address
				 */
				found_range++;
				if (found_range == 1)
					*tmp = '1';
				else
					*tmp = '0';
			}
			tmp--;
		}
	}

	/* fill IPv4 data struct */
	if (setipaddress(&serv_addr.sin_addr, ret_ip)) {

		/* copy IPv4 data into key.ipv4 */
		memcpy(&key.ipv4, &serv_addr, sizeof(serv_addr));

		if (found_range) {
			/* let's zero the last IP byte: a.b.c.0 we set above to 1*/
			key.ipv4.sin_addr.s_addr &= 0x00FFFFFF;
			key.netmask = 32 - (found_range * 8);
		} else {
			key.netmask = 32 - (found_any * 32);
		}
	
		/* add user@host as key and passwd as value in the MySQL users hash table */
		if (mysql_users_add(users, &key, passwd))
			ret = 1;
		if (ret == 1)
			fprintf(stderr, "Added user %s@%i with db [%s]\n", key.user, key.ipv4.sin_addr.s_addr, key.resource);
	}

	free(key.user);
	if (key.resource)
		free(key.resource);

	return ret;
}

/**
 * Load the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 *
 * @param service	The current service
 * @param users		The users table into which to load the users
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 */
static int
getDatabases(SERVICE *service)
{
	MYSQL			*con = NULL;
	MYSQL_ROW		row;
	MYSQL_RES		*result = NULL;
	int			num_fields = 0;
	char			*service_user = NULL;
	char			*service_passwd = NULL;
	char			*dpwd;
	int			total_users = 0;
	SERVER			*server;
	char			*users_query;
	unsigned char		hash[SHA_DIGEST_LENGTH]="";
	char			*users_data = NULL;
	int 			ndbs = 0;
	/* last byte is for Select_priv=Y|N */
	int			users_data_row_len = MYSQL_USER_MAXLEN + MYSQL_HOST_MAXLEN + MYSQL_PASSWORD_LEN + sizeof(char);
	int			i = 0;

	serviceGetUser(service, &service_user, &service_passwd);
	if (service_user == NULL || service_passwd == NULL)
		return -1;

	con = mysql_init(NULL);

 	if (con == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : mysql_init: %s",
                        mysql_error(con))));
		return -1;
	}

	if (mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL)) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : failed to set external connection. "
                        "It is needed for backend server connections. "
                        "Exiting.")));
		return -1;
	}
	/**
	 * Attempt to connect to one of the databases database or until we run 
         * out of databases
	 * to try
	 */
	server = service->databases;
	dpwd = decryptPassword(service_passwd);
	while (server != NULL && (mysql_real_connect(con,
                                                    server->name,
                                                    service_user,
                                                    dpwd,
                                                    NULL,
                                                    server->port,
                                                    NULL,
                                                    0) == NULL))
	{
                server = server->nextdb;
	}
	free(dpwd);

	if (server == NULL)
	{
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Unable to get user data from backend database "
                        "for service %s. Missing server information.",
                        service->name)));
		mysql_close(con);
		return -1;
	}

	if (mysql_query(con, "select count(1) from information_schema.SCHEMATA")) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading database names for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
		mysql_close(con);
		return -1;
	}
	result = mysql_store_result(con);

	if (result == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading database names for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
		mysql_close(con);
		return -1;
	}
	num_fields = mysql_num_fields(result);
	row = mysql_fetch_row(result);

	ndbs = atoi(row[0]);

	mysql_free_result(result);

	fprintf(stderr, "Found %i schemas\n", ndbs);

	if (!ndbs) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Counting users for service %s returned 0",
                        service->name)));
		mysql_close(con);
		return -1;
	}

	service->resources = (char **) calloc(ndbs+1, sizeof(char *));
	for (i = 0; i < ndbs; i++) {
		service->resources[i] = NULL;
	}

	 service->resources[i] = NULL;

	if (mysql_query(con, "SHOW DATABASES")) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading database names for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
		mysql_close(con);
		return -1;
	}

	result = mysql_store_result(con);
  
	if (result == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading database names for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
		mysql_close(con);
		return -1;
	}
	num_fields = mysql_num_fields(result);

	i = 0;	
	while ((row = mysql_fetch_row(result))) { 
		service->resources[i] = strndup(row[0], MYSQL_DATABASE_MAXLEN);
		fprintf(stderr, "Found a Database[%i]:[%s]\n", i, service->resources[i]);

		i++;
	}

	return ndbs;
}

/**
 * Load the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 *
 * @param service	The current service
 * @param users		The users table into which to load the users
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 */
static int
getUsers(SERVICE *service, USERS *users)
{
	MYSQL			*con = NULL;
	MYSQL_ROW		row;
	MYSQL_RES		*result = NULL;
	int			num_fields = 0;
	char			*service_user = NULL;
	char			*service_passwd = NULL;
	char			*dpwd;
	int			total_users = 0;
	SERVER			*server;
	char			*users_query;
	char			*dbs_query;
	unsigned char		hash[SHA_DIGEST_LENGTH]="";
	char			*users_data = NULL;
	int 			nusers = 0;
	/* last byte is for Select_priv=Y|N */
	int			users_data_row_len = MYSQL_USER_MAXLEN + MYSQL_HOST_MAXLEN + MYSQL_PASSWORD_LEN + sizeof(char);

	/* enable_root for MySQL protocol module means load the root user credentials from backend databases */
/*
	if(service->enable_root) {
		users_query = LOAD_MYSQL_USERS_QUERY " ORDER BY HOST DESC";
	} else {
		users_query = LOAD_MYSQL_USERS_QUERY USERS_QUERY_NO_ROOT " ORDER BY HOST DESC";
	}
*/
	if(service->enable_root) {
		users_query = LOAD_MYSQL_USERS_WITH_DB_QUERY;
	} else {
		users_query = LOAD_MYSQL_USERS_WITH_DB_QUERY_NO_ROOT;
	}

	serviceGetUser(service, &service_user, &service_passwd);
	if (service_user == NULL || service_passwd == NULL)
		return -1;

	con = mysql_init(NULL);

 	if (con == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : mysql_init: %s",
                        mysql_error(con))));
		return -1;
	}

	if (mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL)) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : failed to set external connection. "
                        "It is needed for backend server connections. "
                        "Exiting.")));
		return -1;
	}
	/**
	 * Attempt to connect to one of the databases database or until we run 
         * out of databases
	 * to try
	 */
	server = service->databases;
	dpwd = decryptPassword(service_passwd);
	while (server != NULL && (mysql_real_connect(con,
                                                    server->name,
                                                    service_user,
                                                    dpwd,
                                                    NULL,
                                                    server->port,
                                                    NULL,
                                                    0) == NULL))
	{
                server = server->nextdb;
	}
	free(dpwd);

	if (server == NULL)
	{
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Unable to get user data from backend database "
                        "for service %s. Missing server information.",
                        service->name)));
		mysql_close(con);
		return -1;
	}

	if (mysql_query(con, MYSQL_USERS_COUNT)) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading users for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
		mysql_close(con);
		return -1;
	}
	result = mysql_store_result(con);

	if (result == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading users for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
		mysql_close(con);
		return -1;
	}
	num_fields = mysql_num_fields(result);
	row = mysql_fetch_row(result);

	nusers = atoi(row[0]);

	mysql_free_result(result);

	if (!nusers) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Counting users for service %s returned 0",
                        service->name)));
		mysql_close(con);
		return -1;
	}

	if (mysql_query(con, users_query)) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading users for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
		mysql_close(con);
		return -1;
	}

	result = mysql_store_result(con);
  
	if (result == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading users for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
		mysql_close(con);
		return -1;
	}
	num_fields = mysql_num_fields(result);
	
	users_data = (char *)calloc(nusers, (users_data_row_len * sizeof(char)) + 1);

	if(users_data == NULL)
		return -1;

	while ((row = mysql_fetch_row(result))) { 
		/**
                 * Six fields should be returned.
		 * user,host,passwd,concat(),anydb,db
                 * passwd+1 (escaping the first byte that is '*')
                 */
		
		int rc = 0;

		rc = add_mysql_users_with_host_ipv4(users, row[0], row[1], strlen(row[2]) ? row[2]+1 : row[2], row[4], row[5]);

		if (rc == 1) {
			LOGIF(LD, (skygw_log_write_flush(
				LOGFILE_DEBUG,
				"%lu [mysql_users_add()] Added user %s@%s",
				pthread_self(),
				row[0],
				row[1])));

			/* Append data in the memory area for SHA1 digest */	
			strncat(users_data, row[3], users_data_row_len);

			total_users++;
		} else {
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"%lu [mysql_users_add()] Failed adding user %s@%s",
				pthread_self(),
				row[0],
				row[1])));
		}
	}

	/* compute SHA1 digest for users' data */
        SHA1((const unsigned char *) users_data, strlen(users_data), hash);

	memcpy(users->cksum, hash, SHA_DIGEST_LENGTH);

	free(users_data);
	mysql_free_result(result);
	mysql_close(con);

	return total_users;
}

/**
 * Allocate a new MySQL users table for mysql specific users@host as key
 *
 *  @return The users table
 */
USERS *
mysql_users_alloc()
{
USERS	*rval;

	if ((rval = calloc(1, sizeof(USERS))) == NULL)
		return NULL;

	if ((rval->data = hashtable_alloc(USERS_HASHTABLE_DEFAULT_SIZE, uh_hfun, uh_cmpfun)) == NULL) {
		free(rval);
		return NULL;
	}

	/* set the MySQL user@host print routine for the debug interface */
	rval->usersCustomUserFormat = mysql_format_user_entry;

	/* the key is handled by uh_keydup/uh_keyfree.
	* the value is a (char *): it's handled by strdup/free
	*/
	hashtable_memory_fns(rval->data, (HASHMEMORYFN)uh_keydup, (HASHMEMORYFN) strdup, (HASHMEMORYFN)uh_keyfree, (HASHMEMORYFN)free);

	return rval;
}

/**
 * Add a new MySQL user to the user table. The user name must be unique
 *
 * @param users		The users table
 * @param user		The user name
 * @param auth		The authentication data
 * @return		The number of users added to the table
 */
int
mysql_users_add(USERS *users, MYSQL_USER_HOST *key, char *auth)
{
int     add;

	if (key == NULL || key->user == NULL)
		return 0;

        atomic_add(&users->stats.n_adds, 1);
        add = hashtable_add(users->data, key, auth);
        atomic_add(&users->stats.n_entries, add);

        return add;
}

/**
 * Fetch the authentication data for a particular user from the users table
 *
 * @param users The MySQL users table
 * @param key	The key with user@host
 * @return	The authentication data or NULL on error
 */
void *mysql_users_fetch(USERS *users, MYSQL_USER_HOST *key) {
	MYSQL_USER_HOST *entry;
	if (key == NULL)
		return NULL;

        atomic_add(&users->stats.n_fetches, 1);

	return hashtable_fetch(users->data, key);
}

/**
 * The hash function we use for storing MySQL users as: users@hosts.
 * Currently only IPv4 addresses are supported
 *
 * @param key	The key value, i.e. username@host (IPv4)
 * @return	The hash key
 */

static int uh_hfun( void* key) {
        MYSQL_USER_HOST *hu = (MYSQL_USER_HOST *) key;

	if (key == NULL || hu == NULL || hu->user == NULL) {
		return 0;
	} else {
        	return (*hu->user + *(hu->user + 1) + (unsigned int) (hu->ipv4.sin_addr.s_addr & 0xFF000000 / (256 * 256 * 256)));
	}
}

/**
 * The compare function we use for compare MySQL users as: users@hosts.
 * Currently only IPv4 addresses are supported
 *
 * @param key1	The key value, i.e. username@host (IPv4)
 * @param key2	The key value, i.e. username@host (IPv4) 
 * @return	The compare value
 */

static int uh_cmpfun( void* v1, void* v2) {
	MYSQL_USER_HOST *hu1 = (MYSQL_USER_HOST *) v1;
	MYSQL_USER_HOST *hu2 = (MYSQL_USER_HOST *) v2;

	if (v1 == NULL || v2 == NULL || hu1 == NULL || hu2 == NULL || hu1->user == NULL || hu2->user == NULL)
		return 0;
	
	if (strcmp(hu1->user, hu2->user) == 0 && (hu1->ipv4.sin_addr.s_addr == hu2->ipv4.sin_addr.s_addr) && (hu1->netmask >= hu2->netmask)) {

		/* if no database name was passed, auth is ok */
		if (hu1->resource == NULL || (hu1->resource && !strlen(hu1->resource))) {
			return 0;
		} else {
			/* (1) check for no database grants at all and deny auth*/
			if (hu2->resource == NULL) {
				return 1;
			}
			/* (2) check for ANY database grant and allow auth*/
			if (!strlen(hu2->resource)) {
				return 0;
			}
			/* (3) check for database name specific grant and allow auth*/
			if (hu1->resource && hu2->resource && strcmp(hu1->resource,hu2->resource) == 0) {
				return 0;
			}

			/* no matches, deny auth */
			return 1;
		}
	} else {
		return 1;
	}
}

/**
 *The key dup function we use for duplicate the users@hosts.
 *
 * @param key	The key value, i.e. username@host ip4/ip6 data
 */

static void *uh_keydup(void* key) {
	MYSQL_USER_HOST *rval = (MYSQL_USER_HOST *) calloc(1, sizeof(MYSQL_USER_HOST));
	MYSQL_USER_HOST *current_key = (MYSQL_USER_HOST *)key;

	if (key == NULL || rval == NULL || current_key == NULL || current_key->user == NULL) {
		return NULL;
	}

	rval->user = strdup(current_key->user);

	if (rval->user == NULL)
		return NULL;

	memcpy(&rval->ipv4, &current_key->ipv4, sizeof(struct sockaddr_in));
	memcpy(&rval->netmask, &current_key->netmask, sizeof(int));

	if (current_key->resource)
		rval->resource = strdup(current_key->resource);

	return (void *) rval;
}

/**
 * The key free function we use for freeing the users@hosts data
 *
 * @param key	The key value, i.e. username@host ip4 data
 */
static void uh_keyfree( void* key) {
	MYSQL_USER_HOST *current_key = (MYSQL_USER_HOST *)key;

	if (key == NULL)
		return;

	if (current_key && current_key->user)
		free(current_key->user);

	if (current_key && current_key->resource)
		free(current_key->resource);

	free(key);
}

/**
 * Format the mysql user as user@host
 * The returned memory must be freed by the caller
 *
 *  @param data		Input data
 *  @return 		the MySQL user@host
 */
char *mysql_format_user_entry(void *data)
{
	MYSQL_USER_HOST *entry;
	char *mysql_user;
	/* the returned user string is "USER" + "@" + "HOST" + '\0' */
	int mysql_user_len = MYSQL_USER_MAXLEN + 1 + INET_ADDRSTRLEN + 10 + MYSQL_USER_MAXLEN + 1;

	if (data == NULL)
		return NULL;
	
        entry = (MYSQL_USER_HOST *) data;

	if (entry == NULL)
		return NULL;

	mysql_user = (char *) calloc(mysql_user_len, sizeof(char));

	if (mysql_user == NULL)
		return NULL;

	/* format user@host based on wildcards */	
	
	if (entry->ipv4.sin_addr.s_addr == INADDR_ANY && entry->netmask == 0) {
		snprintf(mysql_user, mysql_user_len-1, "%s@%%", entry->user);
	} else if ( (entry->ipv4.sin_addr.s_addr & 0xFF000000) == 0 && entry->netmask == 24) {
		snprintf(mysql_user, mysql_user_len-1, "%s@%i.%i.%i.%%", entry->user, entry->ipv4.sin_addr.s_addr & 0x000000FF, (entry->ipv4.sin_addr.s_addr & 0x0000FF00) / (256), (entry->ipv4.sin_addr.s_addr & 0x00FF0000) / (256 * 256));
	} else if ( (entry->ipv4.sin_addr.s_addr & 0xFFFF0000) == 0 && entry->netmask == 16) {
		snprintf(mysql_user, mysql_user_len-1, "%s@%i.%i.%%.%%", entry->user, entry->ipv4.sin_addr.s_addr & 0x000000FF, (entry->ipv4.sin_addr.s_addr & 0x0000FF00) / (256));
	} else if ( (entry->ipv4.sin_addr.s_addr & 0xFFFFFF00) == 0 && entry->netmask == 8) {
		snprintf(mysql_user, mysql_user_len-1, "%s@%i.%%.%%.%%", entry->user, entry->ipv4.sin_addr.s_addr & 0x000000FF);
	} else if (entry->netmask == 32) {
		strncpy(mysql_user, entry->user, MYSQL_USER_MAXLEN);
		strcat(mysql_user, "@");
		inet_ntop(AF_INET, &(entry->ipv4).sin_addr, mysql_user+strlen(mysql_user), INET_ADDRSTRLEN);
	} else {
		snprintf(mysql_user, MYSQL_USER_MAXLEN-5, "Err: %s", entry->user);
		strcat(mysql_user, "@");
		inet_ntop(AF_INET, &(entry->ipv4).sin_addr, mysql_user+strlen(mysql_user), INET_ADDRSTRLEN);
	}

	if (entry->resource) {
		if (strlen(entry->resource)) {
			strcat(mysql_user, " db: ");
			strcat(mysql_user, entry->resource);
		} else {
			strcat(mysql_user, " db: ANY");
		}
	} else {
		strcat(mysql_user, " no db");
	}

        return mysql_user;
}

