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
 * 04/12/14	Massimiliano Pinto	Added support for IPv$ wildcard hosts: a.%, a.%.% and a.b.%
 *
 * @endverbatim
 */

#include <stdio.h>
#include <ctype.h>
#include <mysql.h>

#include <dcb.h>
#include <service.h>
#include <users.h>
#include <dbusers.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <secrets.h>
#include <mysql_client_server_protocol.h>
#include <mysqld_error.h>


#define DEFAULT_CONNECT_TIMEOUT 3
#define DEFAULT_READ_TIMEOUT 1
#define DEFAULT_WRITE_TIMEOUT 2

#define USERS_QUERY_NO_ROOT " AND user NOT IN ('root')"

#if 0
#  define LOAD_MYSQL_USERS_QUERY 						\
	"SELECT  DISTINCT							\
	user.user AS user,							\
	user.host AS host,							\
	user.password AS password,						\
	concat(user.user,user.host,user.password, 				\
		IF((user.Select_priv+0)||find_in_set('Select',Coalesce(tp.Table_priv,0)),'Y','N') ,	\
		COALESCE( db.db,tp.db, '')) AS userdata,			\
	user.Select_priv AS anydb,						\
	COALESCE( db.db,tp.db, NULL)  AS db 					\
	FROM 									\
	mysql.user LEFT JOIN 							\
	mysql.db ON user.user=db.user AND user.host=db.host  LEFT JOIN 		\
	mysql.tables_priv tp ON user.user=tp.user AND user.host=tp.host 	\
	WHERE user.user IS NOT NULL AND user.user <> ''"

#else
# define LOAD_MYSQL_USERS_QUERY "SELECT user, host, password, concat(user,host,password,Select_priv) AS userdata, Select_priv AS anydb FROM mysql.user WHERE user IS NOT NULL AND user <> ''"
#endif
#define MYSQL_USERS_COUNT "SELECT COUNT(1) AS nusers FROM mysql.user"

#define MYSQL_USERS_WITH_DB_ORDER " ORDER BY host DESC"
#define LOAD_MYSQL_USERS_WITH_DB_QUERY "SELECT user.user AS user,user.host AS host,user.password AS password,concat(user.user,user.host,user.password,user.Select_priv,IFNULL(db,'')) AS userdata, user.Select_priv AS anydb,db.db AS db FROM mysql.user LEFT JOIN mysql.db ON user.user=db.user AND user.host=db.host WHERE user.user IS NOT NULL AND user.user <> ''" MYSQL_USERS_WITH_DB_ORDER

#define MYSQL_USERS_WITH_DB_COUNT "SELECT COUNT(1) AS nusers_db FROM (" LOAD_MYSQL_USERS_WITH_DB_QUERY ") AS tbl_count"

#define LOAD_MYSQL_USERS_WITH_DB_QUERY_NO_ROOT "SELECT * FROM (" LOAD_MYSQL_USERS_WITH_DB_QUERY ") AS t1 WHERE user NOT IN ('root')" MYSQL_USERS_WITH_DB_ORDER

#define LOAD_MYSQL_DATABASE_NAMES "SELECT * FROM ( (SELECT COUNT(1) AS ndbs FROM INFORMATION_SCHEMA.SCHEMATA) AS tbl1, (SELECT GRANTEE,PRIVILEGE_TYPE from INFORMATION_SCHEMA.USER_PRIVILEGES WHERE privilege_type='SHOW DATABASES' AND REPLACE(GRANTEE, \'\\'\',\'\')=CURRENT_USER()) AS tbl2)"

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static int getUsers(SERVICE *service, USERS *users);
static int uh_cmpfun( void* v1, void* v2);
static void *uh_keydup(void* key);
static void uh_keyfree( void* key);
static int uh_hfun( void* key);
char *mysql_users_fetch(USERS *users, MYSQL_USER_HOST *key);
char *mysql_format_user_entry(void *data);
int add_mysql_users_with_host_ipv4(USERS *users, char *user, char *host, char *passwd, char *anydb, char *db);
static int getDatabases(SERVICE *, MYSQL *);
HASHTABLE *resource_alloc();
void resource_free(HASHTABLE *resource);
void *resource_fetch(HASHTABLE *, char *);
int resource_add(HASHTABLE *, char *, char *);
int resource_hash(char *);
static int normalize_hostname(char *input_host, char *output_host);
static int gw_mysql_set_timeouts(
	MYSQL* handle,
	int    read_timeout,
	int    write_timeout,
	int    connect_timeout);

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
HASHTABLE	*oldresources;

	if ((newusers = mysql_users_alloc()) == NULL)
		return 0;

	oldresources = service->resources;

	i = getUsers(service, newusers);

	spinlock_acquire(&service->spin);
	oldusers = service->users;

	service->users = newusers;

	spinlock_release(&service->spin);

	/* free the old table */
	users_free(oldusers);
	/* free old resources */
	resource_free(oldresources);

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
HASHTABLE	*oldresources;

	if ((newusers = mysql_users_alloc()) == NULL)
		return -1;

	oldresources = service->resources;

	/* load db users ad db grants */
	i = getUsers(service, newusers);

	if (i <= 0) {
		users_free(newusers);
		/* restore resources */
		service->resources = oldresources;
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

	/* free old resources */
	resource_free(oldresources);

	spinlock_release(&service->spin);

	if (i) {
		/* free the old table */
		users_free(oldusers);
	}

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
	char ret_ip[400]="";
	int ret = 0;

	if (users == NULL || user == NULL || host == NULL) {
		return ret;
	}

	/* prepare the user@host data struct */
	memset(&serv_addr, 0, sizeof(serv_addr));
	memset(&key, '\0', sizeof(key));

	/* set user */
	key.user = strdup(user);

	if(key.user == NULL) {
		return ret;
	}

	/* for anydb == Y key.resource is '\0' as set by memset */
	if (anydb == NULL) {
		key.resource = NULL;
	} else {
		if (strcmp(anydb, "N") == 0) {
			if (db != NULL)
				key.resource = strdup(db);				
			else
				key.resource = NULL;
		} else {
			key.resource = strdup("");
		}
	}

	/* handle ANY, Class C,B,A */

	/* ANY */
	if (strcmp(host, "%") == 0) {
		strcpy(ret_ip, "0.0.0.0");
		key.netmask = 0;
	} else {
		/* hostname without % wildcards has netmask = 32 */
		key.netmask = normalize_hostname(host, ret_ip);

		if (key.netmask == -1) {
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Error : strdup() failed in normalize_hostname for %s@%s",
				user,
				host)));
		}
	}

	/* fill IPv4 data struct */
	if (setipaddress(&serv_addr.sin_addr, ret_ip) && strlen(ret_ip)) {

		/* copy IPv4 data into key.ipv4 */
		memcpy(&key.ipv4, &serv_addr, sizeof(serv_addr));

		/* if netmask < 32 there are % wildcards */
		if (key.netmask < 32) {
			/* let's zero the last IP byte: a.b.c.0 we may have set above to 1*/
			key.ipv4.sin_addr.s_addr &= 0x00FFFFFF;
		}
	
		/* add user@host as key and passwd as value in the MySQL users hash table */
		if (mysql_users_add(users, &key, passwd)) {
			ret = 1;
		}
	}

	free(key.user);
	if (key.resource)
		free(key.resource);

	return ret;
}

/**
 * Add the database specific grants from mysql.db table into the service resources hashtable
 * environment.
 *
 * @param service	The current service
 * @param users		The users table into which to load the users
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 */
static int
addDatabases(SERVICE *service, MYSQL *con)
{
	MYSQL_ROW		row;
	MYSQL_RES		*result = NULL;
	char			*service_user = NULL;
	char			*service_passwd = NULL;
	int 			ndbs = 0;

	char *get_showdbs_priv_query = LOAD_MYSQL_DATABASE_NAMES;

	serviceGetUser(service, &service_user, &service_passwd);

	if (service_user == NULL || service_passwd == NULL)
		return -1;

	if (mysql_query(con, get_showdbs_priv_query)) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading database names for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
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
		return -1;
	}

	/* Result has only one row */
	row = mysql_fetch_row(result);

	if (row) {
		ndbs = atoi(row[0]);
	} else {
		ndbs = 0;

		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
				"%s: Unable to load database grant information, MaxScale "
				"authentication will proceed without including database "
				"permissions. To correct this GRANT select permission "
				"on mysql.db to the user %s.",
					service->name, service_user)));
	}

	/* free resut set */
	mysql_free_result(result);

	if (!ndbs) {
		/* return if no db names are available */
		return 0;
	}

	if (mysql_query(con, "SHOW DATABASES")) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading database names for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));

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

		return -1;
	}

	/* insert key and value "" */
	while ((row = mysql_fetch_row(result))) {
	    skygw_log_write(LOGFILE_DEBUG,"%s: Adding database %s to the resouce hash.",service->name,row[0]);
	    resource_add(service->resources, row[0], "");
	}

	mysql_free_result(result);

	return ndbs;
}

/**
 * Load the database specific grants from mysql.db table into the service resources hashtable
 * environment.
 *
 * @param service	The current service
 * @param users		The users table into which to load the users
 * @return      -1 on any error or the number of users inserted (0 means no users at all)
 */
static int
getDatabases(SERVICE *service, MYSQL *con)
{
	MYSQL_ROW		row;
	MYSQL_RES		*result = NULL;
	char			*service_user = NULL;
	char			*service_passwd = NULL;
	int 			ndbs = 0;

	char *get_showdbs_priv_query = LOAD_MYSQL_DATABASE_NAMES;

	serviceGetUser(service, &service_user, &service_passwd);

	if (service_user == NULL || service_passwd == NULL)
		return -1;

	if (mysql_query(con, get_showdbs_priv_query)) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading database names for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));
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
		return -1;
	}

	/* Result has only one row */
	row = mysql_fetch_row(result);

	if (row) {
		ndbs = atoi(row[0]);
	} else {
		ndbs = 0;

		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
				"%s: Unable to load database grant information, MaxScale "
				"authentication will proceed without including database "
				"permissions. To correct this GRANT select permission "
				"on mysql.db to the user %s.",
					service->name, service_user)));
	}

	/* free resut set */
	mysql_free_result(result);

	if (!ndbs) {
		/* return if no db names are available */
		return 0;
	}

	if (mysql_query(con, "SHOW DATABASES")) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading database names for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));

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

		return -1;
	}

	/* Now populate service->resources hashatable with db names */
            service->resources = resource_alloc();

	/* insert key and value "" */
	while ((row = mysql_fetch_row(result))) { 
	    skygw_log_write(LOGFILE_DEBUG,"%s: Adding database %s to the resouce hash.",service->name,row[0]);
	    resource_add(service->resources, row[0], "");	    
	}

	mysql_free_result(result);

	return ndbs;
}

/**
 * Load the user/passwd from mysql.user table into the service users' hashtable
 * environment from all the backend servers.
 *
 * @param service	The current service
 * @param users		The users table into which to load the users
 * @return      	-1 on any error or the number of users inserted 
 * 			(0 means no users at all)
 */
static int
getAllUsers(SERVICE *service, USERS *users)
{
	MYSQL		*con = NULL;
	MYSQL_ROW	row;
	MYSQL_RES	*result = NULL;
	char		*service_user = NULL;
	char		*service_passwd = NULL;
	char		*dpwd = NULL;
	int		total_users = 0;
	SERVER_REF	*server;
	char		*users_query, *tmp;
	unsigned char	hash[SHA_DIGEST_LENGTH]="";
	char		*users_data = NULL;
        char            *final_data = NULL;
	char		dbnm[MYSQL_DATABASE_MAXLEN+1];
	int 		nusers = -1;
	int		users_data_row_len = MYSQL_USER_MAXLEN + 
						MYSQL_HOST_MAXLEN + 
						MYSQL_PASSWORD_LEN + 
						sizeof(char) + 
						MYSQL_DATABASE_MAXLEN;
	int		dbnames = 0;
	int		db_grants = 0;
	
	if (serviceGetUser(service, &service_user, &service_passwd) == 0)
	{
		ss_dassert(service_passwd == NULL || service_user == NULL);
		return -1; 
	}
        
        if (service->svc_do_shutdown)
	{
            return -1; 
        }
        
        dpwd = decryptPassword(service_passwd);
        final_data = (char*) malloc(sizeof(char));
        *final_data = '\0';
               
        /**
	 * Attempt to connect to one of the databases database or until we run 
         * out of databases
	 * to try
	 */
	server = service->dbref;
	
        if(server == NULL)
        {
            goto cleanup;
        }

	service->resources = resource_alloc();

        while(server != NULL)
        {
            
            con = mysql_init(NULL);
            
            if (con == NULL) 
            {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                                                 "Error : mysql_init: %s",
                                                 mysql_error(con))));
		goto cleanup;
            }
            
            /** Set read, write and connect timeout values */
            if (gw_mysql_set_timeouts(con, 
                                      DEFAULT_READ_TIMEOUT, 
                                      DEFAULT_WRITE_TIMEOUT,
                                      DEFAULT_CONNECT_TIMEOUT))
            {
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
                                                 "Error : failed to set timeout values for backend "
			"connection.")));
		mysql_close(con);
		goto cleanup;
            }
            
            if (mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL)) 
            {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                                                 "Error : failed to set external connection. "
                        "It is needed for backend server connections.")));
		mysql_close(con);
		goto cleanup;
            }
            
            
            while(!service->svc_do_shutdown &&
                  server != NULL &&
                  (mysql_real_connect(con,
                                      server->server->name,
                                      service_user,
                                      dpwd,
                                      NULL,
                                      server->server->port,
                                      NULL,
                                      0) == NULL))
            {
                server = server->next;
            }
            
            
            if (server == NULL)
            {
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
                                                 "Error : Unable to get user data from backend database "
			"for service [%s]. Missing server information.",
                                                 service->name)));
		mysql_close(con);
		goto cleanup;
            }
            
            /** Count users. Start with users and db grants for users */
            if (mysql_query(con, MYSQL_USERS_WITH_DB_COUNT)) {
		if (mysql_errno(con) != ER_TABLEACCESS_DENIED_ERROR) {
                    /* This is an error we cannot handle, return */
                    LOGIF(LE, (skygw_log_write_flush(
                            LOGFILE_ERROR,
                                                     "Error : Loading users for service [%s] encountered "
                            "error: [%s].",
                                                     service->name,
                                                     mysql_error(con))));
                    mysql_close(con);
                    goto cleanup;
		} else {
                    /*
                     * We have got ER_TABLEACCESS_DENIED_ERROR
                     * try counting users from mysql.user without DB names.
                     */
                    if (mysql_query(con, MYSQL_USERS_COUNT)) {
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                                         "Error : Loading users for service [%s] encountered "
                                "error: [%s].",
                                                         service->name,
                                                         mysql_error(con))));
                        mysql_close(con);
                        goto cleanup;
                    }
		}
            }
            
            result = mysql_store_result(con);
            
            if (result == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                                                 "Error : Loading users for service [%s] encountered "
                        "error: [%s].",
                                                 service->name,
                                                 mysql_error(con))));
		mysql_close(con);
		goto cleanup;
            }
            
            row = mysql_fetch_row(result);
            
            nusers = atoi(row[0]);
            
            mysql_free_result(result);
            
            if (!nusers) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                                                 "Error : Counting users for service %s returned 0",
                                                 service->name)));
		mysql_close(con);
		goto cleanup;
            }
            
            if(service->enable_root) {
		/* enable_root for MySQL protocol module means load the root user credentials from backend databases */
		users_query = LOAD_MYSQL_USERS_WITH_DB_QUERY;
            } else {
		users_query = LOAD_MYSQL_USERS_WITH_DB_QUERY_NO_ROOT;
            }
            
            /* send first the query that fetches users and db grants */
            if (mysql_query(con, users_query)) {
		/*
                 * An error occurred executing the query
                 *
                 * Check mysql_errno() against ER_TABLEACCESS_DENIED_ERROR)
                 */
                
		if (1142 != mysql_errno(con)) {
                    /* This is an error we cannot handle, return */
                    
                    LOGIF(LE, (skygw_log_write_flush(
                            LOGFILE_ERROR,
                                                     "Error : Loading users with dbnames for service [%s] encountered "
                            "error: [%s], MySQL errno %i",
                                                     service->name,
                                                     mysql_error(con),
                                                     mysql_errno(con))));
                    
                    mysql_close(con);
                    
                    goto cleanup;
		}  else {
                    /*
                     * We have got ER_TABLEACCESS_DENIED_ERROR
                     * try loading users from mysql.user without DB names.
                     */
                    
                    LOGIF(LE, (skygw_log_write_flush(
                            LOGFILE_ERROR,
                                                     "%s: Unable to load database grant information, MaxScale "
                            "authentication will proceed without including database "
                            "permissions. To correct this GRANT select permission "
                            "on msql.db to the user %s.",
                                                     service->name, service_user)));
                    
                    /* check for root user select */
                    if(service->enable_root) {
                        users_query = LOAD_MYSQL_USERS_QUERY " ORDER BY HOST DESC";
                    } else {
                        users_query = LOAD_MYSQL_USERS_QUERY USERS_QUERY_NO_ROOT " ORDER BY HOST DESC";
                    }
                    
                    if (mysql_query(con, users_query)) {
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                                         "Error : Loading users for service [%s] encountered "
                                "error: [%s], code %i",
                                                         service->name,
                                                         mysql_error(con),
                                                         mysql_errno(con))));
                        
                        mysql_close(con);
                        
                        goto cleanup;
                    }
                    
                    /* users successfully loaded but without db grants */
                    
                    LOGIF(LM, (skygw_log_write_flush(
                            LOGFILE_MESSAGE,
                                                     "Loading users from [mysql.user] without access to [mysql.db] for "
                            "service [%s]. MaxScale Authentication with DBname on connect "
                            "will not consider database grants.",
                                                     service->name)));
		}
            } else {
		/*
                 * users successfully loaded with db grants.
                 */
                skygw_log_write(LOGFILE_DEBUG,"[%s] Loading users with db grants.",service->name);
		db_grants = 1;
            }
            
            result = mysql_store_result(con);
            
            if (result == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                                                 "Error : Loading users for service %s encountered "
                        "error: %s.",
                                                 service->name,
                                                 mysql_error(con))));
                
		mysql_free_result(result);
		mysql_close(con);
                
		goto cleanup;
            }
            
            users_data = (char *)calloc(nusers, (users_data_row_len * sizeof(char)) + 1);
            
            if (users_data == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
                                                 "Error : Memory allocation for user data failed due to "
			"%d, %s.",
                                                 errno,
                                                 strerror(errno))));
		mysql_free_result(result);
		mysql_close(con);
                
		goto cleanup;
            }
            
            if (db_grants) {
		/* load all mysql database names */
		dbnames = addDatabases(service, con);
                
		LOGIF(LD, (skygw_log_write(
			LOGFILE_DEBUG,
                                         "Loaded %d MySQL Database Names for service [%s]",
                                         dbnames,
                                         service->name)));
            } else {
		service->resources = NULL;
            }
            
            while ((row = mysql_fetch_row(result))) {
                
		/**
                 * Up to six fields could be returned.
                 * user,host,passwd,concat(),anydb,db
                 * passwd+1 (escaping the first byte that is '*')
                 */
		
		int rc = 0;
		char *password = NULL;
                
		if (row[2] != NULL) {
                    /* detect mysql_old_password (pre 4.1 protocol) */
                    if (strlen(row[2]) == 16) {
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                                         "%s: The user %s@%s has on old password in the "
                                "backend database. MaxScale does not support these "
                                "old passwords. This user will not be able to connect "
                                "via MaxScale. Update the users password to correct "
                                "this.",
                                                         service->name,
                                                         row[0],
                                                         row[1])));
                        continue;
                    }
                    
                    if (strlen(row[2]) > 1)
                        password = row[2] +1;
                    else
                        password = row[2];
		}
                
		/* 
                 * add user@host and DB global priv and specificsa grant (if possible)
                 */
                
		if (db_grants) {
		    bool havedb = false;
                    /* we have dbgrants, store them */
		    if(row[5]){
			unsigned long *rowlen = mysql_fetch_lengths(result);
			memcpy(dbnm,row[5],rowlen[5]);
			memset(dbnm + rowlen[5],0,1);
			havedb = true;
			if(service->strip_db_esc) {
			    strip_escape_chars(dbnm);
			    LOGIF(LD, (skygw_log_write(
				    LOGFILE_DEBUG,
						     "[%s]: %s -> %s",
						     service->name,
						     row[5],
						     dbnm)));
			}
		    }
                    rc = add_mysql_users_with_host_ipv4(users, row[0], row[1], password, row[4],havedb ? dbnm : NULL);
		    skygw_log_write(LOGFILE_DEBUG,"%s: Adding user:%s host:%s anydb:%s db:%s.",
			     service->name,row[0],row[1],row[4],
			     havedb ? dbnm : NULL);
		} else {
                    /* we don't have dbgrants, simply set ANY DB for the user */	
                    rc = add_mysql_users_with_host_ipv4(users, row[0], row[1], password, "Y", NULL);
		}
                
		if (rc == 1) {
                    if (db_grants) {
                        char dbgrant[MYSQL_DATABASE_MAXLEN + 1]="";
                        if (row[4] != NULL) {
                            if (strcmp(row[4], "Y"))
                                strcpy(dbgrant, "ANY");
                            else {
                                if (row[5])
                                    strncpy(dbgrant, row[5], MYSQL_DATABASE_MAXLEN);
                            }
                        }
                        
                        if (!strlen(dbgrant))
                            strcpy(dbgrant, "no db");
                        
                        /* Log the user being added with its db grants */
                        LOGIF(LD, (skygw_log_write_flush(
                                LOGFILE_DEBUG,
                                                         "%s: User %s@%s for database %s added to "
                                "service user table.",
                                                         service->name,
                                                         row[0],
                                                         row[1],
                                                         dbgrant)));
                    } else {
                        /* Log the user being added (without db grants) */
                        LOGIF(LD, (skygw_log_write_flush(
                                LOGFILE_DEBUG,
                                                         "%s: User %s@%s added to service user table.",
                                                         service->name,
                                                         row[0],
                                                         row[1])));
                    }
                    
                    /* Append data in the memory area for SHA1 digest */	
                    strncat(users_data, row[3], users_data_row_len);
                    
                    total_users++;
		} else {
                    LOGIF(LE, (skygw_log_write_flush(
                            LOGFILE_ERROR,
                                                     "Warning: Failed to add user %s@%s for service [%s]. "
                            "This user will be unavailable via MaxScale.",
                                                     row[0],
                                                     row[1],
                                                     service->name)));
		}
            }
            
            mysql_free_result(result);
            mysql_close(con);
            
            if((tmp = realloc(final_data, (strlen(final_data) + strlen(users_data) + 1) * sizeof(char))) == NULL)
            {
                free(users_data);
                goto cleanup;
            }
            
            final_data = tmp;
            
            strcat(final_data,users_data);
            free(users_data);
            
            if(service->users_from_all)
            {
                server = server->next;
            }
            else
            {
                server = NULL;
            }
        }        
        
	/* compute SHA1 digest for users' data */
        SHA1((const unsigned char *) final_data, strlen(final_data), hash);

	memcpy(users->cksum, hash, SHA_DIGEST_LENGTH);
        
        cleanup:
        
        free(dpwd);
        free(final_data);
        
	return total_users;
}


/**
 * Load the user/passwd form mysql.user table into the service users' hashtable
 * environment.
 *
 * @param service	The current service
 * @param users		The users table into which to load the users
 * @return      	-1 on any error or the number of users inserted 
 * 			(0 means no users at all)
 */
static int
getUsers(SERVICE *service, USERS *users)
{
	MYSQL		*con = NULL;
	MYSQL_ROW	row;
	MYSQL_RES	*result = NULL;
	char		*service_user = NULL;
	char		*service_passwd = NULL;
	char		*dpwd;
	int		total_users = 0;
	SERVER_REF	*server;
	char		*users_query;
	unsigned char	hash[SHA_DIGEST_LENGTH]="";
	char		*users_data = NULL;
	int 		nusers = 0;
	int		users_data_row_len = MYSQL_USER_MAXLEN + 
						MYSQL_HOST_MAXLEN + 
						MYSQL_PASSWORD_LEN + 
						sizeof(char) + 
						MYSQL_DATABASE_MAXLEN;
	int		dbnames = 0;
	int		db_grants = 0;
	
	if (serviceGetUser(service, &service_user, &service_passwd) == 0)
	{
		ss_dassert(service_passwd == NULL || service_user == NULL);
		return -1; 
	}

	if(service->users_from_all)
	  {
	    return getAllUsers(service,users);
	  }

	con = mysql_init(NULL);

 	if (con == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : mysql_init: %s",
                        mysql_error(con))));
		return -1;
	}
	/** Set read, write and connect timeout values */
	if (gw_mysql_set_timeouts(con, 
		DEFAULT_READ_TIMEOUT, 
		DEFAULT_WRITE_TIMEOUT,
		DEFAULT_CONNECT_TIMEOUT))
	{
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error : failed to set timeout values for backend "
			"connection.")));
		mysql_close(con);
		return -1;
	}

	if (mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL)) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : failed to set external connection. "
                        "It is needed for backend server connections.")));
		mysql_close(con);
		return -1;
	}
	/**
	 * Attempt to connect to one of the databases database or until we run 
         * out of databases
	 * to try
	 */
	server = service->dbref;
	dpwd = decryptPassword(service_passwd);

	/* Select a server with Master bit, if available */
	while (server != NULL && !(server->server->status & SERVER_MASTER)) {
                	server = server->next;
	}

	if (service->svc_do_shutdown)
	{
		free(dpwd);
		mysql_close(con);
		return -1;
	}
	
	/* Try loading data from master server */
	if (server != NULL && 
		(mysql_real_connect(con,
			server->server->name, service_user, 
			dpwd, 
			NULL, 
			server->server->port, 
			NULL, 0) != NULL)) 
	{
		LOGIF(LD, (skygw_log_write_flush(
			LOGFILE_DEBUG,
			"Dbusers : Loading data from backend database with "
			"Master role [%s:%i] for service [%s]",
			server->server->name,
			server->server->port,
			service->name)));
	} else {
		/* load data from other servers via loop */
		server = service->dbref;

		while (!service->svc_do_shutdown &&
			server != NULL && 
			(mysql_real_connect(con,
					server->server->name,
					service_user,
					dpwd,
					NULL,
					server->server->port,
					NULL,
					0) == NULL))
		{
			server = server->next;
		}
		
		if (service->svc_do_shutdown)
		{
			free(dpwd);
			mysql_close(con);
			return -1;
		}
		
		if (server != NULL) {
			LOGIF(LD, (skygw_log_write_flush(
				LOGFILE_DEBUG,
				"Dbusers : Loading data from backend database "
				"[%s:%i] for service [%s]",
				server->server->name,
				server->server->port,
				service->name)));
		}
	}

	free(dpwd);

	if (server == NULL)
	{
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error : Unable to get user data from backend database "
			"for service [%s]. Missing server information.",
			service->name)));
		mysql_close(con);
		return -1;
	}

	/** Count users. Start with users and db grants for users */
	if (mysql_query(con, MYSQL_USERS_WITH_DB_COUNT)) {
		if (mysql_errno(con) != ER_TABLEACCESS_DENIED_ERROR) {
                        /* This is an error we cannot handle, return */
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Error : Loading users for service [%s] encountered "
				"error: [%s].",
				service->name,
				mysql_error(con))));
			mysql_close(con);
			return -1;
		} else {
			/*
			 * We have got ER_TABLEACCESS_DENIED_ERROR
			 * try counting users from mysql.user without DB names.
			 */
			if (mysql_query(con, MYSQL_USERS_COUNT)) {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error : Loading users for service [%s] encountered "
					"error: [%s].",
					service->name,
					mysql_error(con))));
				mysql_close(con);
				return -1;
			}
		}
	}

	result = mysql_store_result(con);

	if (result == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading users for service [%s] encountered "
                        "error: [%s].",
                        service->name,
                        mysql_error(con))));
		mysql_close(con);
		return -1;
	}

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

	if(service->enable_root) {
		/* enable_root for MySQL protocol module means load the root user credentials from backend databases */
		users_query = LOAD_MYSQL_USERS_WITH_DB_QUERY;
	} else {
		users_query = LOAD_MYSQL_USERS_WITH_DB_QUERY_NO_ROOT;
	}

	/* send first the query that fetches users and db grants */
	if (mysql_query(con, users_query)) {
		/*
		 * An error occurred executing the query
		 *
		 * Check mysql_errno() against ER_TABLEACCESS_DENIED_ERROR)
		 */

		if (1142 != mysql_errno(con)) {
			/* This is an error we cannot handle, return */

			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Error : Loading users with dbnames for service [%s] encountered "
				"error: [%s], MySQL errno %i",
				service->name,
				mysql_error(con),
				mysql_errno(con))));

			mysql_close(con);

			return -1;
		}  else {
			/*
			 * We have got ER_TABLEACCESS_DENIED_ERROR
			 * try loading users from mysql.user without DB names.
			 */

			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"%s: Unable to load database grant information, MaxScale "
				"authentication will proceed without including database "
				"permissions. To correct this GRANT select permission "
				"on msql.db to the user %s.",
					service->name, service_user)));
			
			/* check for root user select */
			if(service->enable_root) {
				users_query = LOAD_MYSQL_USERS_QUERY " ORDER BY HOST DESC";
			} else {
				users_query = LOAD_MYSQL_USERS_QUERY USERS_QUERY_NO_ROOT " ORDER BY HOST DESC";
			}

			if (mysql_query(con, users_query)) {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error : Loading users for service [%s] encountered "
					"error: [%s], code %i",
					service->name,
					mysql_error(con),
					mysql_errno(con))));

				mysql_close(con);

				return -1;
			}

			/* users successfully loaded but without db grants */

			LOGIF(LM, (skygw_log_write_flush(
				LOGFILE_MESSAGE,
				"Loading users from [mysql.user] without access to [mysql.db] for "
				"service [%s]. MaxScale Authentication with DBname on connect "
				"will not consider database grants.",
				 service->name)));
		}
	} else {
		/*
		 * users successfully loaded with db grants.
		 */

		db_grants = 1;
	}

	result = mysql_store_result(con);
  
	if (result == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Loading users for service %s encountered "
                        "error: %s.",
                        service->name,
                        mysql_error(con))));

		mysql_free_result(result);
		mysql_close(con);

		return -1;
	}

	users_data = (char *)calloc(nusers, (users_data_row_len * sizeof(char)) + 1);

	if (users_data == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error : Memory allocation for user data failed due to "
			"%d, %s.",
			errno,
			strerror(errno))));
		mysql_free_result(result);
		mysql_close(con);

		return -1;
	}

	if (db_grants) {
		/* load all mysql database names */
		dbnames = getDatabases(service, con);

		LOGIF(LD, (skygw_log_write(
			LOGFILE_DEBUG,
			"Loaded %d MySQL Database Names for service [%s]",
			dbnames,
			service->name)));
	} else {
		service->resources = NULL;
	}

	while ((row = mysql_fetch_row(result))) {

		/**
                 * Up to six fields could be returned.
		 * user,host,passwd,concat(),anydb,db
                 * passwd+1 (escaping the first byte that is '*')
                 */
		
		int rc = 0;
		char *password = NULL;

		if (row[2] != NULL) {
			/* detect mysql_old_password (pre 4.1 protocol) */
			if (strlen(row[2]) == 16) {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"%s: The user %s@%s has on old password in the "
					"backend database. MaxScale does not support these "
					"old passwords. This user will not be able to connect "
					"via MaxScale. Update the users password to correct "
					"this.",
					service->name,
					row[0],
					row[1])));
				continue;
			}

			if (strlen(row[2]) > 1)
				password = row[2] +1;
			else
				password = row[2];
		}

		/* 
		 * add user@host and DB global priv and specificsa grant (if possible)
		 */

		if (db_grants) {
			/* we have dbgrants, store them */
			rc = add_mysql_users_with_host_ipv4(users, row[0], row[1], password, row[4], row[5]);
		} else {
			/* we don't have dbgrants, simply set ANY DB for the user */	
			rc = add_mysql_users_with_host_ipv4(users, row[0], row[1], password, "Y", NULL);
		}

		if (rc == 1) {
			if (db_grants) {
				char dbgrant[MYSQL_DATABASE_MAXLEN + 1]="";
				if (row[4] != NULL) {
					if (strcmp(row[4], "Y"))
						strcpy(dbgrant, "ANY");
					else {
						if (row[5])
							strncpy(dbgrant, row[5], MYSQL_DATABASE_MAXLEN);
					}
				}

				if (!strlen(dbgrant))
					strcpy(dbgrant, "no db");

				/* Log the user being added with its db grants */
				LOGIF(LD, (skygw_log_write_flush(
						LOGFILE_DEBUG,
						"%s: User %s@%s for database %s added to "
						"service user table.",
						service->name,
						row[0],
						row[1],
						dbgrant)));
			} else {
				/* Log the user being added (without db grants) */
				LOGIF(LD, (skygw_log_write_flush(
					LOGFILE_DEBUG,
						"%s: User %s@%s added to service user table.",
						service->name,
						row[0],
						row[1])));
			}

			/* Append data in the memory area for SHA1 digest */	
			strncat(users_data, row[3], users_data_row_len);

			total_users++;
		} else {
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Warning: Failed to add user %s@%s for service [%s]. "
				"This user will be unavailable via MaxScale.",
				row[0],
				row[1],
				service->name)));
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
char *mysql_users_fetch(USERS *users, MYSQL_USER_HOST *key) {
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

	if (v1 == NULL || v2 == NULL)
		return 0;
	
	if (hu1->user == NULL || hu2->user == NULL)
		return 0;

	if (strcmp(hu1->user, hu2->user) == 0 && (hu1->ipv4.sin_addr.s_addr == hu2->ipv4.sin_addr.s_addr) && (hu1->netmask >= hu2->netmask)) {

		/* if no database name was passed, auth is ok */
		if (hu1->resource == NULL || (hu1->resource && !strlen(hu1->resource))) {
			return 0;
		} else {
			/* (1) check for no database grants at all and deny auth */
			if (hu2->resource == NULL) {
				return 1;
			}
			/* (2) check for ANY database grant and allow auth */
			if (!strlen(hu2->resource)) {
				return 0;
			}
			/* (3) check for database name specific grant and allow auth */
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
		if (rval) {
			free(rval);
		}

		return NULL;
	}

	rval->user = strdup(current_key->user);

	if (rval->user == NULL) {
		free(rval);
		return NULL;
	}

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

        return mysql_user;
}

/*
 * The hash function we use for storing MySQL database names.
 *
 * @param key	The key value
 * @return	The hash key
 */
int
resource_hash(char *key)
{
        return (*key + *(key + 1));
}

/**
 * Remove the resources table
 *
 * @param resources	The resources table to remove
 */
void
resource_free(HASHTABLE *resources)
{
	if (resources) {
        	hashtable_free(resources);
	}
}

/**
 * Allocate a MySQL database names table
 *
 * @return	The database names table
 */
HASHTABLE *
resource_alloc()
{
HASHTABLE       *resources;

        if ((resources = hashtable_alloc(10, resource_hash, strcmp)) == NULL)
        {
                return NULL;
        }

        hashtable_memory_fns(resources, (HASHMEMORYFN)strdup, (HASHMEMORYFN)strdup, (HASHMEMORYFN)free, (HASHMEMORYFN)free);

        return resources;
}

/**
 * Add a new MySQL database name to the resources table. The resource name must be unique
 *
 * @param resources	The resources table
 * @param key		The resource name
 * @param value		The value for resource (not used)
 * @return		The number of resources dded to the table
 */
int
resource_add(HASHTABLE *resources, char *key, char *value)
{
        return hashtable_add(resources, key, value);
}

/**
 * Fetch a particular database name from the resources table
 *
 * @param resources	The MySQL database names table
 * @param key		The database name to fetch
 * @return		The database esists or NULL if not found
 */
void *
resource_fetch(HASHTABLE *resources, char *key)
{
        return hashtable_fetch(resources, key);
}

/**
 * Normalize hostname with % wildcards to a valid IP string.
 *
 * Valid input values:
 * a.b.c.d, a.b.c.%, a.b.%.%, a.%.%.%
 * Short formats a.% and a.%.% are both converted to a.%.%.%
 * Short format a.b.% is converted to a.b.%.%
 *
 * Last host byte is set to 1, avoiding setipadress() failure
 *
 * @param input_host	The hostname with possible % wildcards
 * @param output_host	The normalized hostname (buffer must be preallocated)
 * @return		The calculated netmask or -1 on failure
 */
static int normalize_hostname(char *input_host, char *output_host)
{
int	netmask, bytes, bits = 0, found_wildcard = 0;
char	*p, *lasts, *tmp;
int	useorig = 0;

	output_host[0] = 0;
	bytes = 0;

	tmp = strdup(input_host);

	if (tmp == NULL) {
		return -1;
	}

	p = strtok_r(tmp, ".", &lasts);
	while (p != NULL)
	{

		if (strcmp(p, "%"))
		{
			if (! isdigit(*p))
				useorig = 1;

			strcat(output_host, p);
			bits += 8;
		}
		else if (bytes == 3)
		{
			found_wildcard = 1;
			strcat(output_host, "1");
		}
		else
		{
			found_wildcard = 1;
			strcat(output_host, "0");
		}
		bytes++;
		p = strtok_r(NULL, ".", &lasts);
		if (p)
			strcat(output_host, ".");
	}
	if (found_wildcard)
	{
		netmask = bits;
		while (bytes++ < 4)
		{
			if (bytes == 4)
			{
				strcat(output_host, ".1");
			}
			else
			{
				strcat(output_host, ".0");
			}
		}
	}
	else
		netmask = 32;

	if (useorig == 1)
	{
		netmask = 32;
		strcpy(output_host, input_host);
	}

	free(tmp);

	return netmask;
}

/**
 * Set read, write and connect timeout values for MySQL database connection.
 * 
 * @param handle		MySQL handle
 * @param read_timeout		Read timeout value in seconds
 * @param write_timeout		Write timeout value in seconds
 * @param connect_timeout	Connect timeout value in seconds
 * 
 * @return 0 if succeed, 1 if failed
 */
static int gw_mysql_set_timeouts(
	MYSQL* handle,
	int    read_timeout,
	int    write_timeout,
	int    connect_timeout)
{	
	int rc;
	
	if ((rc = mysql_options(handle, 
		MYSQL_OPT_READ_TIMEOUT, 
		(void *)&read_timeout)))
	{
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error : failed to set read timeout for backend "
			"connection.")));
		goto retblock;
	}
	
	if ((rc = mysql_options(handle, 
			MYSQL_OPT_CONNECT_TIMEOUT, 
			(void *)&connect_timeout)))
	{
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error : failed to set connect timeout for backend "
			"connection.")));
		goto retblock;
	}
	
	if ((rc = mysql_options(handle, 
			MYSQL_OPT_WRITE_TIMEOUT, 
			(void *)&write_timeout)))
	{
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error : failed to set write timeout for backend "
			"connection.")));
		goto retblock;
	}
	
	retblock:
	return rc;
}

/*
 * Serialise a key for the dbusers hashtable to a file
 *
 * @param fd	File descriptor to write to
 * @param key	The key to write
 * @return 	0 on error, 1 if the key was written
 */
static int
dbusers_keywrite(int fd, void *key)
{
MYSQL_USER_HOST		*dbkey = (MYSQL_USER_HOST *)key;
int			tmp;

	tmp = strlen(dbkey->user);
	if (write(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
		return 0;
	if (write(fd, dbkey->user, tmp) != tmp)
		return 0;
	if (write(fd, &dbkey->ipv4, sizeof(dbkey->ipv4)) != sizeof(dbkey->ipv4))
		return 0;
	if (write(fd, &dbkey->netmask, sizeof(dbkey->netmask)) != sizeof(dbkey->netmask))
		return 0;
	if (dbkey->resource)
	{
		tmp = strlen(dbkey->resource);
		if (write(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
			return 0;
		if (write(fd, dbkey->resource, tmp) != tmp)
			return 0;
	}
	else		// NULL is valid, so represent with a length of -1
	{
		tmp = -1;
		if (write(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
			return 0;
	}
	return 1;
}

/**
 * Serialise a value for the dbusers hashtable to a file
 *
 * @param fd	File descriptor to write to
 * @param value	The value to write
 * @return 	0 on error, 1 if the value was written
 */
static int
dbusers_valuewrite(int fd, void *value)
{
int			tmp;

	tmp = strlen(value);
	if (write(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
		return 0;
	if (write(fd, value, tmp) != tmp)
		return 0;
	return 1;
}

/**
 * Unserialise a key for the dbusers hashtable from a file
 *
 * @param fd	File descriptor to read from
 * @return 	Pointer to the new key or NULL on error
 */
static void *
dbusers_keyread(int fd)
{
MYSQL_USER_HOST		*dbkey;
int			tmp;

	if ((dbkey = (MYSQL_USER_HOST *)malloc(sizeof(MYSQL_USER_HOST))) == NULL)
		return NULL;
	if (read(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
	{
		free(dbkey);
		return NULL;
	}
	if ((dbkey->user = (char *)malloc(tmp + 1)) == NULL)
	{
		free(dbkey);
		return NULL;
	}
	if (read(fd, dbkey->user, tmp) != tmp)
	{
		free(dbkey->user);
		free(dbkey);
		return NULL;
	}
	dbkey->user[tmp] = 0;	// NULL Terminate
	if (read(fd, &dbkey->ipv4, sizeof(dbkey->ipv4)) != sizeof(dbkey->ipv4))
	{
		free(dbkey->user);
		free(dbkey);
		return NULL;
	}
	if (read(fd, &dbkey->netmask, sizeof(dbkey->netmask)) != sizeof(dbkey->netmask))
	{
		free(dbkey->user);
		free(dbkey);
		return NULL;
	}
	if (read(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
	{
		free(dbkey->user);
		free(dbkey);
		return NULL;
	}
	if (tmp != -1)
	{
		if ((dbkey->resource = (char *)malloc(tmp + 1)) == NULL)
		{
			free(dbkey->user);
			free(dbkey);
			return NULL;
		}
		if (read(fd, dbkey->resource, tmp) != tmp)
		{
			free(dbkey->resource);
			free(dbkey->user);
			free(dbkey);
			return NULL;
		}
	}
	else		// NULL is valid, so represent with a length of -1
	{
		dbkey->resource = NULL;
	}
	return (void *)dbkey;
}

/**
 * Unserialise a value for the dbusers hashtable from a file
 *
 * @param fd	File descriptor to read from
 * @return 	Return the new value data or NULL on error
 */
static void *
dbusers_valueread(int fd)
{
char			*value;
int			tmp;

	if (read(fd, &tmp, sizeof(tmp)) != sizeof(tmp))
		return NULL;
	if ((value = (char *)malloc(tmp + 1)) == NULL)
		return NULL;
	if (read(fd, value, tmp) != tmp)
	{
		free(value);
		return NULL;
	}
	value[tmp] = 0;
	return (void *)value;
}

/**
 * Save the dbusers data to a hashtable file
 *
 * @param users		The hashtable that stores the user data
 * @param filename	The filename to save the data in
 * @return		The number of entries saved
 */
int
dbusers_save(USERS *users, char *filename)
{
	return hashtable_save(users->data, filename, dbusers_keywrite, dbusers_valuewrite);
}

/**
 * Load the dbusers data from a saved hashtable file
 *
 * @param users		The hashtable that stores the user data
 * @param filename	The filename to laod the data from
 * @return		The number of entries loaded
 */
int
dbusers_load(USERS *users, char *filename)
{
	return hashtable_load(users->data, filename, dbusers_keyread, dbusers_valueread);
}
