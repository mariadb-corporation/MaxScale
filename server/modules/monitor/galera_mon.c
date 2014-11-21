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
 * @file galera_mon.c - A MySQL Galera cluster monitor
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 22/07/13	Mark Riddoch		Initial implementation
 * 21/05/14	Massimiliano Pinto	Monitor sets a master server 
 *					that has the lowest value of wsrep_local_index
 * 23/05/14	Massimiliano Pinto	Added 1 configuration option (setInterval).
 * 					Interval is printed in diagnostics.
 * 03/06/14	Mark Riddoch		Add support for maintenance mode
 * 24/06/14	Massimiliano Pinto	Added depth level 0 for each node
 * 30/10/14	Massimiliano Pinto	Added disableMasterFailback feature
 * 10/11/14	Massimiliano Pinto	Added setNetworkTimeout for connect,read,write
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

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static	void	monitorMain(void *);

static char *version_str = "V1.4.0";

MODULE_INFO	info = {
	MODULE_API_MONITOR,
	MODULE_BETA_RELEASE,
	MONITOR_VERSION,
	"A Galera cluster monitor"
};

static	void 	*startMonitor(void *);
static	void	stopMonitor(void *);
static	void	registerServer(void *, SERVER *);
static	void	unregisterServer(void *, SERVER *);
static	void	defaultUsers(void *, char *, char *);
static	void	diagnostics(DCB *, void *);
static  void    setInterval(void *, size_t);
static	MONITOR_SERVERS *get_candidate_master(MONITOR_SERVERS *);
static	MONITOR_SERVERS *set_cluster_master(MONITOR_SERVERS *, MONITOR_SERVERS *, int);
static	void	disableMasterFailback(void *, int);
static	void	setNetworkTimeout(void *arg, int type, int value);
static  bool    mon_status_changed(MONITOR_SERVERS* mon_srv);
static  bool    mon_print_fail_status(MONITOR_SERVERS* mon_srv);

static MONITOR_OBJECT MyObject = { 
	startMonitor, 
	stopMonitor, 
	registerServer, 
	unregisterServer, 
	defaultUsers, 
	diagnostics, 
	setInterval,
	setNetworkTimeout,
	NULL, 
	NULL, 
	NULL,
	disableMasterFailback
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
                           "Initialise the MySQL Galera Monitor module %s.\n",
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
 * @return A handle to use when interacting with the monitor
 */
static	void 	*
startMonitor(void *arg)
{
MYSQL_MONITOR *handle;

	if (arg != NULL)
	{
		handle = (MYSQL_MONITOR *)arg;
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
		handle->disableMasterFailback = 0;
		handle->master = NULL;
		handle->connect_timeout=DEFAULT_CONNECT_TIMEOUT;
		handle->read_timeout=DEFAULT_READ_TIMEOUT;
		handle->write_timeout=DEFAULT_WRITE_TIMEOUT;
		spinlock_init(&handle->lock);
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
 * Diagnostic interface
 *
 * @param dcb	DCB to send output
 * @param arg	The monitor handle
 */
static void
diagnostics(DCB *dcb, void *arg)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
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
	dcb_printf(dcb,"\tMaster Failback:\t%s\n", (handle->disableMasterFailback == 1) ? "off" : "on");
	dcb_printf(dcb,"\tConnect Timeout:\t%i seconds\n", handle->connect_timeout);
	dcb_printf(dcb,"\tRead Timeout:\t\t%i seconds\n", handle->read_timeout);
	dcb_printf(dcb,"\tWrite Timeout:\t\t%i seconds\n", handle->write_timeout);
	dcb_printf(dcb, "\tMonitored servers:	");

	db = handle->databases;
	sep = "";
	while (db)
	{
		dcb_printf(dcb, "%s%s:%d", sep, db->server->name, db->server->port);
		sep = ", ";
		db = db->next;
	}
	dcb_printf(dcb, "\n");
}

/**
 * Set the default username and password to use to monitor if the server does not
 * override this.
 *
 * @param arg           The handle allocated by startMonitor
 * @param uname         The default user name
 * @param passwd        The default password
 */
static void
defaultUsers(void *arg, char *uname, char *passwd)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;

 	if (handle->defaultUser)
		free(handle->defaultUser);
	if (handle->defaultPasswd)
		free(handle->defaultPasswd);
	handle->defaultUser = strdup(uname);
	handle->defaultPasswd = strdup(passwd);
}

/**
 * Monitor an individual server
 *
 * @param handle        The MySQL Monitor object
 * @param database      The database to probe
 */
static void
monitorDatabase(MYSQL_MONITOR *handle, MONITOR_SERVERS *database)
{
MYSQL_ROW	row;
MYSQL_RES	*result;
int		num_fields;
int		isjoined = 0;
char		*uname  = handle->defaultUser;
char		*passwd = handle->defaultPasswd;
unsigned long int	server_version = 0;
char 			*server_string;

	if (database->server->monuser != NULL)
	{
		uname = database->server->monuser;
		passwd = database->server->monpw;
	}
	if (uname == NULL)
		return;

	/* Don't even probe server flagged as in maintenance */
	if (SERVER_IN_MAINT(database->server))
		return;

	/** Store previous status */
	database->mon_prev_status = database->server->status;

	if (database->con == NULL || mysql_ping(database->con) != 0)
	{
		char *dpwd = decryptPassword(passwd);
		int rc;
		int connect_timeout = handle->connect_timeout;
		int read_timeout = handle->read_timeout;
		int write_timeout = handle->write_timeout;;

		database->con = mysql_init(NULL);

		rc = mysql_options(database->con, MYSQL_OPT_CONNECT_TIMEOUT, (void *)&connect_timeout);
		rc = mysql_options(database->con, MYSQL_OPT_READ_TIMEOUT, (void *)&read_timeout);
		rc = mysql_options(database->con, MYSQL_OPT_WRITE_TIMEOUT, (void *)&write_timeout);

		if (mysql_real_connect(database->con, database->server->name,
			uname, dpwd, NULL, database->server->port, NULL, 0) == NULL)
		{
			free(dpwd);

			server_clear_status(database->server, SERVER_RUNNING);

			/* Also clear Joined, M/S and Stickiness bits */
			server_clear_status(database->server, SERVER_JOINED);
			server_clear_status(database->server, SERVER_SLAVE);
			server_clear_status(database->server, SERVER_MASTER);
			server_clear_status(database->server, SERVER_MASTER_STICKINESS);

			if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
			{
				server_set_status(database->server, SERVER_AUTH_ERROR);
			}

			database->server->node_id = -1;

			if (mon_status_changed(database) && mon_print_fail_status(database))
			{
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error : Monitor was unable to connect to "
					"server %s:%d : \"%s\"",
					database->server->name,
					database->server->port,
					mysql_error(database->con))));
			}

			return;
		}
		else
		{
			server_clear_status(database->server, SERVER_AUTH_ERROR);
		}
		free(dpwd);
	}

	/* If we get this far then we have a working connection */
	server_set_status(database->server, SERVER_RUNNING);

	/* get server version from current server */
	server_version = mysql_get_server_version(database->con);

	/* get server version string */
	server_string = (char *)mysql_get_server_info(database->con);
	if (server_string) {
		database->server->server_string = realloc(database->server->server_string, strlen(server_string)+1);
		if (database->server->server_string)
			strcpy(database->server->server_string, server_string);
	}	

	/* Check if the the Galera FSM shows this node is joined to the cluster */
	if (mysql_query(database->con, "SHOW STATUS LIKE 'wsrep_local_state_comment'") == 0
		&& (result = mysql_store_result(database->con)) != NULL)
	{
		num_fields = mysql_num_fields(result);
		while ((row = mysql_fetch_row(result)))
		{
			if (strncasecmp(row[1], "SYNCED", 3) == 0)
				isjoined = 1;
		}
		mysql_free_result(result);
	}

	/* Check the the Galera node index in the cluster */
	if (mysql_query(database->con, "SHOW STATUS LIKE 'wsrep_local_index'") == 0
		&& (result = mysql_store_result(database->con)) != NULL)
	{
		long local_index = -1;
		num_fields = mysql_num_fields(result);
		while ((row = mysql_fetch_row(result)))
		{
			local_index = strtol(row[1], NULL, 10);
			if ((errno == ERANGE && (local_index == LONG_MAX
				|| local_index == LONG_MIN)) || (errno != 0 && local_index == 0))
			{
				local_index = -1;
			}
			database->server->node_id = local_index;
		}
		mysql_free_result(result);
	}

	if (isjoined)
		server_set_status(database->server, SERVER_JOINED);
	else
		server_clear_status(database->server, SERVER_JOINED);
}

/**
 * The entry point for the monitoring module thread
 *
 * @param arg	The handle of the monitor
 */
static void
monitorMain(void *arg)
{
MYSQL_MONITOR		*handle = (MYSQL_MONITOR *)arg;
MONITOR_SERVERS		*ptr;
size_t			nrounds = 0;
MONITOR_SERVERS		*candidate_master = NULL;
int			master_stickiness = handle->disableMasterFailback;
int			is_cluster=0;
int			log_no_members = 1;

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

		if (nrounds != 0 && ((nrounds*MON_BASE_INTERVAL_MS)%handle->interval) >= MON_BASE_INTERVAL_MS) 
		{
			nrounds += 1;
			continue;
		}

		nrounds += 1;

		/* reset cluster members counter */
		is_cluster=0;

		ptr = handle->databases;

		while (ptr)
		{
			monitorDatabase(handle, ptr);

			/* clear bits for non member nodes */
			if ( ! SERVER_IN_MAINT(ptr->server) && (ptr->server->node_id < 0 || ! SERVER_IS_JOINED(ptr->server))) {
				ptr->server->depth = -1;

				/* clear M/S status */
				server_clear_status(ptr->server, SERVER_SLAVE);
                		server_clear_status(ptr->server, SERVER_MASTER);
				
				/* clear master sticky status */
				server_clear_status(ptr->server, SERVER_MASTER_STICKINESS);
			}

			/* Log server status change */
			if (mon_status_changed(ptr))
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

		/*
		 * Let's select a master server:
		 * it could be the candidate master following MIN(node_id) rule or
		 * the server that was master in the previous monitor polling cycle
		 * Decision depends on master_stickiness value set in configuration
		 */

		/* get the candidate master, followinf MIN(node_id) rule */
		candidate_master = get_candidate_master(handle->databases);

		/* Select the master, based on master_stickiness */
		handle->master = set_cluster_master(handle->master, candidate_master, master_stickiness);

		ptr = handle->databases;

		while (ptr && handle->master) {
			if (!SERVER_IS_JOINED(ptr->server) || SERVER_IN_MAINT(ptr->server)) {
				ptr = ptr->next;
				continue;
			}

			if (ptr != handle->master) {
				/* set the Slave role */
				server_set_status(ptr->server, SERVER_SLAVE);
				server_clear_status(ptr->server, SERVER_MASTER);

				/* clear master stickyness */
				server_clear_status(ptr->server, SERVER_MASTER_STICKINESS);
			} else {
				/* set the Master role */
				server_set_status(handle->master->server, SERVER_MASTER);
				server_clear_status(handle->master->server, SERVER_SLAVE);

				if (candidate_master && handle->master->server->node_id != candidate_master->server->node_id) {
					/* set master stickyness */
					server_set_status(handle->master->server, SERVER_MASTER_STICKINESS);
				} else {
					/* clear master stickyness */
					server_clear_status(ptr->server, SERVER_MASTER_STICKINESS);
				}			
			}

			is_cluster++;

			ptr = ptr->next;
		}

		if (is_cluster == 0 && log_no_members) {
			LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error: there are no cluster members")));
			log_no_members = 0;
		} else {
			if (is_cluster > 0 && log_no_members == 0) {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Info: found cluster members")));
				log_no_members = 1;
			}
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
 * get candidate master from all nodes
 *
 * The current available rule: get the server with MIN(node_id)
 * node_id comes from 'wsrep_local_index' variable
 *
 * @param	servers The monitored servers list
 * @return	The candidate master on success, NULL on failure
 */
static MONITOR_SERVERS *get_candidate_master(MONITOR_SERVERS *servers) {
	MONITOR_SERVERS *ptr = servers;
	MONITOR_SERVERS *candidate_master = NULL;
	long min_id = -1;
	
	/* set min_id to the lowest value of ptr->server->node_id */
	while(ptr) {
		if ((! SERVER_IN_MAINT(ptr->server)) && ptr->server->node_id >= 0 && SERVER_IS_JOINED(ptr->server)) {
			ptr->server->depth = 0;
			if ((ptr->server->node_id < min_id) && min_id >= 0) {
				min_id = ptr->server->node_id;
				candidate_master = ptr;
			} else {
				if (min_id < 0) {
					min_id = ptr->server->node_id;
					candidate_master = ptr;
				}
			}
		}

		ptr = ptr->next;
	}

	return candidate_master;
}

/**
 * set the master server in the cluster
 *
 * master could be the last one from previous monitor cycle Iis running) or
 * the candidate master.
 * The selection is based on the configuration option mapped to master_stickiness
 * The candidate master may change over time due to
 * 'wsrep_local_index' value change in the Galera Cluster
 * Enabling master_stickiness will avoid master change unless a failure is spotted
 *
 * @param	current_master Previous master server
 * @param	candidate_master The candidate master server accordingly to the selection rule
 * @return	The  master node pointer (could be NULL)
 */
static MONITOR_SERVERS *set_cluster_master(MONITOR_SERVERS *current_master, MONITOR_SERVERS *candidate_master, int master_stickiness) {
	/*
	 * if current master is not set or master_stickiness is not enable
	 * just return candidate_master.
	 */
	if (current_master == NULL || master_stickiness == 0) {
		return candidate_master;
	} else {
		/*
		 * if current_master is still a cluster member use it
		 *
		 */
		if (SERVER_IS_JOINED(current_master->server) && (! SERVER_IN_MAINT(current_master->server))) {
			return current_master;
		} else
			return candidate_master;
	}
}

/**
 * Disable/Enable the Master failback in a Galera Cluster.
 *
 * A restarted / rejoined node may get back the previous 'wsrep_local_index'
 * from Cluster: if the value is the lowest in the cluster it will be selected as Master
 * This will cause a Master change even if there is no failure.
 * The option if set to 1 will avoid this situation, keeping the current Master (if running) available
 *
 * @param arg           The handle allocated by startMonitor
 * @param disable       To disable it use 1, 0 keeps failback
 */
static void
disableMasterFailback(void *arg, int disable)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
        memcpy(&handle->disableMasterFailback, &disable, sizeof(int));
}

/**
 * Set the default id to use in the monitor.
 *
 * @param arg           The handle allocated by startMonitor
 * @param type          The connect timeout type
 * @param value         The timeout value to set
 */
static void
setNetworkTimeout(void *arg, int type, int value)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
int max_timeout = (int)(handle->interval/1000);
int new_timeout = max_timeout -1;

	if (new_timeout <= 0)
		new_timeout = DEFAULT_CONNECT_TIMEOUT;

	switch(type) {
		case MONITOR_CONNECT_TIMEOUT:
			if (value < max_timeout) {
				memcpy(&handle->connect_timeout, &value, sizeof(int));
			} else {
				memcpy(&handle->connect_timeout, &new_timeout, sizeof(int));
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"warning : Monitor Connect Timeout %i is greater than monitor interval ~%i seconds"
					", lowering to %i seconds", value, max_timeout, new_timeout)));
			}
			break;

		case MONITOR_READ_TIMEOUT:
			if (value < max_timeout) {
				memcpy(&handle->read_timeout, &value, sizeof(int));
			} else {
				memcpy(&handle->read_timeout, &new_timeout, sizeof(int));
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
						"warning : Monitor Read Timeout %i is greater than monitor interval ~%i seconds"
						", lowering to %i seconds", value, max_timeout, new_timeout)));
			}
			break;

                case MONITOR_WRITE_TIMEOUT:
			if (value < max_timeout) {
				memcpy(&handle->write_timeout, &value, sizeof(int));
			} else {
				memcpy(&handle->write_timeout, &new_timeout, sizeof(int));
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"warning : Monitor Write Timeout %i is greater than monitor interval ~%i seconds"
					", lowering to %i seconds", value, max_timeout, new_timeout)));
			}
			break;
		default:
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error : Monitor setNetworkTimeout received an unsupported action type %i", type)));
			break;
	}
}

/**
 * Check if current monitored server status has changed
 *
 * @param mon_srv	The monitored server
 * @return		true if status has changed or false
 */
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

/**
 * Check if current monitored server has a loggable failure status
 *
 * @param mon_srv	The monitored server
 * @return		true if failed status can be logged or false
 */
static bool mon_print_fail_status(
        MONITOR_SERVERS* mon_srv)
{
        bool succp;
        int errcount = mon_srv->mon_err_count;
        uint8_t modval;

        modval = 1<<(MIN(errcount/10, 7));

        if (SERVER_IS_DOWN(mon_srv->server) && errcount == 0)
        {
                succp = true;
        }
        else
        {
                succp = false;
        }
        return succp;
}
