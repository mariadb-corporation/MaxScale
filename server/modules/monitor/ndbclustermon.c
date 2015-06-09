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
 * @file ndbcluster_mon.c - A MySQL cluster SQL node monitor
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 25/07/14	Massimiliano Pinto	Initial implementation
 * 10/11/14	Massimiliano Pinto	Added setNetworkTimeout for connect,read,write
 * 08/05/15     Markus Makela           Addition of launchable scripts
 *
 * @endverbatim
 */


#include <mysqlmon.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static	void	monitorMain(void *);

static char *version_str = "V2.1.0";

MODULE_INFO	info = {
	MODULE_API_MONITOR,
	MODULE_BETA_RELEASE,
	MONITOR_VERSION,
	"A MySQL cluster SQL node monitor"
};

static	void 	*startMonitor(void *,void*);
static	void	stopMonitor(void *);
static	void	diagnostics(DCB *, void *);
bool isNdbEvent(monitor_event_t event);

static MONITOR_OBJECT MyObject = { 
	startMonitor, 
	stopMonitor,
	diagnostics
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
                           "Initialise the MySQL Cluster Monitor module %s.\n",
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
startMonitor(void *arg,void* opt)
{
    MONITOR* mon = (MONITOR*)arg;
    MYSQL_MONITOR *handle = mon->handle;
    CONFIG_PARAMETER* params = (CONFIG_PARAMETER*)opt;
    bool have_events = false;

    if (handle != NULL)
    {
	handle->shutdown = 0;
    }
    else
    {
	if ((handle = (MYSQL_MONITOR *)malloc(sizeof(MYSQL_MONITOR))) == NULL)
	    return NULL;
	handle->shutdown = 0;
	handle->id = MONITOR_DEFAULT_ID;
	handle->script = NULL;
	handle->master = NULL;
	memset(handle->events,false,sizeof(handle->events));
	spinlock_init(&handle->lock);
    }
    while(params)
    {
	if(!strcmp(params->name,"script"))
	{
	    if(handle->script)
		free(handle->script);
	    if(access(params->value,X_OK) == 0)
	    {
		handle->script = strdup(params->value);
	    }
	    else
	    {
		if(access(params->value,F_OK) == 0)
		{
		skygw_log_write(LE,
			 "Error: The file cannot be executed: %s",
			 params->value);
		}
		else
		{
		skygw_log_write(LE,
			 "Error: The file cannot be found: %s",
			 params->value);
		}
		handle->script = NULL;
	    }
	}
	else if(!strcmp(params->name,"events"))
	{
	    mon_parse_event_string(&handle->events,sizeof(handle->events),params->value);
	    have_events = true;
	}
	params = params->next;
    }
    /** If no specific events are given, enable them all */
    if(!have_events)
    {
	memset(handle->events,true,sizeof(handle->events));
    }
    handle->tid = (THREAD)thread_start(monitorMain, mon);
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
    MONITOR* mon = (MONITOR*)arg;
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)mon->handle;

        handle->shutdown = 1;
        thread_wait((void *)handle->tid);
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
    MONITOR* mon = arg;
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)mon->handle;
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

	dcb_printf(dcb,"\tSampling interval:\t%lu milliseconds\n", mon->interval);
	dcb_printf(dcb,"\tConnect Timeout:\t%i seconds\n", mon->connect_timeout);
	dcb_printf(dcb,"\tRead Timeout:\t\t%i seconds\n", mon->read_timeout);
	dcb_printf(dcb,"\tWrite Timeout:\t\t%i seconds\n", mon->write_timeout);
	dcb_printf(dcb, "\tMonitored servers:	");

	db = mon->databases;
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
 * Monitor an individual server
 *
 * @param database	The database to probe
 */
static void
monitorDatabase(MONITOR_SERVERS	*database, char *defaultUser, char *defaultPasswd, MONITOR *mon)
{
    MYSQL_MONITOR* handle = mon->handle;
MYSQL_ROW	row;
MYSQL_RES	*result;
int		isjoined = 0;
char            *uname = defaultUser, *passwd = defaultPasswd;
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

	if (database->con == NULL || mysql_ping(database->con) != 0)
	{
		char *dpwd = decryptPassword(passwd);
                int connect_timeout = mon->connect_timeout;
                int read_timeout = mon->read_timeout;
                int write_timeout = mon->write_timeout;

		if(database->con)
		    mysql_close(database->con);
                database->con = mysql_init(NULL);

                mysql_options(database->con, MYSQL_OPT_CONNECT_TIMEOUT, (void *)&connect_timeout);
                mysql_options(database->con, MYSQL_OPT_READ_TIMEOUT, (void *)&read_timeout);
                mysql_options(database->con, MYSQL_OPT_WRITE_TIMEOUT, (void *)&write_timeout);

		if (mysql_real_connect(database->con, database->server->name,
			uname, dpwd, NULL, database->server->port, NULL, 0) == NULL)
		{
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Error : Monitor was unable to connect to "
				"server %s:%d : \"%s\"",
				database->server->name,
				database->server->port,
				mysql_error(database->con))));
			server_clear_status(database->server, SERVER_RUNNING);
			if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
			{
				server_set_status(database->server, SERVER_AUTH_ERROR);
			}
			database->server->node_id = -1;
			free(dpwd);
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

	/* get server version string */
	server_string = (char *)mysql_get_server_info(database->con);
	if (server_string) {
		database->server->server_string = realloc(database->server->server_string, strlen(server_string)+1);
		if (database->server->server_string)
			strcpy(database->server->server_string, server_string);
	}	

	/* Check if the the SQL node is able to contact one or more data nodes */
	if (mysql_query(database->con, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'") == 0
		&& (result = mysql_store_result(database->con)) != NULL)
	{
		while ((row = mysql_fetch_row(result)))
		{
			if (atoi(row[1]) > 0)
				isjoined = 1;
		}
		mysql_free_result(result);
	}

	/* Check the the SQL node id in the MySQL cluster */
	if (mysql_query(database->con, "SHOW STATUS LIKE 'Ndb_cluster_node_id'") == 0
		&& (result = mysql_store_result(database->con)) != NULL)
	{
		long cluster_node_id = -1;
		while ((row = mysql_fetch_row(result)))
		{
			cluster_node_id = strtol(row[1], NULL, 10);
			if ((errno == ERANGE && (cluster_node_id == LONG_MAX
				|| cluster_node_id == LONG_MIN)) || (errno != 0 && cluster_node_id == 0))
			{
				cluster_node_id = -1;
			}
			database->server->node_id = cluster_node_id;
		}
		mysql_free_result(result);
	}

	if (isjoined) {
		server_set_status(database->server, SERVER_NDB);
		database->server->depth = 0;
	} else {
		server_clear_status(database->server, SERVER_NDB);
		database->server->depth = -1;
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
    MONITOR* mon = arg;
MYSQL_MONITOR	*handle;
MONITOR_SERVERS	*ptr;
size_t nrounds = 0;

spinlock_acquire(&mon->lock);
handle = (MYSQL_MONITOR *)mon->handle;
spinlock_release(&mon->lock);

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
			((nrounds*MON_BASE_INTERVAL_MS)%mon->interval) >=
			MON_BASE_INTERVAL_MS) 
		{
			nrounds += 1;
			continue;
		}
		nrounds += 1;
		ptr = mon->databases;

		while (ptr)
		{
			ptr->mon_prev_status = ptr->server->status;
			monitorDatabase(ptr, mon->user, mon->password,mon);

			if (ptr->server->status != ptr->mon_prev_status ||
				SERVER_IS_DOWN(ptr->server))
			{
				LOGIF(LD, (skygw_log_write_flush(
					LOGFILE_DEBUG,
					"Backend server %s:%d state : %s",
					ptr->server->name,
					ptr->server->port,
					STRSRVSTATUS(ptr->server))));
			}

			ptr = ptr->next;
		}

		ptr = mon->databases;
		monitor_event_t evtype;

		while(ptr)
		{
		    /** Execute monitor script if a server state has changed */
		    if(mon_status_changed(ptr))
		    {
			evtype = mon_get_event_type(ptr);
			if(isNdbEvent(evtype))
			{
			    skygw_log_write(LOGFILE_TRACE,"Server changed state: %s[%s:%u]: %s",
				     ptr->server->unique_name,
				     ptr->server->name,ptr->server->port,
				     mon_get_event_name(ptr));
			    if(handle->script && handle->events[evtype])
			    {
				monitor_launch_script(mon,ptr,handle->script);
			    }
			}
		    }
		    ptr = ptr->next;
		}
	}
}


static monitor_event_t ndb_events[] = {
  MASTER_DOWN_EVENT,
  MASTER_UP_EVENT,
  SLAVE_DOWN_EVENT,
  SLAVE_UP_EVENT,
  SERVER_DOWN_EVENT,
  SERVER_UP_EVENT,
  NDB_UP_EVENT,
  NDB_DOWN_EVENT,
  LOST_MASTER_EVENT,
  LOST_SLAVE_EVENT,
  LOST_NDB_EVENT,
  NEW_MASTER_EVENT,
  NEW_SLAVE_EVENT,
  NEW_NDB_EVENT,
  MAX_MONITOR_EVENT
};

/**
 * Check if the event type is one the ndbcustermonitor is interested in.
 * @param event Event to check
 * @return True if the event is monitored, false if it is not
 */
bool isNdbEvent(monitor_event_t event)
{
    int i;
    for(i = 0;ndb_events[i] != MAX_MONITOR_EVENT;i++)
    {
	if(event == ndb_events[i])
	    return true;
    }
    return false;
}