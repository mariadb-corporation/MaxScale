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
 * @file mysql_mon.c - A MySQL replication cluster monitor
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 08/07/13	Mark Riddoch		Initial implementation
 * 11/07/13	Mark Riddoch		Addition of code to check replication
 * 					status
 * 25/07/13	Mark Riddoch		Addition of decrypt for passwords and
 * 					diagnostic interface
 * 20/05/14	Massimiliano Pinto	Addition of support for MariadDB multimaster replication setup.
 *					New server field version_string is updated.
 * 28/05/14	Massimiliano Pinto	Added set Id and configuration options (setInverval)
 *					Parameters are now printed in diagnostics
 * 03/06/14	Mark Ridoch		Add support for maintenance mode
 * 17/06/14	Massimiliano Pinto	Addition of getServerByNodeId routine
 *					and first implementation for depth of replication for nodes.
 * 23/06/14	Massimiliano Pinto	Added replication consistency after replication tree computation
 * 27/06/14	Massimiliano Pinto	Added replication pending status in monitored server, storing there
 *					the status to update in server status field before
 *					starting the replication consistency check.
 *					This will also give routers a consistent "status" of all servers
 * 28/08/14	Massimiliano Pinto	Added detectStaleMaster feature: previous detected master will be used again, even if the replication is stopped.
 *					This means both IO and SQL threads are not working on slaves.
 *					This option is not enabled by default.
 * 10/11/14	Massimiliano Pinto	Addition of setNetworkTimeout for connect, read, write
 * 18/11/14	Massimiliano Pinto	One server only in configuration becomes master.
 *					servers=server1 must be present in mysql_mon and in router sections as well.
 * 08/05/15     Markus Makela           Added launchable scripts
 *
 * @endverbatim
 */


#include <mysqlmon.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static	void	monitorMain(void *);

static char *version_str = "V1.4.0";

MODULE_INFO	info = {
	MODULE_API_MONITOR,
	MODULE_GA,
	MONITOR_VERSION,
	"A MySQL Master/Slave replication monitor"
};

static	void 	*startMonitor(void *,void*);
static	void	stopMonitor(void *);
static	void	diagnostics(DCB *, void *);
static  void    defaultId(void *, unsigned long);
static	MONITOR_SERVERS   *getServerByNodeId(MONITOR_SERVERS *, long);
static	MONITOR_SERVERS   *getSlaveOfNodeId(MONITOR_SERVERS *, long);
static MONITOR_SERVERS *get_replication_tree(MONITOR *, int);
static void set_master_heartbeat(MYSQL_MONITOR *, MONITOR_SERVERS *);
static void set_slave_heartbeat(MONITOR *, MONITOR_SERVERS *);
static int add_slave_to_master(long *, int, long);
bool isMySQLEvent(monitor_event_t event);
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
 * @param opt	Configuration parameters
 * @return A handle to use when interacting with the monitor
 */
static	void 	*
startMonitor(void *arg, void* opt)
{
    MONITOR* monitor = (MONITOR*)arg;
    MYSQL_MONITOR *handle = (MYSQL_MONITOR*)monitor->handle;
    CONFIG_PARAMETER* params = (CONFIG_PARAMETER*)opt;
    bool have_events = false;

    if (handle)
    {
	handle->shutdown = 0;
    }
    else
    {
	if ((handle = (MYSQL_MONITOR *)malloc(sizeof(MYSQL_MONITOR))) == NULL)
	    return NULL;
	handle->shutdown = 0;
	handle->id = config_get_gateway_id();
	handle->replicationHeartbeat = 0;
	handle->detectStaleMaster = 0;
	handle->master = NULL;
	handle->script = NULL;
	memset(handle->events,false,sizeof(handle->events));
	spinlock_init(&handle->lock);
    }

    while(params)
    {
	if(!strcmp(params->name,"detect_stale_master"))
	    handle->detectStaleMaster = config_truth_value(params->value);
	else if(!strcmp(params->name,"detect_replication_lag"))
	    handle->replicationHeartbeat = config_truth_value(params->value);
	else if(!strcmp(params->name,"script"))
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
	    mon_parse_event_string(handle->events,sizeof(handle->events),params->value);
	    have_events = true;
	}
	params = params->next;
    }
    /** If no specific events are given, enable them all */
    if(!have_events)
    {
	memset(handle->events,true,sizeof(handle->events));
    }
    handle->tid = (THREAD)thread_start(monitorMain, monitor);
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
 * Daignostic interface
 *
 * @param dcb	DCB to print diagnostics
 * @param arg	The monitor handle
 */
static void diagnostics(DCB *dcb, void *arg)
{
    MONITOR* mon = (MONITOR*)arg;
    MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)mon->handle;
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
    dcb_printf(dcb,"\tMaxScale MonitorId:\t%lu\n", handle->id);
    dcb_printf(dcb,"\tReplication lag:\t%s\n", (handle->replicationHeartbeat == 1) ? "enabled" : "disabled");
    dcb_printf(dcb,"\tDetect Stale Master:\t%s\n", (handle->detectStaleMaster == 1) ? "enabled" : "disabled");
    dcb_printf(dcb,"\tConnect Timeout:\t%i seconds\n", mon->connect_timeout);
    dcb_printf(dcb,"\tRead Timeout:\t\t%i seconds\n", mon->read_timeout);
    dcb_printf(dcb,"\tWrite Timeout:\t\t%i seconds\n", mon->write_timeout);
    dcb_printf(dcb, "\tMonitored servers:	");
    
    db = mon->databases;
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
monitorDatabase(MONITOR *mon, MONITOR_SERVERS *database)
{
    MYSQL_MONITOR* handle = mon->handle;
MYSQL_ROW	  row;
MYSQL_RES	  *result;
int               isslave = 0;
char		  *uname  = mon->user;
char              *passwd = mon->password;
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

        /** Store previous status */
        database->mon_prev_status = database->server->status;
        
	if (database->con == NULL || mysql_ping(database->con) != 0)
	{
		char *dpwd = decryptPassword(passwd);
                int  connect_timeout = mon->connect_timeout;
                int  read_timeout = mon->read_timeout;
                int  write_timeout = mon->write_timeout;

                database->con = mysql_init(NULL);

                mysql_options(database->con, MYSQL_OPT_CONNECT_TIMEOUT, (void *)&connect_timeout);
                mysql_options(database->con, MYSQL_OPT_READ_TIMEOUT, (void *)&read_timeout);
                mysql_options(database->con, MYSQL_OPT_WRITE_TIMEOUT, (void *)&write_timeout);
                
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
			server_clear_status(database->server, SERVER_SLAVE_OF_EXTERNAL_MASTER);
			server_clear_status(database->server, SERVER_STALE_STATUS);
			monitor_clear_pending_status(database, SERVER_SLAVE_OF_EXTERNAL_MASTER);
			monitor_clear_pending_status(database, SERVER_STALE_STATUS);

			/* Log connect failure only once */
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

	/* Remove addition info */
	monitor_clear_pending_status(database, SERVER_SLAVE_OF_EXTERNAL_MASTER);
	monitor_clear_pending_status(database, SERVER_STALE_STATUS);

	/* Please note, the MASTER status and SERVER_SLAVE_OF_EXTERNAL_MASTER
	 * will be assigned in the monitorMain() via get_replication_tree() routine
	 */

	/* Set the Slave Role */
	if (isslave)
	{
		monitor_set_pending_status(database, SERVER_SLAVE);
		/* Avoid any possible stale Master state */
		monitor_clear_pending_status(database, SERVER_MASTER);
	} else {
		/* Avoid any possible Master/Slave stale state */
		monitor_clear_pending_status(database, SERVER_SLAVE);
		monitor_clear_pending_status(database, SERVER_MASTER);
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
    MONITOR* mon = (MONITOR*) arg;
MYSQL_MONITOR	*handle;
MONITOR_SERVERS	*ptr;
int replication_heartbeat;
int detect_stale_master;
int num_servers=0;
MONITOR_SERVERS *root_master = NULL;
size_t nrounds = 0;
int log_no_master = 1;

spinlock_acquire(&mon->lock);
handle = (MYSQL_MONITOR *)mon->handle;
spinlock_release(&mon->lock);
replication_heartbeat = handle->replicationHeartbeat;
detect_stale_master = handle->detectStaleMaster;

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
		/* reset num_servers */
		num_servers = 0;

		/* start from the first server in the list */
		ptr = mon->databases;

		while (ptr)
		{
			ptr->mon_prev_status = ptr->server->status;

			/* copy server status into monitor pending_status */
			ptr->pending_status = ptr->server->status;

			/* monitor current node */
			monitorDatabase(mon, ptr);

			/* reset the slave list of current node */
			if (ptr->server->slaves) {
				free(ptr->server->slaves);
                        }
			/* create a new slave list */
			ptr->server->slaves = (long *) calloc(MONITOR_MAX_NUM_SLAVES, sizeof(long));

			num_servers++;

                        if (mon_status_changed(ptr))
                        {
				if (SRV_MASTER_STATUS(ptr->mon_prev_status))
				{
					/** Master failed, can't recover */
					LOGIF(LM, (skygw_log_write(
						LOGFILE_MESSAGE,
						"Server %s:%d lost the master status.",
						ptr->server->name,
						ptr->server->port)));
				}
				/**
				 * Here we say: If the server's state changed
				 * so that it isn't running or some other way
				 * lost cluster membership, call call-back function
				 * of every DCB for which such callback was 
				 * registered for this kind of issue (DCB_REASON_...)
				 */
				if (!(SERVER_IS_RUNNING(ptr->server)) || 
					!(SERVER_IS_IN_CLUSTER(ptr->server)))
				{
					dcb_call_foreach(ptr->server,DCB_REASON_NOT_RESPONDING);

				}		


                

                        }
                        
                        if (mon_status_changed(ptr))
                        {
#if defined(SS_DEBUG)
                                LOGIF(LT, (skygw_log_write_flush(
                                        LOGFILE_TRACE,
                                        "Backend server %s:%d state : %s",
                                        ptr->server->name,
                                        ptr->server->port,
                                        STRSRVSTATUS(ptr->server))));
#else
				LOGIF(LD, (skygw_log_write_flush(
					LOGFILE_DEBUG,
					"Backend server %s:%d state : %s",
					ptr->server->name,
					ptr->server->port,
					STRSRVSTATUS(ptr->server))));
#endif
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
	
		ptr = mon->databases;
		/* if only one server is configured, that's is Master */
		if (num_servers == 1) {
			if (SERVER_IS_RUNNING(ptr->server)) {
				ptr->server->depth = 0;
				/* status cleanup */
				monitor_clear_pending_status(ptr, SERVER_SLAVE);

				/* master status set */
				monitor_set_pending_status(ptr, SERVER_MASTER);

				ptr->server->depth = 0;
				handle->master = ptr;
				root_master = ptr;
			}
		} else {
			/* Compute the replication tree */
			root_master = get_replication_tree(mon, num_servers);
		}

		/* Update server status from monitor pending status on that server*/

                ptr = mon->databases;
		while (ptr)
		{
			if (! SERVER_IN_MAINT(ptr->server)) {
				/* If "detect_stale_master" option is On, let's use the previus master */
				if (detect_stale_master && 
					root_master && 
					(!strcmp(ptr->server->name, root_master->server->name) && 
					ptr->server->port == root_master->server->port) && 
					(ptr->server->status & SERVER_MASTER) && 
					!(ptr->pending_status & SERVER_MASTER)) 
				{
					/**
					 * In this case server->status will not be updated from pending_statu
					 * Set the STALE bit for this server in server struct
					 */
					server_set_status(ptr->server, SERVER_STALE_STATUS);

					/* log it once */
                        		if (mon_status_changed(ptr)) {
						LOGIF(LM, (skygw_log_write_flush(
							LOGFILE_MESSAGE, 
							"[mysql_mon]: root server "
							"[%s:%i] is no longer Master,"
							" let's use it again even "
							" if it could be a stale master,"
							" you have been warned!",
							ptr->server->name,
							ptr->server->port)));
					}
				} else {
					ptr->server->status = ptr->pending_status;
				}
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
			if(isMySQLEvent(evtype))
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

		/* log master detection failure of first master becomes available after failure */
		if (root_master && 
			mon_status_changed(root_master) && 
			!(root_master->server->status & SERVER_STALE_STATUS)) 
		{
			if (root_master->pending_status & (SERVER_MASTER) && SERVER_IS_RUNNING(root_master->server)) {
				if (!(root_master->mon_prev_status & SERVER_STALE_STATUS) && 
					!(root_master->server->status & SERVER_MAINT)) 
				{
					LOGIF(LM, (skygw_log_write(
						LOGFILE_MESSAGE,
						"Info : A Master Server is now available: %s:%i",
						root_master->server->name,
						root_master->server->port)));
				}
			} else {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error : No Master can be determined. Last known was %s:%i",
					root_master->server->name,
					root_master->server->port)));
			}
			log_no_master = 1;
		} else {
			if (!root_master && log_no_master) 
			{
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error : No Master can be determined")));
				log_no_master = 0;
			}
		}

		/* Do now the heartbeat replication set/get for MySQL Replication Consistency */
		if (replication_heartbeat && 
			root_master && 
			(SERVER_IS_MASTER(root_master->server) || 
				SERVER_IS_RELAY_SERVER(root_master->server))) 
		{
			set_master_heartbeat(handle, root_master);
			ptr = mon->databases;
			
			while (ptr) {
				if( (! SERVER_IN_MAINT(ptr->server)) && SERVER_IS_RUNNING(ptr->server))
				{
					if (ptr->server->node_id != root_master->server->node_id && 
						(SERVER_IS_SLAVE(ptr->server) || 
							SERVER_IS_RELAY_SERVER(ptr->server))) 
					{
						set_slave_heartbeat(mon, ptr);
					}
				}
				ptr = ptr->next;
			}
                }
	} /*< while (1) */
}
                        
/**
 * Set the default id to use in the monitor.
 *
 * @param arg           The handle allocated by startMonitor
 * @param id            The id to set in monitor struct
 */
static void
defaultId(void *arg, unsigned long id)
                        {
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
	memcpy(&handle->id, &id, sizeof(unsigned long));
                        }

/**
 * Enable/Disable the MySQL Replication hearbeat, detecting slave lag behind master.
 *
 * @param arg		The handle allocated by startMonitor
 * @param enable	To enable it 1, disable it with 0
 */
static void
replicationHeartbeat(void *arg, int enable)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
	memcpy(&handle->replicationHeartbeat, &enable, sizeof(int));
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

/**
 * Fetch a MySQL node by node_id
 *
 * @param ptr           The list of servers to monitor
 * @param node_id	The MySQL server_id to fetch
 * @return		The server with the required server_id
 */
static MONITOR_SERVERS *
getServerByNodeId(MONITOR_SERVERS *ptr, long node_id) {
        SERVER *current;
        while (ptr)
        {
                current = ptr->server;
                if (current->node_id == node_id) {
                        return ptr;
                }
                ptr = ptr->next;
        }
        return NULL;
}

/**
 * Fetch a MySQL slave node from a node_id
 *
 * @param ptr           The list of servers to monitor
 * @param node_id	The MySQL server_id to fetch
 * @return		The slave server of this node_id
 */
static MONITOR_SERVERS *
getSlaveOfNodeId(MONITOR_SERVERS *ptr, long node_id) {
        SERVER *current;
        while (ptr)
        {
                current = ptr->server;
                if (current->master_id == node_id) {
                        return ptr;
                }
                ptr = ptr->next;
        }
        return NULL;
}

/*******
 * This function sets the replication heartbeat
 * into the maxscale_schema.replication_heartbeat table in the current master.
 * The inserted values will be seen from all slaves replication from this master.
 *
 * @param handle   	The monitor handle
 * @param database   	The number database server
 */
static void set_master_heartbeat(MYSQL_MONITOR *handle, MONITOR_SERVERS *database) {
	unsigned long id = handle->id;
	time_t heartbeat;
	time_t purge_time;
	char heartbeat_insert_query[512]="";
	char heartbeat_purge_query[512]="";

	if (handle->master == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"[mysql_mon]: set_master_heartbeat called without an available Master server")));
		return;
	}

	/* create the maxscale_schema database */
	if (mysql_query(database->con, "CREATE DATABASE IF NOT EXISTS maxscale_schema")) {
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"[mysql_mon]: Error creating maxscale_schema database in Master server"
			": %s", mysql_error(database->con))));

			database->server->rlag = -1;
	}

	/* create repl_heartbeat table in maxscale_schema database */
	if (mysql_query(database->con, "CREATE TABLE IF NOT EXISTS "
			"maxscale_schema.replication_heartbeat "
			"(maxscale_id INT NOT NULL, "
			"master_server_id INT NOT NULL, "
			"master_timestamp INT UNSIGNED NOT NULL, "
			"PRIMARY KEY ( master_server_id, maxscale_id ) ) "
			"ENGINE=MYISAM DEFAULT CHARSET=latin1")) {
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"[mysql_mon]: Error creating maxscale_schema.replication_heartbeat table in Master server"
			": %s", mysql_error(database->con))));

		database->server->rlag = -1;
	}

	/* auto purge old values after 48 hours*/
	purge_time = time(0) - (3600 * 48);

	sprintf(heartbeat_purge_query, "DELETE FROM maxscale_schema.replication_heartbeat WHERE master_timestamp < %lu", purge_time);

	if (mysql_query(database->con, heartbeat_purge_query)) {
		LOGIF(LE, (skygw_log_write_flush(
		LOGFILE_ERROR,
		"[mysql_mon]: Error deleting from maxscale_schema.replication_heartbeat table: [%s], %s",
		heartbeat_purge_query,
		mysql_error(database->con))));
	}

	heartbeat = time(0);

	/* set node_ts for master as time(0) */
	database->server->node_ts = heartbeat;

	sprintf(heartbeat_insert_query, "UPDATE maxscale_schema.replication_heartbeat SET master_timestamp = %lu WHERE master_server_id = %li AND maxscale_id = %lu", heartbeat, handle->master->server->node_id, id);

	/* Try to insert MaxScale timestamp into master */
	if (mysql_query(database->con, heartbeat_insert_query)) {

		database->server->rlag = -1;

		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"[mysql_mon]: Error updating maxscale_schema.replication_heartbeat table: [%s], %s",
			heartbeat_insert_query,
			mysql_error(database->con))));
	} else {
		if (mysql_affected_rows(database->con) == 0) {
			heartbeat = time(0);
			sprintf(heartbeat_insert_query, "REPLACE INTO maxscale_schema.replication_heartbeat (master_server_id, maxscale_id, master_timestamp ) VALUES ( %li, %lu, %lu)", handle->master->server->node_id, id, heartbeat);

			if (mysql_query(database->con, heartbeat_insert_query)) {

				database->server->rlag = -1;

				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"[mysql_mon]: Error inserting into maxscale_schema.replication_heartbeat table: [%s], %s",
					heartbeat_insert_query,
					mysql_error(database->con))));
			} else {
				/* Set replication lag to 0 for the master */
				database->server->rlag = 0;

				LOGIF(LD, (skygw_log_write_flush(
					LOGFILE_DEBUG,
					"[mysql_mon]: heartbeat table inserted data for %s:%i", database->server->name, database->server->port)));
			}
		} else {
			/* Set replication lag as 0 for the master */
			database->server->rlag = 0;

			LOGIF(LD, (skygw_log_write_flush(
				LOGFILE_DEBUG,
				"[mysql_mon]: heartbeat table updated for Master %s:%i", database->server->name, database->server->port)));
		}
	}
}

/*******
 * This function gets the replication heartbeat
 * from the maxscale_schema.replication_heartbeat table in the current slave
 * and stores the timestamp and replication lag in the slave server struct
 *
 * @param handle   	The monitor handle
 * @param database   	The number database server
 */
static void set_slave_heartbeat(MONITOR* mon, MONITOR_SERVERS *database) {
    MYSQL_MONITOR *handle = (MYSQL_MONITOR*)mon->handle;
	unsigned long id = handle->id;
	time_t heartbeat;
	char select_heartbeat_query[256] = "";
	MYSQL_ROW row;
	MYSQL_RES *result;

	if (handle->master == NULL) {
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"[mysql_mon]: set_slave_heartbeat called without an available Master server")));
		return;
	}

	/* Get the master_timestamp value from maxscale_schema.replication_heartbeat table */

	sprintf(select_heartbeat_query, "SELECT master_timestamp "
		"FROM maxscale_schema.replication_heartbeat "
		"WHERE maxscale_id = %lu AND master_server_id = %li",
		id, handle->master->server->node_id);

	/* if there is a master then send the query to the slave with master_id */
	if (handle->master !=NULL && (mysql_query(database->con, select_heartbeat_query) == 0
		&& (result = mysql_store_result(database->con)) != NULL)) {
		int rows_found = 0;

		while ((row = mysql_fetch_row(result))) {
			int rlag = -1;
			time_t slave_read;

			rows_found = 1;

			heartbeat = time(0);
			slave_read = strtoul(row[0], NULL, 10);

			if ((errno == ERANGE && (slave_read == LONG_MAX || slave_read == LONG_MIN)) || (errno != 0 && slave_read == 0)) {
				slave_read = 0;
			}

			if (slave_read) {
				/* set the replication lag */
				rlag = heartbeat - slave_read;
			}

			/* set this node_ts as master_timestamp read from replication_heartbeat table */
			database->server->node_ts = slave_read;

			if (rlag >= 0) {
				/* store rlag only if greater than monitor sampling interval */
				database->server->rlag = (rlag > (mon->interval / 1000)) ? rlag : 0;
			} else {
				database->server->rlag = -1;
			}

			LOGIF(LD, (skygw_log_write_flush(
				LOGFILE_DEBUG,
				"[mysql_mon]: replication heartbeat: "
				"Slave %s:%i has %i seconds lag",
				database->server->name,
				database->server->port,
				database->server->rlag)));
		}
		if (!rows_found) {
			database->server->rlag = -1;
			database->server->node_ts = 0;
		}

		mysql_free_result(result);
	} else {
		database->server->rlag = -1;
		database->server->node_ts = 0;

		if (handle->master->server->node_id < 0) {
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"[mysql_mon]: error: replication heartbeat: "
				"master_server_id NOT available for %s:%i",
				database->server->name,
				database->server->port)));
		} else {
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"[mysql_mon]: error: replication heartbeat: "
				"failed selecting from hearthbeat table of %s:%i : [%s], %s",
				database->server->name,
				database->server->port,
				select_heartbeat_query,
				mysql_error(database->con))));
		}
	}
}

/*******
 * This function computes the replication tree
 * from a set of MySQL Master/Slave monitored servers
 * and returns the root server with SERVER_MASTER bit.
 * The tree is computed even for servers in 'maintenance' mode.
 *
 * @param handle   	The monitor handle
 * @param num_servers   The number of servers monitored
 * @return		The server at root level with SERVER_MASTER bit
 */

static MONITOR_SERVERS *get_replication_tree(MONITOR *mon, int num_servers) {
	MYSQL_MONITOR* handle = (MYSQL_MONITOR*)mon->handle;
	MONITOR_SERVERS *ptr;
	MONITOR_SERVERS *backend;
	SERVER *current;
	int depth=0;
	long node_id;
	int root_level;

	ptr = mon->databases;
	root_level = num_servers;

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
		depth = 0;
		current = ptr->server;

		node_id = current->master_id;
		if (node_id < 1) {
			MONITOR_SERVERS *find_slave;
			find_slave = getSlaveOfNodeId(mon->databases, current->node_id);

			if (find_slave == NULL) {
				current->depth = -1;
				ptr = ptr->next;

				continue;
			} else {
				current->depth = 0;
			}
		} else {
			depth++;
		} 

		while(depth <= num_servers) {
			/* set the root master at lowest depth level */
			if (current->depth > -1 && current->depth < root_level) {
				root_level = current->depth;
				handle->master = ptr;
			}
			backend = getServerByNodeId(mon->databases, node_id);

			if (backend) {
				node_id = backend->server->master_id;
			} else {
				node_id = -1;
			}

			if (node_id > 0) {
				current->depth = depth + 1;
				depth++;

			} else {
				MONITOR_SERVERS *master;
				current->depth = depth;

				master = getServerByNodeId(mon->databases, current->master_id);
				if (master && master->server && master->server->node_id > 0) {
					add_slave_to_master(master->server->slaves, MONITOR_MAX_NUM_SLAVES, current->node_id);
					master->server->depth = current->depth -1;
					monitor_set_pending_status(master, SERVER_MASTER);
					handle->master = master;
				} else {
					if (current->master_id > 0) {
						/* this server is slave of another server not in MaxScale configuration
						 * we cannot use it as a real slave.
						 */
						monitor_clear_pending_status(ptr, SERVER_SLAVE);
						monitor_set_pending_status(ptr, SERVER_SLAVE_OF_EXTERNAL_MASTER);
					}
				}
				break;
			}

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

/*******
 * This function add a slave id into the slaves server field
 * of its master server
 *
 * @param slaves_list  	The slave list array of the master server
 * @param list_size   	The size of the slave list
 * @param node_id   	The node_id of the slave to be inserted
 * @return		1 for inserted value and 0 otherwise
 */
static int add_slave_to_master(long *slaves_list, int list_size, long node_id) {
        int i;
        for (i = 0; i< list_size; i++) {
                if (slaves_list[i] == 0) {
                        memcpy(&slaves_list[i], &node_id, sizeof(long));
                        return 1;
                }
        }
        return 0;
}

static monitor_event_t mysql_events[] = {
  MASTER_DOWN_EVENT,
  MASTER_UP_EVENT,
  SLAVE_DOWN_EVENT,
  SLAVE_UP_EVENT,
  SERVER_DOWN_EVENT,
  SERVER_UP_EVENT,
  LOST_MASTER_EVENT,
  LOST_SLAVE_EVENT,
  NEW_MASTER_EVENT,
  NEW_SLAVE_EVENT,
  MAX_MONITOR_EVENT
};
/**
 * Check if the MySQL monitor is monitoring this event type.
 * @param event Event to check
 * @return True if the event is monitored, false if it is not
 * */
bool isMySQLEvent(monitor_event_t event)
{
    int i;
    for(i = 0;mysql_events[i] != MAX_MONITOR_EVENT;i++)
    {
	if(event == mysql_events[i])
	    return true;
    }
    return false;
}