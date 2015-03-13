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
 * @file mysql_mon.c - A MySQL Multi Muster cluster monitor
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 08/09/14	Massimiliano Pinto	Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <monitor.h>
#include <mysqlmon.h>
#include <thread.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <secrets.h>
#include <dcb.h>
#include <modinfo.h>
#include <maxconfig.h>
/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static	void	monitorMain(void *);

static char *version_str = "V1.0.1";

MODULE_INFO	info = {
	MODULE_API_MONITOR,
	MODULE_BETA_RELEASE,
	MONITOR_VERSION,
	"A MySQL Multi Master monitor"
};

static	void 	*startMonitor(void *,void*);
static	void	stopMonitor(void *);
static	void	registerServer(void *, SERVER *);
static	void	unregisterServer(void *, SERVER *);
static	void	defaultUser(void *, char *, char *);
static	void	diagnostics(DCB *, void *);
static  void    setInterval(void *, size_t);
static	void	detectStaleMaster(void *, int);
static  bool    mon_status_changed(MONITOR_SERVERS* mon_srv);
static  bool    mon_print_fail_status(MONITOR_SERVERS* mon_srv);
static MONITOR_SERVERS *get_current_master(MYSQL_MONITOR *);
static void monitor_set_pending_status(MONITOR_SERVERS *, int);
static void monitor_clear_pending_status(MONITOR_SERVERS *, int);

static MONITOR_OBJECT MyObject = {
	startMonitor,
	stopMonitor,
	registerServer,
	unregisterServer,
	defaultUser,
	diagnostics,
	setInterval
};

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
	LOGIF(LM, (skygw_log_write(
                           LOGFILE_MESSAGE,
                           "Initialise the MySQL Monitor module %s.",
                           version_str)));
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MONITOR_OBJECT *
GetModuleObject()
{
	return &MyObject;
}

/**
 * Start the instance of the monitor, returning a handle on the monitor.
 *
 * This function creates a thread to execute the actual monitoring.
 *
 * @param arg	The current handle - NULL if first start
 * @return A handle to use when interacting with the monitor
 */
static	void 	*
startMonitor(void *arg,void* opt)
{
MYSQL_MONITOR *handle;
CONFIG_PARAMETER* params = (CONFIG_PARAMETER*)opt;
        if (arg)
        {
            handle = arg;	/* Must be a restart */
            handle->shutdown = 0;
        }
        else
        {
            if ((handle = (MYSQL_MONITOR *)malloc(sizeof(MYSQL_MONITOR))) == NULL)
                return NULL;
            handle->databases = NULL;
            handle->shutdown = 0;
            handle->defaultUser = NULL;
            handle->defaultPasswd = NULL;
            handle->id = MONITOR_DEFAULT_ID;
            handle->interval = MONITOR_INTERVAL;
            handle->replicationHeartbeat = 0;
            handle->detectStaleMaster = 0;
            handle->master = NULL;
            spinlock_init(&handle->lock);
        }

	while(params)
	{
	    if(!strcmp(params->name,"detect_stale_master"))
		handle->detectStaleMaster = config_truth_value(params->value);
	    params = params->next;
	}

        handle->tid = (THREAD)thread_start(monitorMain, handle);
        return handle;
}

/**
 * Stop a running monitor
 *
 * @param arg	Handle on thr running monior
 */
static	void	
stopMonitor(void *arg)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;

        handle->shutdown = 1;
        thread_wait((void *)handle->tid);
}

/**
 * Register a server that must be added to the monitored servers for
 * a monitoring module.
 *
 * @param arg	A handle on the running monitor module
 * @param server	The server to add
 */
static	void	
registerServer(void *arg, SERVER *server)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;
MONITOR_SERVERS	*ptr, *db;

	if ((db = (MONITOR_SERVERS *)malloc(sizeof(MONITOR_SERVERS))) == NULL)
		return;
	db->server = server;
	db->con = NULL;
	db->next = NULL;
        db->mon_err_count = 0;
        db->mon_prev_status = 0;
	/* pending status is updated by monitorMain */
	db->pending_status = 0;

	spinlock_acquire(&handle->lock);
        
	if (handle->databases == NULL)
		handle->databases = db;
	else
	{
		ptr = handle->databases;
		while (ptr->next != NULL)
			ptr = ptr->next;
		ptr->next = db;
	}
	spinlock_release(&handle->lock);
}

/**
 * Remove a server from those being monitored by a monitoring module
 *
 * @param arg	A handle on the running monitor module
 * @param server	The server to remove
 */
static	void	
unregisterServer(void *arg, SERVER *server)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;
MONITOR_SERVERS	*ptr, *lptr;

	spinlock_acquire(&handle->lock);
	if (handle->databases == NULL)
	{
		spinlock_release(&handle->lock);
		return;
	}
	if (handle->databases->server == server)
	{
		ptr = handle->databases;
		handle->databases = handle->databases->next;
		free(ptr);
	}
	else
	{
		ptr = handle->databases;
		while (ptr->next != NULL && ptr->next->server != server)
			ptr = ptr->next;
		if (ptr->next)
		{
			lptr = ptr->next;
			ptr->next = ptr->next->next;
			free(lptr);
		}
	}
	spinlock_release(&handle->lock);
}

/**
 * Set the default username and password to use to monitor if the server does not
 * override this.
 *
 * @param arg		The handle allocated by startMonitor
 * @param uname		The default user name
 * @param passwd	The default password
 */
static void
defaultUser(void *arg, char *uname, char *passwd)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;

	if (handle->defaultUser)
		free(handle->defaultUser);
	if (handle->defaultPasswd)
		free(handle->defaultPasswd);
	handle->defaultUser = strdup(uname);
	handle->defaultPasswd = strdup(passwd);
}

/**
 * Daignostic interface
 *
 * @param dcb	DCB to print diagnostics
 * @param arg	The monitor handle
 */
static void diagnostics(DCB *dcb, void *arg)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;
MONITOR_SERVERS	*db;
char		*sep;

	switch (handle->status)
	{
	case MONITOR_RUNNING:
		dcb_printf(dcb, "\tMonitor running\n");
		break;
	case MONITOR_STOPPING:
		dcb_printf(dcb, "\tMonitor stopping\n");
		break;
	case MONITOR_STOPPED:
		dcb_printf(dcb, "\tMonitor stopped\n");
		break;
	}

	dcb_printf(dcb,"\tSampling interval:\t%lu milliseconds\n", handle->interval);
	dcb_printf(dcb,"\tDetect Stale Master:\t%s\n", (handle->detectStaleMaster == 1) ? "enabled" : "disabled");
	dcb_printf(dcb, "\tMonitored servers:	");

	db = handle->databases;
	sep = "";
	while (db)
	{
		dcb_printf(dcb,
                           "%s%s:%d",
                           sep,
                           db->server->name,
                           db->server->port);
		sep = ", ";
		db = db->next;
	}
	dcb_printf(dcb, "\n");
}

/**
 * Monitor an individual server
 *
 * @param handle        The MySQL Monitor object
 * @param database	The database to probe
 */
static void
monitorDatabase(MYSQL_MONITOR *handle, MONITOR_SERVERS *database)
{
MYSQL_ROW	  row;
MYSQL_RES	  *result;
int		  num_fields;
int               isslave = 0;
int               ismaster = 0;
char		  *uname  = handle->defaultUser; 
char              *passwd = handle->defaultPasswd;
unsigned long int server_version = 0;
char 		  *server_string;

        if (database->server->monuser != NULL)
	{
		uname = database->server->monuser;
		passwd = database->server->monpw;
	}
	
	if (uname == NULL)
		return;
        
	/* Don't probe servers in maintenance mode */
	if (SERVER_IN_MAINT(database->server))
		return;

        /** Store prevous status */
        database->mon_prev_status = database->server->status;
        
	if (database->con == NULL || mysql_ping(database->con) != 0)
	{
		char *dpwd = decryptPassword(passwd);
                int  rc;
                int  read_timeout = 1;

                database->con = mysql_init(NULL);

                rc = mysql_options(database->con, MYSQL_OPT_READ_TIMEOUT, (void *)&read_timeout);
                
		if (mysql_real_connect(database->con,
                                       database->server->name,
                                       uname,
                                       dpwd,
                                       NULL,
                                       database->server->port,
                                       NULL,
                                       0) == NULL)
		{
                        free(dpwd);
                        
                        if (mon_print_fail_status(database))
                        {
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Monitor was unable to connect to "
                                        "server %s:%d : \"%s\"",
                                        database->server->name,
                                        database->server->port,
                                        mysql_error(database->con))));
                        }

			/* The current server is not running
			 *
			 * Store server NOT running in server and monitor server pending struct
			 *
			 */
			if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
			{
				server_set_status(database->server, SERVER_AUTH_ERROR);
				monitor_set_pending_status(database, SERVER_AUTH_ERROR);
			}
			server_clear_status(database->server, SERVER_RUNNING);
			monitor_clear_pending_status(database, SERVER_RUNNING);

			/* Also clear M/S state in both server and monitor server pending struct */
			server_clear_status(database->server, SERVER_SLAVE);
			server_clear_status(database->server, SERVER_MASTER);
			monitor_clear_pending_status(database, SERVER_SLAVE);
			monitor_clear_pending_status(database, SERVER_MASTER);

			/* Clean addition status too */
			server_clear_status(database->server, SERVER_STALE_STATUS);
			monitor_clear_pending_status(database, SERVER_STALE_STATUS);

			return;
		}  else {
                        server_clear_status(database->server, SERVER_AUTH_ERROR);
                        monitor_clear_pending_status(database, SERVER_AUTH_ERROR);
                }
		free(dpwd);
	}
        /* Store current status in both server and monitor server pending struct */
	server_set_status(database->server, SERVER_RUNNING);
	monitor_set_pending_status(database, SERVER_RUNNING);

	/* get server version from current server */
	server_version = mysql_get_server_version(database->con);

	/* get server version string */
	server_string = (char *)mysql_get_server_info(database->con);
	if (server_string) {
		database->server->server_string = realloc(database->server->server_string, strlen(server_string)+1);
		if (database->server->server_string)
			strcpy(database->server->server_string, server_string);
	}

        /* get server_id form current node */
        if (mysql_query(database->con, "SELECT @@server_id") == 0
                && (result = mysql_store_result(database->con)) != NULL)
        {
                long server_id = -1;
                num_fields = mysql_num_fields(result);
                while ((row = mysql_fetch_row(result)))
                {
                        server_id = strtol(row[0], NULL, 10);
                        if ((errno == ERANGE && (server_id == LONG_MAX
                                || server_id == LONG_MIN)) || (errno != 0 && server_id == 0))
                        {
                                server_id = -1;
                        }
                        database->server->node_id = server_id;
                }
                mysql_free_result(result);
        }

	/* Check if the Slave_SQL_Running and Slave_IO_Running status is
	 * set to Yes
	 */

	/* Check first for MariaDB 10.x.x and get status for multimaster replication */
	if (server_version >= 100000) {

		if (mysql_query(database->con, "SHOW ALL SLAVES STATUS") == 0
			&& (result = mysql_store_result(database->con)) != NULL)
		{
			int i = 0;
			long master_id = -1;
			num_fields = mysql_num_fields(result);
			while ((row = mysql_fetch_row(result)))
			{
				/* get Slave_IO_Running and Slave_SQL_Running values*/
				if (strncmp(row[12], "Yes", 3) == 0
						&& strncmp(row[13], "Yes", 3) == 0) {
					isslave += 1;
				}

				/* If Slave_IO_Running = Yes, assign the master_id to current server: this allows building 
				 * the replication tree, slaves ids will be added to master(s) and we will have at least the 
				 * root master server.
				 * Please note, there could be no slaves at all if Slave_SQL_Running == 'No'
				 */
				if (strncmp(row[12], "Yes", 3) == 0) {
					/* get Master_Server_Id values */
                                        master_id = atol(row[41]);
                                        if (master_id == 0)
                                                master_id = -1;
				}

				i++;
			}
			/* store master_id of current node */
			memcpy(&database->server->master_id, &master_id, sizeof(long));

			mysql_free_result(result);

			/* If all configured slaves are running set this node as slave */
			if (isslave > 0 && isslave == i)
				isslave = 1;
			else
				isslave = 0;
		}
	} else {	
		if (mysql_query(database->con, "SHOW SLAVE STATUS") == 0
			&& (result = mysql_store_result(database->con)) != NULL)
		{
			long master_id = -1;
			num_fields = mysql_num_fields(result);
			while ((row = mysql_fetch_row(result)))
			{
				/* get Slave_IO_Running and Slave_SQL_Running values*/
				if (strncmp(row[10], "Yes", 3) == 0
						&& strncmp(row[11], "Yes", 3) == 0) {
					isslave = 1;
				}

				/* If Slave_IO_Running = Yes, assign the master_id to current server: this allows building 
				 * the replication tree, slaves ids will be added to master(s) and we will have at least the 
				 * root master server.
				 * Please note, there could be no slaves at all if Slave_SQL_Running == 'No'
				 */
				if (strncmp(row[10], "Yes", 3) == 0) {
					/* get Master_Server_Id values */
					master_id = atol(row[39]);
					if (master_id == 0)
						master_id = -1;
				}
			}
			/* store master_id of current node */
			memcpy(&database->server->master_id, &master_id, sizeof(long));

			mysql_free_result(result);
		}
	}

	/* get variable 'read_only' set by an external component */
        if (mysql_query(database->con, "SHOW GLOBAL VARIABLES LIKE 'read_only'") == 0
                && (result = mysql_store_result(database->con)) != NULL)
        {
                num_fields = mysql_num_fields(result);
                while ((row = mysql_fetch_row(result)))
                {
                        if (strncasecmp(row[1], "OFF", 3) == 0) {
                                        ismaster = 1;
                        }
                }
                mysql_free_result(result);
        }

	/* Remove addition info */
	monitor_clear_pending_status(database, SERVER_STALE_STATUS);

	/* Set the Slave Role */
	if (isslave)
	{
		monitor_set_pending_status(database, SERVER_SLAVE);
		/* Avoid any possible stale Master state */
		monitor_clear_pending_status(database, SERVER_MASTER);

		/* Set replication depth to 1 */
		database->server->depth = 1;
	} else {
		/* Avoid any possible Master/Slave stale state */
		monitor_clear_pending_status(database, SERVER_SLAVE);
		monitor_clear_pending_status(database, SERVER_MASTER);
	}

	/* Set the Master role */
        if (isslave && ismaster)
        {
		monitor_clear_pending_status(database, SERVER_SLAVE);
                monitor_set_pending_status(database, SERVER_MASTER);

		/* Set replication depth to 0 */
		database->server->depth = 0;
        }

}

/**
 * The entry point for the monitoring module thread
 *
 * @param arg	The handle of the monitor
 */
static void
monitorMain(void *arg)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;
MONITOR_SERVERS	*ptr;
int detect_stale_master = handle->detectStaleMaster;
MONITOR_SERVERS *root_master;
size_t nrounds = 0;

	if (mysql_thread_init())
	{
		LOGIF(LE, (skygw_log_write_flush(
                                   LOGFILE_ERROR,
                                   "Fatal : mysql_thread_init failed in monitor "
                                   "module. Exiting.\n")));
		return;
	}                         

	handle->status = MONITOR_RUNNING;
	while (1)
	{
		if (handle->shutdown)
		{
			handle->status = MONITOR_STOPPING;
			mysql_thread_end();
			handle->status = MONITOR_STOPPED;
			return;
		}

		/** Wait base interval */
		thread_millisleep(MON_BASE_INTERVAL_MS);
		/**
		 * Calculate how far away the monitor interval is from its full
		 * cycle and if monitor interval time further than the base
		 * interval, then skip monitoring checks. Excluding the first
		 * round.
		 */
                if (nrounds != 0 &&
                        ((nrounds*MON_BASE_INTERVAL_MS)%handle->interval) >=
                        MON_BASE_INTERVAL_MS)
                {
                        nrounds += 1;
                        continue;
                }
                nrounds += 1;

		/* start from the first server in the list */
		ptr = handle->databases;

		while (ptr)
		{
			/* copy server status into monitor pending_status */
			ptr->pending_status = ptr->server->status;

			/* monitor current node */
			monitorDatabase(handle, ptr);

                        if (mon_status_changed(ptr))
                        {
                                dcb_call_foreach(ptr->server,DCB_REASON_NOT_RESPONDING);
                        }
                        
                        if (mon_status_changed(ptr) || 
                                mon_print_fail_status(ptr))
                        {
                                LOGIF(LD, (skygw_log_write_flush(
                                        LOGFILE_DEBUG,
                                        "Backend server %s:%d state : %s",
                                        ptr->server->name,
                                        ptr->server->port,
                                        STRSRVSTATUS(ptr->server))));                                
                        }
                        if (SERVER_IS_DOWN(ptr->server))
                        {
                                /** Increase this server'e error count */
                                ptr->mon_err_count += 1;                                
                        }
                        else
                        {
                                /** Reset this server's error count */
                                ptr->mon_err_count = 0;
                        }

			ptr = ptr->next;
		}
	
		/* Get Master server pointer */
		root_master = get_current_master(handle);

		/* Update server status from monitor pending status on that server*/

                ptr = handle->databases;
		while (ptr)
		{
			if (! SERVER_IN_MAINT(ptr->server)) {
				/* If "detect_stale_master" option is On, let's use the previus master */
				if (detect_stale_master && root_master && (!strcmp(ptr->server->name, root_master->server->name) && ptr->server->port == root_master->server->port) && (ptr->server->status & SERVER_MASTER) && !(ptr->pending_status & SERVER_MASTER)) {
					/* in this case server->status will not be updated from pending_status */
					LOGIF(LM, (skygw_log_write_flush(
						LOGFILE_MESSAGE, "[mysql_mon]: root server [%s:%i] is no longer Master, let's use it again even if it could be a stale master, you have been warned!", ptr->server->name, ptr->server->port)));
					/* Set the STALE bit for this server in server struct */
					server_set_status(ptr->server, SERVER_STALE_STATUS);
				} else {
					ptr->server->status = ptr->pending_status;
				}
			}
			ptr = ptr->next;
		}
	}
}
                        
/**
 * Set the monitor sampling interval.
 *
 * @param arg           The handle allocated by startMonitor
 * @param interval      The interval to set in monitor struct, in milliseconds
 */
static void
setInterval(void *arg, size_t interval)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
	memcpy(&handle->interval, &interval, sizeof(unsigned long));
}

/**
 * Enable/Disable the MySQL Replication Stale Master dectection, allowing a previouvsly detected master to still act as a Master.
 * This option must be enabled in order to keep the Master when the replication is stopped or removed from slaves.
 * If the replication is still stopped when MaxSclale is restarted no Master will be available.
 *
 * @param arg		The handle allocated by startMonitor
 * @param enable	To enable it 1, disable it with 0
 */
static void
detectStaleMaster(void *arg, int enable)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
	memcpy(&handle->detectStaleMaster, &enable, sizeof(int));
}

static bool mon_status_changed(
        MONITOR_SERVERS* mon_srv)
{
        bool succp;
        
        if (mon_srv->mon_prev_status != mon_srv->server->status)
        {
                succp = true;
        }
        else
        {
                succp = false;
        }
        return succp;
}

static bool mon_print_fail_status(
        MONITOR_SERVERS* mon_srv)
{
        bool succp;
        int errcount = mon_srv->mon_err_count;
        uint8_t modval;
        
        modval = 1<<(MIN(errcount/10, 7));

        if (SERVER_IS_DOWN(mon_srv->server) && errcount%modval == 0)
        {
                succp = true;
        }
        else
        {
                succp = false;
        }
        return succp;
}

/**
 * Set a pending status bit in the monior server
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
static void
monitor_set_pending_status(MONITOR_SERVERS *ptr, int bit)
{
	ptr->pending_status |= bit;
}

/**
 * Clear a pending status bit in the monior server
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
static void
monitor_clear_pending_status(MONITOR_SERVERS *ptr, int bit)
{
	ptr->pending_status &= ~bit;
}

/*******
 * This function returns the master server
 * from a set of MySQL Multi Master monitored servers
 * and returns the root server (that has SERVER_MASTER bit)
 * The server is returned even for servers in 'maintenance' mode.
 *
 * @param handle        The monitor handle
 * @return              The server at root level with SERVER_MASTER bit
 */

static MONITOR_SERVERS *get_current_master(MYSQL_MONITOR *handle) {
MONITOR_SERVERS *ptr;

	ptr = handle->databases;

	while (ptr)
	{
		/* The server could be in SERVER_IN_MAINT
		 * that means SERVER_IS_RUNNING returns 0
		 * Let's check only for SERVER_IS_DOWN: server is not running
		 */
		if (SERVER_IS_DOWN(ptr->server)) {
			ptr = ptr->next;
			continue;
		}

		if (ptr->server->depth == 0) {
			handle->master = ptr;
		}

		ptr = ptr->next;
	}


	/*
	 * Return the root master
	 */

	if (handle->master != NULL) {
		/* If the root master is in MAINT, return NULL */
		if (SERVER_IN_MAINT(handle->master->server)) {
			return NULL;
		} else {
			return handle->master;
		}
	} else {
		return NULL;
	}
}

