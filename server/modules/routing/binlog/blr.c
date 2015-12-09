/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2014-2015
 */

/**
 * @file blr.c - binlog router, allows MaxScale to act as an intermediatory for replication
 *
 * The binlog router is designed to be used in replication environments to
 * increase the replication fanout of a master server. It provides a transparant
 * mechanism to read the binlog entries for multiple slaves while requiring
 * only a single connection to the actual master to support the slaves.
 *
 * The current prototype implement is designed to support MySQL 5.6 and has
 * a number of limitations. This prototype is merely a proof of concept and
 * should not be considered production ready.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 02/04/2014	Mark Riddoch		Initial implementation
 * 17/02/2015	Massimiliano Pinto	Addition of slave port and username in diagnostics
 * 18/02/2015	Massimiliano Pinto	Addition of dcb_close in closeSession
 * 07/05/2015   Massimiliano Pinto      Addition of MariaDB 10 compatibility support
 * 12/06/2015   Massimiliano Pinto      Addition of MariaDB 10 events in diagnostics()
 * 29/06/2015	Massimiliano Pinto	Addition of master.ini for easy startup configuration
 *					If not found router goes into BLRM_UNCONFIGURED state.
 *					Cache dir is 'cache' under router->binlogdir.
 * 07/08/2015	Massimiliano Pinto	Addition of binlog check at startup if trx_safe is on
 * 21/08/2015	Massimiliano Pinto	Added support for new config options:
 *					master_uuid, master_hostname, master_version
 *					If set those values are sent to slaves instead of
 *					saved master responses
 * 23/08/2015	Massimiliano Pinto	Added strerror_r
 * 09/09/2015   Martin Brampton         Modify error handler
 * 30/09/2015	Massimiliano Pinto	Addition of send_slave_heartbeat option
 * 23/10/2015	Markus Makela		Added current_safe_event
 * 27/10/2015   Martin Brampton         Amend getCapabilities to return RCAP_TYPE_NO_RSESSION
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <service.h>
#include <server.h>
#include <router.h>
#include <atomic.h>
#include <spinlock.h>
#include <blr.h>
#include <dcb.h>
#include <spinlock.h>
#include <housekeeper.h>
#include <time.h>

#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#include <mysql_client_server_protocol.h>
#include <ini.h>
#include <sys/stat.h>

static char *version_str = "V2.0.0";

/* The router entry points */
static	ROUTER	*createInstance(SERVICE *service, char **options);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *router_session);
static	void 	freeSession(ROUTER *instance, void *router_session);
static	int	routeQuery(ROUTER *instance, void *router_session, GWBUF *queue);
static	void	diagnostics(ROUTER *instance, DCB *dcb);
static  void    clientReply(
        ROUTER  *instance,
        void    *router_session,
        GWBUF   *queue,
        DCB     *backend_dcb);
static  void    errorReply(
        ROUTER  *instance,
        void    *router_session,
        GWBUF   *message,
        DCB     *backend_dcb,
        error_action_t     action,
	bool	*succp);

static  int getCapabilities ();
static int blr_handler_config(void *userdata, const char *section, const char *name, const char *value);
static int blr_handle_config_item(const char *name, const char *value, ROUTER_INSTANCE *inst);
static int blr_set_service_mysql_user(SERVICE *service);
int blr_load_dbusers(ROUTER_INSTANCE *router);
int blr_save_dbusers(ROUTER_INSTANCE *router);
static int blr_check_binlog(ROUTER_INSTANCE *router);
extern void blr_cache_read_master_data(ROUTER_INSTANCE *router);
extern char *decryptPassword(char *crypt);
extern char *create_hex_sha1_sha1_passwd(char *passwd);
extern int blr_read_events_all_events(ROUTER_INSTANCE *router, int fix, int debug);
void blr_master_close(ROUTER_INSTANCE *);
char * blr_last_event_description(ROUTER_INSTANCE *router);
extern int MaxScaleUptime();
char	*blr_get_event_description(ROUTER_INSTANCE *router, uint8_t event);

/** The module object definition */
static ROUTER_OBJECT MyObject = {
    createInstance,
    newSession,
    closeSession,
    freeSession,
    routeQuery,
    diagnostics,
    clientReply,
    errorReply,
    getCapabilities
};

static void	stats_func(void *);

static bool rses_begin_locked_router_action(ROUTER_SLAVE *);
static void rses_end_locked_router_action(ROUTER_SLAVE *);
void my_uuid_init(ulong seed1, ulong seed2);
void my_uuid(unsigned char *guid);
GWBUF *blr_cache_read_response(ROUTER_INSTANCE *router, char *response);

static SPINLOCK	instlock;
static ROUTER_INSTANCE *instances;

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
        MXS_NOTICE("Initialise binlog router module %s.\n", version_str);
        spinlock_init(&instlock);
	instances = NULL;
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
ROUTER_OBJECT *
GetModuleObject()
{
	return &MyObject;
}

/**
 * Create an instance of the router for a particular service
 * within MaxScale.
 *
 * The process of creating the instance causes the router to register
 * with the master server and begin replication of the binlogs from
 * the master server to MaxScale.
 * 
 * @param service	The service this router is being create for
 * @param options	An array of options for this query router
 *
 * @return The instance data for this new instance
 */
static	ROUTER	*
createInstance(SERVICE *service, char **options)
{
ROUTER_INSTANCE	*inst;
char		*value;
int		i;
unsigned char	*defuuid;
char		path[PATH_MAX+1] = "";
char		filename[PATH_MAX+1] = "";
int		rc = 0;
char		task_name[BLRM_TASK_NAME_LEN+1] = "";

	if(service->credentials.name == NULL ||
	   service->credentials.authdata == NULL)
	{
	    MXS_ERROR("%s: Error: Service is missing user credentials."
                      " Add the missing username or passwd parameter to the service.",
                      service->name);
	    return NULL;
	}

	if(options == NULL || options[0] == NULL)
	{
	    MXS_ERROR("%s: Error: No router options supplied for binlogrouter",
                      service->name);
	    return NULL;
	}

	/*
	 * We only support one server behind this router, since the server is
	 * the master from which we replicate binlog records. Therefore check
	 * that only one server has been defined.
	 */
	if (service->dbref != NULL)
	{
		MXS_WARNING("%s: backend database server is provided by master.ini file "
			    "for use with the binlog router."
			    " Server section is no longer required.",
			    service->name);

		server_free(service->dbref->server);
		free(service->dbref);
		service->dbref = NULL;
	}

	if ((inst = calloc(1, sizeof(ROUTER_INSTANCE))) == NULL) {
		MXS_ERROR("%s: Error: failed to allocate memory for router instance.",
                          service->name);

		return NULL;
	}

	memset(&inst->stats, 0, sizeof(ROUTER_STATS));
	memset(&inst->saved_master, 0, sizeof(MASTER_RESPONSES));

	inst->service = service;
	spinlock_init(&inst->lock);
	inst->files = NULL;
	spinlock_init(&inst->fileslock);
	spinlock_init(&inst->binlog_lock);

	inst->binlog_fd = -1;
	inst->master_chksum = true;
	inst->master_uuid = NULL;

	inst->master_state = BLRM_UNCONFIGURED;
	inst->master = NULL;
	inst->client = NULL;

	inst->low_water = DEF_LOW_WATER;
	inst->high_water = DEF_HIGH_WATER;
	inst->initbinlog = 0;
	inst->short_burst = DEF_SHORT_BURST;
	inst->long_burst = DEF_LONG_BURST;
	inst->burst_size = DEF_BURST_SIZE;
	inst->retry_backoff = 1;
	inst->binlogdir = NULL;
	inst->heartbeat = BLR_HEARTBEAT_DEFAULT_INTERVAL;
	inst->mariadb10_compat = false;

	inst->user = strdup(service->credentials.name);
	inst->password = strdup(service->credentials.authdata);

	inst->m_errno = 0;
	inst->m_errmsg = NULL;

	inst->trx_safe = 1;
	inst->pending_transaction = 0;
	inst->last_safe_pos = 0;

	inst->set_master_version = NULL;
	inst->set_master_hostname = NULL;
	inst->set_master_uuid = NULL;
	inst->set_master_server_id = NULL;
	inst->send_slave_heartbeat = 0;

	inst->serverid = 0;

	my_uuid_init((ulong)rand()*12345,12345);
	if ((defuuid = (unsigned char *)malloc(20)) != NULL)
	{
		my_uuid(defuuid);
		if ((inst->uuid = (char *)malloc(38)) != NULL)
			sprintf(inst->uuid,
			        "%02hhx%02hhx%02hhx%02hhx-"
			        "%02hhx%02hhx-"
			        "%02hhx%02hhx-"
			        "%02hhx%02hhx-"
			        "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
			        defuuid[0], defuuid[1], defuuid[2], defuuid[3],
			        defuuid[4], defuuid[5], defuuid[6], defuuid[7],
			        defuuid[8], defuuid[9], defuuid[10], defuuid[11],
			        defuuid[12], defuuid[13], defuuid[14], defuuid[15]);
	}

	/*
	 * Process the options.
	 * We have an array of attribute values passed to us that we must
	 * examine. Supported attributes are:
	 *	uuid=
	 *	server-id=
	 *	user=
	 *	password=
	 *	master-id=
	 *	filestem=
	 *	lowwater=
	 *	highwater=
	 */
	if (options)
	{
		for (i = 0; options[i]; i++)
		{
			if ((value = strchr(options[i], '=')) == NULL)
			{
                            MXS_WARNING("Unsupported router "
					"option %s for binlog router.",
					options[i]);
			}
			else
			{
				*value = 0;
				value++;
				if (strcmp(options[i], "uuid") == 0)
				{
					inst->uuid = strdup(value);
				}
				else if ( (strcmp(options[i], "server_id") == 0) || (strcmp(options[i], "server-id") == 0) )
				{
					inst->serverid = atoi(value);
					if (strcmp(options[i], "server-id") == 0) {
						MXS_WARNING("Configuration setting '%s' in router_options is deprecated"
                                                            " and will be removed in a later version of MaxScale. "
                                                            "Please use the new setting '%s' instead.",
                                                            "server-id", "server_id");
					}

					if (inst->serverid <= 0) {
						MXS_ERROR("Service %s, invalid server-id '%s'. "
                                                          "Please configure it with a unique positive integer value (1..2^32-1)",
                                                          service->name, value);

						free(inst);
						return NULL;
					}
				}
				else if (strcmp(options[i], "user") == 0)
				{
					inst->user = strdup(value);
				}
				else if (strcmp(options[i], "password") == 0)
				{
					inst->password = strdup(value);
				}
				else if (strcmp(options[i], "passwd") == 0)
				{
					inst->password = strdup(value);
				}
				else if ( (strcmp(options[i], "master_id") == 0) || (strcmp(options[i], "master-id") == 0) )
				{
					int master_id = atoi(value);
					if (master_id > 0) {
						inst->masterid = master_id;
						inst->set_master_server_id = strdup(value);
					}
					if (strcmp(options[i], "master-id") == 0) {
						MXS_WARNING("Configuration setting '%s' in router_options is deprecated"
                                                            " and will be removed in a later version of MaxScale. "
                                                            "Please use the new setting '%s' instead.",
                                                            "master-id", "master_id");
					}
				}
				else if (strcmp(options[i], "master_uuid") == 0)
				{
					inst->set_master_uuid = strdup(value);
					inst->master_uuid = inst->set_master_uuid;
				}
				else if (strcmp(options[i], "master_version") == 0)
				{
					inst->set_master_version = strdup(value);
				}
				else if (strcmp(options[i], "master_hostname") == 0)
				{
					inst->set_master_hostname = strdup(value);
				}
				else if (strcmp(options[i], "mariadb10-compatibility") == 0)
				{
					inst->mariadb10_compat = config_truth_value(value);
				}
				else if (strcmp(options[i], "filestem") == 0)
				{
					inst->fileroot = strdup(value);
				}
				else if (strcmp(options[i], "file") == 0)
				{
					inst->initbinlog = atoi(value);
				}
				else if (strcmp(options[i], "transaction_safety") == 0)
				{
					inst->trx_safe = config_truth_value(value);
				}
				else if (strcmp(options[i], "lowwater") == 0)
				{
					inst->low_water = atoi(value);
				}
				else if (strcmp(options[i], "highwater") == 0)
				{
					inst->high_water = atoi(value);
				}
				else if (strcmp(options[i], "shortburst") == 0)
				{
					inst->short_burst = atoi(value);
				}
				else if (strcmp(options[i], "longburst") == 0)
				{
					inst->long_burst = atoi(value);
				}
				else if (strcmp(options[i], "burstsize") == 0)
				{
					unsigned long size = atoi(value);
					char	*ptr = value;
					while (*ptr && isdigit(*ptr))
						ptr++;
					switch (*ptr)
					{
					case 'G':
					case 'g':
						size = size * 1024 * 1000 * 1000;
						break;
					case 'M':
					case 'm':
						size = size * 1024 * 1000;
						break;
					case 'K':
					case 'k':
						size = size * 1024;
						break;
					}
					inst->burst_size = size;
					
				}
				else if (strcmp(options[i], "heartbeat") == 0)
				{
					int h_val = (int)strtol(value, NULL, 10);

					if (h_val <= 0 || (errno == ERANGE)) {
						MXS_WARNING("Invalid heartbeat period %s."
                                                            " Setting it to default value %ld.",
                                                            value, inst->heartbeat);
					} else {
						inst->heartbeat = h_val;
					}
				}
				else if (strcmp(options[i], "send_slave_heartbeat") == 0)
				{
					inst->send_slave_heartbeat = config_truth_value(value);
				}
				else if (strcmp(options[i], "binlogdir") == 0)
				{
					inst->binlogdir = strdup(value);
				}
				else
				{
					MXS_WARNING("Unsupported router "
                                                    "option %s for binlog router.",
                                                    options[i]);
				}
			}
		}
	}
	else
	{
		MXS_ERROR("%s: Error: No router options supplied for binlogrouter",
                          service->name);
	}

	if (inst->fileroot == NULL)
		inst->fileroot = strdup(BINLOG_NAME_ROOT);
	inst->active_logs = 0;
	inst->reconnect_pending = 0;
	inst->handling_threads = 0;
	inst->rotating = 0;
	inst->residual = NULL;
	inst->slaves = NULL;
	inst->next = NULL;
	inst->lastEventTimestamp = 0;

	inst->binlog_position = 0;
	inst->current_pos = 0;
	inst->current_safe_event = 0;

	strcpy(inst->binlog_name, "");
	strcpy(inst->prevbinlog, "");

	if ((inst->binlogdir == NULL) || (inst->binlogdir != NULL && !strlen(inst->binlogdir))) {
		MXS_ERROR("Service %s, binlog directory is not specified",
                          service->name);
		free(inst);
		return NULL;
	}

	if (inst->serverid <= 0) {
		MXS_ERROR("Service %s, server-id is not configured. "
                          "Please configure it with a unique positive integer value (1..2^32-1)",
                          service->name);
		free(inst);
		return NULL;
	}

	/**
	 * If binlogdir is not found create it
	 * On failure don't start the instance
	 */
	if (access(inst->binlogdir, R_OK) == -1) {
		int mkdir_rval;
		mkdir_rval = mkdir(inst->binlogdir, 0700);
		if (mkdir_rval == -1) {
			char err_msg[STRERROR_BUFLEN];
			MXS_ERROR("Service %s, Failed to create binlog directory '%s': [%d] %s",
                                  service->name,
                                  inst->binlogdir,
                                  errno,
                                  strerror_r(errno, err_msg, sizeof(err_msg)));

			free(inst);
			return NULL;
		}
	}

	/* Allocate dbusers for this router here instead of serviceStartPort() */
	if (service->users == NULL) {
		service->users = (void *)mysql_users_alloc();
		if (service->users == NULL) {
			MXS_ERROR("%s: Error allocating dbusers in createInstance",
                                  inst->service->name);

			free(inst);
			return NULL;
		}
	}

	/* Dynamically allocate master_host server struct, not written in anyfile */
	if (service->dbref == NULL) {
		SERVICE *service = inst->service;
		SERVER *server;
		server = server_alloc("_none_", "MySQLBackend", (int)3306);
		if (server == NULL) {
			MXS_ERROR("%s: Error for server_alloc in createInstance",
                                  inst->service->name);
			if (service->users) {
				users_free(service->users);
                service->users = NULL;
			}

			free(inst);
			return NULL;
		}
		server_set_unique_name(server, "binlog_router_master_host");
		serviceAddBackend(service, server);
	}

	/*
	 * Check for master.ini file with master connection details
	 * If not found a CHANGE MASTER TO is required via mysqsl client.
	 * Use START SLAVE for replication startup.
	 *
	 * If existent master.ini will be used for
	 * automatic master replication start phase
	 */

	strncpy(path, inst->binlogdir, PATH_MAX);
	snprintf(filename,PATH_MAX, "%s/master.ini", path);

	rc = ini_parse(filename, blr_handler_config, inst);

	MXS_INFO("%s: %s parse result is %d",
                 inst->service->name,
                 filename,
                 rc);

	/*
	 * retcode:
	 * -1 file not found, 0 parsing ok, > 0 error parsing the content
	 */

	if (rc != 0) {
		if (rc == -1) {
			MXS_ERROR("%s: master.ini file not found in %s."
                                  " Master registration cannot be started."
                                  " Configure with CHANGE MASTER TO ...",
                                  inst->service->name, inst->binlogdir);
		} else {
			MXS_ERROR("%s: master.ini file with errors in %s."
                                  " Master registration cannot be started."
                                  " Fix errors in it or configure with CHANGE MASTER TO ...",
                                  inst->service->name, inst->binlogdir);
		}
	
		/* Set service user or load db users */
		blr_set_service_mysql_user(inst->service);

	} else {
		inst->master_state = BLRM_UNCONNECTED;

		/* Try loading dbusers */
		blr_load_dbusers(inst);
	}

	/**
	 * Initialise the binlog router 
	 */
	if (inst->master_state == BLRM_UNCONNECTED) {

	 	/* Read any cached response messages */
		blr_cache_read_master_data(inst);

		/* Find latest binlog file or create a new one (000001) */
		if (blr_file_init(inst) == 0)
		{
			MXS_ERROR("%s: Service not started due to lack of binlog directory %s",
                                  service->name,
                                  inst->binlogdir);

			if (service->users) {
				users_free(service->users);
                service->users = NULL;
			}

			if (service->dbref && service->dbref->server) {
				server_free(service->dbref->server);
				free(service->dbref);
			}

			free(inst);
			return NULL;
		}
	}

	/**
	 * We have completed the creation of the instance data, so now
	 * insert this router instance into the linked list of routers
	 * that have been created with this module.
	 */
	spinlock_acquire(&instlock);
	inst->next = instances;
	instances = inst;
	spinlock_release(&instlock);

	/*
	 * Initialise the binlog cache for this router instance
	 */
	blr_init_cache(inst);

	/*
	 * Add tasks for statistic computation
	 */
	snprintf(task_name, BLRM_TASK_NAME_LEN, "%s stats", service->name);
	hktask_add(task_name, stats_func, inst, BLR_STATS_FREQ);

	/* Log whether the transaction safety option value is on*/
	if (inst->trx_safe) {
		MXS_INFO("%s: Service has transaction safety option set to ON",
                         service->name);
	}

	/**
	 * Check whether replication can be started
	 */
	if (inst->master_state == BLRM_UNCONNECTED) {
		/* Check current binlog */
		MXS_NOTICE("Validating binlog file '%s' ...",
                           inst->binlog_name);

		if (inst->trx_safe && !blr_check_binlog(inst)) {
			/* Don't start replication, just return */
			return (ROUTER *)inst;
		}

		if (!inst->trx_safe) {
			MXS_INFO("Current binlog file is %s, current pos is %lu\n",
                                 inst->binlog_name, inst->binlog_position);
		} else {
			MXS_INFO("Current binlog file is %s, safe pos %lu, current pos is %lu\n",
                                 inst->binlog_name, inst->binlog_position, inst->current_pos);
		}

		/* Start replication from master server */
		blr_start_master(inst);
	}

	return (ROUTER *)inst;
}

/**
 * Associate a new session with this instance of the router.
 *
 * In the case of the binlog router a new session equates to a new slave
 * connecting to MaxScale and requesting binlog records. We need to go
 * through the slave registration process for this new slave.
 *
 * @param instance	The router instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(ROUTER *instance, SESSION *session)
{
ROUTER_INSTANCE		*inst = (ROUTER_INSTANCE *)instance;
ROUTER_SLAVE		*slave;

        MXS_DEBUG("binlog router: %lu [newSession] new router session with "
                  "session %p, and inst %p.",
                  pthread_self(),
                  session,
                  inst);


	if ((slave = (ROUTER_SLAVE *)calloc(1, sizeof(ROUTER_SLAVE))) == NULL)
	{
		MXS_ERROR("Insufficient memory to create new slave session for binlog router");
                return NULL;
	}

#if defined(SS_DEBUG)
        slave->rses_chk_top = CHK_NUM_ROUTER_SES;
        slave->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif

	memset(&slave->stats, 0, sizeof(SLAVE_STATS));
	atomic_add(&inst->stats.n_slaves, 1);
	slave->state = BLRS_CREATED;		/* Set initial state of the slave */
	slave->cstate = 0;
	slave->pthread = 0;
	slave->overrun = 0;
	slave->uuid = NULL;
	slave->hostname = NULL;
        spinlock_init(&slave->catch_lock);
	slave->dcb = session->client;
	slave->router = inst;
	slave->file = NULL;
	strcpy(slave->binlogfile, "unassigned");
	slave->connect_time = time(0);
	slave->lastEventTimestamp = 0;
	slave->mariadb10_compat = false;
	slave->heartbeat = 0;
	slave->lastEventReceived = 0;

	/**
         * Add this session to the list of active sessions.
         */
	spinlock_acquire(&inst->lock);
	slave->next = inst->slaves;
	inst->slaves = slave;
	spinlock_release(&inst->lock);

        CHK_CLIENT_RSES(slave);
                
	return (void *)slave;
}

/**
 * The session is no longer required. Shutdown all operation and free memory
 * associated with this session. In this case a single session is associated
 * to a slave of MaxScale. Therefore this is called when that slave is no
 * longer active and should remove of reference to that slave, free memory
 * and prevent any further forwarding of binlog records to that slave.
 *
 * Parameters:
 * @param router_instance	The instance of the router
 * @param router_cli_ses 	The particular session to free
 *
 */
static void freeSession(
        ROUTER* router_instance,
        void*   router_client_ses)
{
ROUTER_INSTANCE 	*router = (ROUTER_INSTANCE *)router_instance;
ROUTER_SLAVE		*slave = (ROUTER_SLAVE *)router_client_ses;
int			prev_val;
        
        prev_val = atomic_add(&router->stats.n_slaves, -1);
        ss_dassert(prev_val > 0);
        
	/*
	 * Remove the slave session form the list of slaves that are using the
	 * router currently.
	 */
	spinlock_acquire(&router->lock);
	if (router->slaves == slave) {
		router->slaves = slave->next;
        } else {
		ROUTER_SLAVE *ptr = router->slaves;
                
		while (ptr != NULL && ptr->next != slave) {
			ptr = ptr->next;
                }
                
		if (ptr != NULL) {
			ptr->next = slave->next;
                }
	}
	spinlock_release(&router->lock);

        MXS_DEBUG("%lu [freeSession] Unlinked router_client_session %p from "
                  "router %p. Connections : %d. ",
                  pthread_self(),
                  slave,
                  router,
                  prev_val-1);

	if (slave->hostname)
		free(slave->hostname);
	if (slave->user)
		free(slave->user);
	if (slave->passwd)
		free(slave->passwd);
        free(slave);
}


/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance		The router instance data
 * @param router_session	The session being closed
 */
static	void 	
closeSession(ROUTER *instance, void *router_session)
{
ROUTER_INSTANCE	 *router = (ROUTER_INSTANCE *)instance;
ROUTER_SLAVE	 *slave = (ROUTER_SLAVE *)router_session;

	if (slave == NULL)
	{
		/*
		 * We must be closing the master session.
		 */
		MXS_NOTICE("%s: Master %s disconnected after %ld seconds. "
                           "%lu events read,",
                           router->service->name, router->service->dbref->server->name,
                           time(0) - router->connect_time, router->stats.n_binlogs_ses);
        	MXS_ERROR("Binlog router close session with master server %s",
                          router->service->dbref->server->unique_name);
		blr_master_reconnect(router);
		return;
	}
        CHK_CLIENT_RSES(slave);

        /**
         * Lock router client session for secure read and update.
         */
        if (rses_begin_locked_router_action(slave))
        {
		/* decrease server registered slaves counter */
		atomic_add(&router->stats.n_registered, -1);

		if (slave->state > 0) {
			MXS_NOTICE("%s: Slave %s:%d, server id %d, disconnected after %ld seconds. "
                                 "%d SQL commands, %d events sent (%lu bytes), binlog '%s', "
                                   "last position %lu",
                                   router->service->name, slave->dcb->remote, ntohs((slave->dcb->ipv4).sin_port),
                                   slave->serverid,
                                   time(0) - slave->connect_time,
                                   slave->stats.n_queries,
                                   slave->stats.n_events,
                                   slave->stats.n_bytes,
                                   slave->binlogfile,
                                   (unsigned long)slave->binlog_pos);
		} else {
			MXS_NOTICE("%s: Slave %s, server id %d, disconnected after %ld seconds. "
                                   "%d SQL commands",
                                   router->service->name, slave->dcb->remote,
                                   slave->serverid,
                                   time(0) - slave->connect_time,
                                   slave->stats.n_queries);
		}

		/*
		 * Mark the slave as unregistered to prevent the forwarding
		 * of any more binlog records to this slave.
		 */
		slave->state = BLRS_UNREGISTERED;

		if (slave->file)
			blr_close_binlog(router, slave->file);

                /* Unlock */
                rses_end_locked_router_action(slave);
        }
}

/**
 * We have data from the client, this is likely to be packets related to
 * the registration of the slave to receive binlog records. Unlike most
 * MaxScale routers there is no forwarding to the backend database, merely
 * the return of either predefined server responses that have been cached
 * or binlog records.
 *
 * @param instance		The router instance
 * @param router_session	The router session returned from the newSession call
 * @param queue			The queue of data buffers to route
 * @return The number of bytes sent
 */
static	int	
routeQuery(ROUTER *instance, void *router_session, GWBUF *queue)
{
ROUTER_INSTANCE	*router = (ROUTER_INSTANCE *)instance;
ROUTER_SLAVE	 *slave = (ROUTER_SLAVE *)router_session;
       
	return blr_slave_request(router, slave, queue);
}

static char *event_names[] = {
	"Invalid", "Start Event V3", "Query Event", "Stop Event", "Rotate Event",
	"Integer Session Variable", "Load Event", "Slave Event", "Create File Event",
	"Append Block Event", "Exec Load Event", "Delete File Event",
	"New Load Event", "Rand Event", "User Variable Event", "Format Description Event",
	"Transaction ID Event (2 Phase Commit)", "Begin Load Query Event",
	"Execute Load Query Event", "Table Map Event", "Write Rows Event (v0)",
	"Update Rows Event (v0)", "Delete Rows Event (v0)", "Write Rows Event (v1)",
	"Update Rows Event (v1)", "Delete Rows Event (v1)", "Incident Event",
	"Heartbeat Event", "Ignorable Event", "Rows Query Event", "Write Rows Event (v2)",
	"Update Rows Event (v2)", "Delete Rows Event (v2)", "GTID Event",
	"Anonymous GTID Event", "Previous GTIDS Event"
};

/* New MariaDB event numbers starts from 0xa0 */
static char *event_names_mariadb10[] = {
	"Annotate Rows Event",
	/* New MariaDB 10.x event numbers */
	"Binlog Checkpoint Event",
	"GTID Event",
	"GTID List Event"
};

/**
 * Display an entry from the spinlock statistics data
 *
 * @param	dcb	The DCB to print to
 * @param	desc	Description of the statistic
 * @param	value	The statistic value
 */
static void
spin_reporter(void *dcb, char *desc, int value)
{
	dcb_printf((DCB *)dcb, "\t\t%-35s	%d\n", desc, value);
}

/**
 * Display router diagnostics
 *
 * @param instance	Instance of the router
 * @param dcb		DCB to send diagnostics to
 */
static	void
diagnostics(ROUTER *router, DCB *dcb)
{
ROUTER_INSTANCE	*router_inst = (ROUTER_INSTANCE *)router;
ROUTER_SLAVE	*session;
int		i = 0, j;
int		minno = 0;
double		min5, min10, min15, min30;
char		buf[40];
struct tm	tm;

	spinlock_acquire(&router_inst->lock);
	session = router_inst->slaves;
	while (session)
	{
		i++;
		session = session->next;
	}
	spinlock_release(&router_inst->lock);

	minno = router_inst->stats.minno;
	min30 = 0.0;
	min15 = 0.0;
	min10 = 0.0;
	min5 = 0.0;
	for (j = 0; j < 30; j++)
	{
		minno--;
		if (minno < 0)
			minno += 30;
		min30 += router_inst->stats.minavgs[minno];
		if (j < 15)
			min15 += router_inst->stats.minavgs[minno];
		if (j < 10)
			min10 += router_inst->stats.minavgs[minno];
		if (j < 5)
			min5 += router_inst->stats.minavgs[minno];
	}
	min30 /= 30.0;
	min15 /= 15.0;
	min10 /= 10.0;
	min5 /= 5.0;
	
	if (router_inst->master)
		dcb_printf(dcb, "\tMaster connection DCB:  			%p\n",
			router_inst->master);
	else
		dcb_printf(dcb, "\tMaster connection DCB: 			0x0\n");

	dcb_printf(dcb, "\tMaster connection state:			%s\n",
			blrm_states[router_inst->master_state]);

	localtime_r(&router_inst->stats.lastReply, &tm);
	asctime_r(&tm, buf);
	
	dcb_printf(dcb, "\tBinlog directory:				%s\n",
		   router_inst->binlogdir);
	dcb_printf(dcb, "\tHeartbeat period (seconds):			%lu\n",
		   router_inst->heartbeat);
	dcb_printf(dcb, "\tNumber of master connects:	  		%d\n",
                   router_inst->stats.n_masterstarts);
	dcb_printf(dcb, "\tNumber of delayed reconnects:      		%d\n",
                   router_inst->stats.n_delayedreconnects);
	dcb_printf(dcb, "\tCurrent binlog file:		  		%s\n",
                   router_inst->binlog_name);
	dcb_printf(dcb, "\tCurrent binlog position:	  		%lu\n",
                   router_inst->current_pos);
	if (router_inst->trx_safe) {
		if (router_inst->pending_transaction) {	
			dcb_printf(dcb, "\tCurrent open transaction pos:	  		%lu\n",
	                   router_inst->binlog_position);
		}
	}
	dcb_printf(dcb, "\tNumber of slave servers:	   		%u\n",
                   router_inst->stats.n_slaves);
	dcb_printf(dcb, "\tNo. of binlog events received this session:	%u\n",
                   router_inst->stats.n_binlogs_ses);
	dcb_printf(dcb, "\tTotal no. of binlog events received:        	%u\n",
                   router_inst->stats.n_binlogs);
	dcb_printf(dcb, "\tNo. of bad CRC received from master:        	%u\n",
                   router_inst->stats.n_badcrc);
	minno = router_inst->stats.minno - 1;
	if (minno == -1)
		minno = 30;
	dcb_printf(dcb, "\tNumber of binlog events per minute\n");
	dcb_printf(dcb, "\tCurrent        5        10       15       30 Min Avg\n");
	dcb_printf(dcb, "\t %6d  %8.1f %8.1f %8.1f %8.1f\n",
		   router_inst->stats.minavgs[minno], min5, min10, min15, min30);
	dcb_printf(dcb, "\tNumber of fake binlog events:      		%u\n",
                   router_inst->stats.n_fakeevents);
	dcb_printf(dcb, "\tNumber of artificial binlog events: 		%u\n",
                   router_inst->stats.n_artificial);
	dcb_printf(dcb, "\tNumber of binlog events in error:  		%u\n",
                   router_inst->stats.n_binlog_errors);
	dcb_printf(dcb, "\tNumber of binlog rotate events:  		%u\n",
                   router_inst->stats.n_rotates);
	dcb_printf(dcb, "\tNumber of heartbeat events:     		%u\n",
                   router_inst->stats.n_heartbeats);
	dcb_printf(dcb, "\tNumber of packets received:			%u\n",
		   router_inst->stats.n_reads);
	dcb_printf(dcb, "\tNumber of residual data packets:		%u\n",
		   router_inst->stats.n_residuals);
	dcb_printf(dcb, "\tAverage events per packet:			%.1f\n",
		   router_inst->stats.n_reads != 0 ? ((double)router_inst->stats.n_binlogs / router_inst->stats.n_reads) : 0);

	spinlock_acquire(&router_inst->lock);
	if (router_inst->stats.lastReply) {
		if (buf[strlen(buf)-1] == '\n') {
			buf[strlen(buf)-1] = '\0';
		}
		dcb_printf(dcb, "\tLast event from master at:  			%s (%d seconds ago)\n",
			buf, time(0) - router_inst->stats.lastReply);

		if (!router_inst->mariadb10_compat) {
			dcb_printf(dcb, "\tLast event from master:  			0x%x, %s\n",
				router_inst->lastEventReceived,
				(router_inst->lastEventReceived >= 0 && 
				router_inst->lastEventReceived <= MAX_EVENT_TYPE) ?
				event_names[router_inst->lastEventReceived] : "unknown");
		} else {
			char *ptr = NULL;
			if (router_inst->lastEventReceived >= 0 && router_inst->lastEventReceived <= MAX_EVENT_TYPE) {
				ptr = event_names[router_inst->lastEventReceived];
			} else {
				/* Check MariaDB 10 new events */
				if (router_inst->lastEventReceived >= MARIADB_NEW_EVENTS_BEGIN && router_inst->lastEventReceived <= MAX_EVENT_TYPE_MARIADB10) {
					ptr = event_names_mariadb10[(router_inst->lastEventReceived - MARIADB_NEW_EVENTS_BEGIN)];
				}
			}

			dcb_printf(dcb, "\tLast event from master:  			0x%x, %s\n",
				router_inst->lastEventReceived, (ptr != NULL) ? ptr : "unknown");
		}

		if (router_inst->lastEventTimestamp) {
			time_t	last_event = (time_t)router_inst->lastEventTimestamp;
			localtime_r(&last_event, &tm);
			asctime_r(&tm, buf);
			if (buf[strlen(buf)-1] == '\n') {
				buf[strlen(buf)-1] = '\0';
			}
			dcb_printf(dcb, "\tLast binlog event timestamp:  			%ld (%s)\n",
				router_inst->lastEventTimestamp, buf);
		}
	} else {
		dcb_printf(dcb, "\tNo events received from master yet\n");
	}
	spinlock_release(&router_inst->lock);

	if (router_inst->active_logs)
		dcb_printf(dcb, "\tRouter processing binlog records\n");
	if (router_inst->reconnect_pending)
		dcb_printf(dcb, "\tRouter pending reconnect to master\n");
	dcb_printf(dcb, "\tEvents received:\n");
	for (i = 0; i <= MAX_EVENT_TYPE; i++)
	{
		dcb_printf(dcb, "\t\t%-38s   %u\n", event_names[i], router_inst->stats.events[i]);
	}

	if (router_inst->mariadb10_compat) {
		/* Display MariaDB 10 new events */
		for (i = MARIADB_NEW_EVENTS_BEGIN; i <= MAX_EVENT_TYPE_MARIADB10; i++)
			dcb_printf(dcb, "\t\tMariaDB 10 %-38s   %u\n", event_names_mariadb10[(i - MARIADB_NEW_EVENTS_BEGIN)], router_inst->stats.events[i]);
	}

#if SPINLOCK_PROFILE
	dcb_printf(dcb, "\tSpinlock statistics (instlock):\n");
	spinlock_stats(&instlock, spin_reporter, dcb);
	dcb_printf(dcb, "\tSpinlock statistics (instance lock):\n");
	spinlock_stats(&router_inst->lock, spin_reporter, dcb);
	dcb_printf(dcb, "\tSpinlock statistics (binlog position lock):\n");
	spinlock_stats(&router_inst->binlog_lock, spin_reporter, dcb);
#endif

	if (router_inst->slaves)
	{
		dcb_printf(dcb, "\tSlaves:\n");
		spinlock_acquire(&router_inst->lock);
		session = router_inst->slaves;
		while (session)
		{

			minno = session->stats.minno;
			min30 = 0.0;
			min15 = 0.0;
			min10 = 0.0;
			min5 = 0.0;
			for (j = 0; j < 30; j++)
			{
				minno--;
				if (minno < 0)
					minno += 30;
				min30 += session->stats.minavgs[minno];
				if (j < 15)
					min15 += session->stats.minavgs[minno];
				if (j < 10)
					min10 += session->stats.minavgs[minno];
				if (j < 5)
					min5 += session->stats.minavgs[minno];
			}
			min30 /= 30.0;
			min15 /= 15.0;
			min10 /= 10.0;
			min5 /= 5.0;
			dcb_printf(dcb,
				"\t\tServer-id:					%d\n",
						 session->serverid);
			if (session->hostname)
				dcb_printf(dcb, "\t\tHostname:					%s\n", session->hostname);
			if (session->uuid)
				dcb_printf(dcb, "\t\tSlave UUID:					%s\n", session->uuid);
			dcb_printf(dcb,
				"\t\tSlave_host_port:				%s:%d\n",
						 session->dcb->remote, ntohs((session->dcb->ipv4).sin_port));
			dcb_printf(dcb,
				"\t\tUsername:					%s\n",
						 session->dcb->user);
			dcb_printf(dcb,
				"\t\tSlave DCB:					%p\n",
						 session->dcb);
			dcb_printf(dcb,
				"\t\tNext Sequence No:				%d\n",
						 session->seqno);
			dcb_printf(dcb,
				"\t\tState:    					%s\n",
						 blrs_states[session->state]);
			dcb_printf(dcb,
				"\t\tBinlog file:					%s\n",
						session->binlogfile);
			dcb_printf(dcb,
				"\t\tBinlog position:				%u\n",
						session->binlog_pos);
			if (session->nocrc)
				dcb_printf(dcb,
					"\t\tMaster Binlog CRC:				None\n");
			dcb_printf(dcb,
				"\t\tNo. requests:   				%u\n",
						session->stats.n_requests);
			dcb_printf(dcb,
					"\t\tNo. events sent:				%u\n",
						session->stats.n_events);
			dcb_printf(dcb,
					"\t\tNo. bytes sent:					%u\n",
						session->stats.n_bytes);
			dcb_printf(dcb,
					"\t\tNo. bursts sent:				%u\n",
						session->stats.n_bursts);
			dcb_printf(dcb,
					"\t\tNo. transitions to follow mode:			%u\n",
						session->stats.n_bursts);
			if (router_inst->send_slave_heartbeat)
				dcb_printf(dcb, "\t\tHeartbeat period (seconds):			%lu\n",
					session->heartbeat);

			minno = session->stats.minno - 1;
			if (minno == -1)
				minno = 30;
			dcb_printf(dcb, "\t\tNumber of binlog events per minute\n");
			dcb_printf(dcb, "\t\tCurrent        5        10       15       30 Min Avg\n");
			dcb_printf(dcb, "\t\t %6d  %8.1f %8.1f %8.1f %8.1f\n",
		   		session->stats.minavgs[minno], min5, min10,
						min15, min30);
			dcb_printf(dcb, "\t\tNo. flow control:				%u\n", session->stats.n_flows);
			dcb_printf(dcb, "\t\tNo. up to date:					%u\n", session->stats.n_upd);
			dcb_printf(dcb, "\t\tNo. of drained cbs 				%u\n", session->stats.n_dcb);
			dcb_printf(dcb, "\t\tNo. of failed reads				%u\n", session->stats.n_failed_read);

#if DETAILED_DIAG
			dcb_printf(dcb, "\t\tNo. of nested distribute events			%u\n", session->stats.n_overrun);
			dcb_printf(dcb, "\t\tNo. of distribute action 1			%u\n", session->stats.n_actions[0]);
			dcb_printf(dcb, "\t\tNo. of distribute action 2			%u\n", session->stats.n_actions[1]);
			dcb_printf(dcb, "\t\tNo. of distribute action 3			%u\n", session->stats.n_actions[2]);
#endif
			if (session->lastEventTimestamp
					&& router_inst->lastEventTimestamp && session->lastEventReceived != HEARTBEAT_EVENT)
			{
				unsigned long seconds_behind;
				time_t	session_last_event = (time_t)session->lastEventTimestamp;

				if (router_inst->lastEventTimestamp > session->lastEventTimestamp)
					seconds_behind  = router_inst->lastEventTimestamp - session->lastEventTimestamp;
				else
					seconds_behind = 0;

				localtime_r(&session_last_event, &tm);
				asctime_r(&tm, buf);
				dcb_printf(dcb, "\t\tLast binlog event timestamp			%u, %s", session->lastEventTimestamp, buf);
				dcb_printf(dcb, "\t\tSeconds behind master				%lu\n", seconds_behind);
			}

			if (session->state == 0)
			{
				dcb_printf(dcb, "\t\tSlave_mode:					connected\n");
			}
			else if ((session->cstate & CS_UPTODATE) == 0)
			{
				dcb_printf(dcb, "\t\tSlave_mode:					catchup. %s%s\n", 
					((session->cstate & CS_EXPECTCB) == 0 ? "" :
					"Waiting for DCB queue to drain."),
					((session->cstate & CS_BUSY) == 0 ? "" :
					" Busy in slave catchup."));
			}
			else
			{
				dcb_printf(dcb, "\t\tSlave_mode:					follow\n");
				if (session->binlog_pos != router_inst->binlog_position)
				{
					dcb_printf(dcb, "\t\tSlave reports up to date however "
					"the slave binlog position does not match the master\n");
				}
			}
#if SPINLOCK_PROFILE
			dcb_printf(dcb, "\tSpinlock statistics (catch_lock):\n");
			spinlock_stats(&session->catch_lock, spin_reporter, dcb);
			dcb_printf(dcb, "\tSpinlock statistics (rses_lock):\n");
			spinlock_stats(&session->rses_lock, spin_reporter, dcb);
#endif
			dcb_printf(dcb, "\t\t--------------------\n\n");
			session = session->next;
		}
		spinlock_release(&router_inst->lock);
	}
}

/**
 * Client Reply routine - in this case this is a message from the
 * master server, It should be sent to the state machine that manages
 * master packets as it may be binlog records or part of the registration
 * handshake that takes part during connection establishment.
 *
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       master_dcb      The DCB for the connection to the master
 * @param       queue           The GWBUF with reply data
 */
static  void
clientReply(ROUTER *instance, void *router_session, GWBUF *queue, DCB *backend_dcb)
{
ROUTER_INSTANCE	*router = (ROUTER_INSTANCE *)instance;

	atomic_add(&router->stats.n_reads, 1);
	blr_master_response(router, queue);
	router->stats.lastReply = time(0);
}

static char *
extract_message(GWBUF *errpkt)
{
char	*rval;
int	len;

	len = EXTRACT24(errpkt->start);
	if ((rval = (char *)malloc(len)) == NULL)
		return NULL;
	memcpy(rval, (char *)(errpkt->start) + 7, 6);
	rval[6] = ' ';
	/* message size is len - (1 byte field count + 2 bytes errno + 6 bytes status) */
	memcpy(&rval[7], (char *)(errpkt->start) + 13, len - 9);
	rval[len-2] = 0;
	return rval;
}



/**
 * Error Reply routine
 *
 * The routine will reply to client errors and/or closing the session
 * or try to open a new backend connection.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       message         The error message to reply
 * @param       backend_dcb     The backend DCB
 * @param       action     	The action: ERRACT_NEW_CONNECTION or ERRACT_REPLY_CLIENT
 * @param	succp		Result of action: true iff router can continue
 *
 */
static  void
errorReply(ROUTER *instance, void *router_session, GWBUF *message, DCB *backend_dcb, error_action_t action, bool *succp)
{
ROUTER_INSTANCE	*router = (ROUTER_INSTANCE *)instance;
int		error;
socklen_t	len;
char		msg[STRERROR_BUFLEN + 1 + 5] = "";
char 		*errmsg;
unsigned long	mysql_errno;

	/** Don't handle same error twice on same DCB */
        if (backend_dcb->dcb_errhandle_called)
	{
		/** we optimistically assume that previous call succeed */
		*succp = true;
		return;
	}
	else
	{
		backend_dcb->dcb_errhandle_called = true;
	}

	len = sizeof(error);
	if (router->master && getsockopt(router->master->fd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 && error != 0)
	{
		char errbuf[STRERROR_BUFLEN];
                sprintf(msg, "%s ", strerror_r(error, errbuf, sizeof(errbuf)));
	}
	else
		strcpy(msg, "");

	mysql_errno = (unsigned long) extract_field((uint8_t *)(GWBUF_DATA(message) + 5), 16);
	errmsg = extract_message(message);

	if (router->master_state < BLRM_BINLOGDUMP || router->master_state != BLRM_SLAVE_STOPPED) {
		/* set mysql_errno */
		router->m_errno = mysql_errno;

		/* set io error message */
		if (router->m_errmsg)
			free(router->m_errmsg);
		router->m_errmsg = strdup(errmsg);

	       	MXS_ERROR("%s: Master connection error %lu '%s' in state '%s', "
                          "%sattempting reconnect to master %s:%d",
                          router->service->name, mysql_errno, errmsg,
                          blrm_states[router->master_state], msg,
                          router->service->dbref->server->name,
                          router->service->dbref->server->port);
	} else {
       		MXS_ERROR("%s: Master connection error %lu '%s' in state '%s', "
                          "%sattempting reconnect to master %s:%d",
                          router->service->name, router->m_errno, router->m_errmsg,
                          blrm_states[router->master_state], msg,
                          router->service->dbref->server->name,
                          router->service->dbref->server->port);
	}

	if (errmsg)
		free(errmsg);
	*succp = true;
        dcb_close(backend_dcb);
	MXS_NOTICE("%s: Master %s disconnected after %ld seconds. "
                   "%lu events read.",
                   router->service->name, router->service->dbref->server->name,
                   time(0) - router->connect_time, router->stats.n_binlogs_ses);
	blr_master_reconnect(router);
}

/** to be inline'd */
/** 
 * @node Acquires lock to router client session if it is not closed.
 *
 * Parameters:
 * @param rses - in, use
 *          
 *
 * @return true if router session was not closed. If return value is true
 * it means that router is locked, and must be unlocked later. False, if
 * router was closed before lock was acquired.
 *
 * 
 * @details (write detailed description here)
 *
 */
static bool rses_begin_locked_router_action(ROUTER_SLAVE *rses)
{
        bool succp = false;
        
        CHK_CLIENT_RSES(rses);

        spinlock_acquire(&rses->rses_lock);
        succp = true;
        
        return succp;
}

/** to be inline'd */
/** 
 * @node Releases router client session lock.
 *
 * Parameters:
 * @param rses - <usage>
 *          <description>
 *
 * @return void
 *
 * 
 * @details (write detailed description here)
 *
 */
static void rses_end_locked_router_action(ROUTER_SLAVE	* rses)
{
        CHK_CLIENT_RSES(rses);
        spinlock_release(&rses->rses_lock);
}


static int getCapabilities()
{
        return RCAP_TYPE_NO_RSESSION;
}

/**
 * The stats gathering function called from the housekeeper so that we
 * can get timed averages of binlog records shippped
 *
 * @param inst	The router instance
 */
static void
stats_func(void *inst)
{
ROUTER_INSTANCE *router = (ROUTER_INSTANCE *)inst;
ROUTER_SLAVE	*slave;

	router->stats.minavgs[router->stats.minno++]
			 = router->stats.n_binlogs - router->stats.lastsample;
	router->stats.lastsample = router->stats.n_binlogs;
	if (router->stats.minno == BLR_NSTATS_MINUTES)
		router->stats.minno = 0;

	spinlock_acquire(&router->lock);
	slave = router->slaves;
	while (slave)
	{
		slave->stats.minavgs[slave->stats.minno++]
			 = slave->stats.n_events - slave->stats.lastsample;
		slave->stats.lastsample = slave->stats.n_events;
		if (slave->stats.minno == BLR_NSTATS_MINUTES)
			slave->stats.minno = 0;
		slave = slave->next;
	}
	spinlock_release(&router->lock);
}

/**
 * Return some basic statistics from the router in response to a COM_STATISTICS
 * request.
 *
 * @param router	The router instance
 * @param slave		The "slave" connection that requested the statistics
 * @param queue		The statistics request
 *
 * @return non-zero on sucessful send
 */
int
blr_statistics(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *queue)
{
char	result[BLRM_COM_STATISTICS_SIZE + 1] = "";
char	*ptr;
GWBUF	*ret;
unsigned long	len;

	snprintf(result, BLRM_COM_STATISTICS_SIZE,
		"Uptime: %u  Threads: %u  Events: %u  Slaves: %u  Master State: %s",
			(unsigned int)(time(0) - router->connect_time),
			(unsigned int)config_threadcount(),
			(unsigned int)router->stats.n_binlogs_ses,
			(unsigned int)router->stats.n_slaves,
			blrm_states[router->master_state]);
	if ((ret = gwbuf_alloc(4 + strlen(result))) == NULL)
		return 0;
	len = strlen(result);
	ptr = GWBUF_DATA(ret);
	*ptr++ = len & 0xff;
	*ptr++ = (len & 0xff00) >> 8;
	*ptr++ = (len & 0xff0000) >> 16;
	*ptr++ = 1;
	strncpy(ptr, result, len);

	return slave->dcb->func.write(slave->dcb, ret);
}

/**
 * Respond to a COM_PING command
 *
 * @param router	The router instance
 * @param slave		The "slave" connection that requested the ping
 * @param queue		The ping request
 */
int
blr_ping(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *queue)
{
char	*ptr;
GWBUF	*ret;

	if ((ret = gwbuf_alloc(5)) == NULL)
		return 0;
	ptr = GWBUF_DATA(ret);
	*ptr++ = 0x01;
	*ptr++ = 0;
	*ptr++ = 0;
	*ptr++ = 1;
	*ptr = 0;		// OK 

	return slave->dcb->func.write(slave->dcb, ret);
}



/**
 * mysql_send_custom_error
 *
 * Send a MySQL protocol Generic ERR message, to the dcb
 *
 * @param dcb Owner_Dcb Control Block for the connection to which the error message is sent
 * @param packet_number
 * @param in_affected_rows
 * @param msg		Message to send
 * @param statemsg	MySQL State message
 * @param errcode	MySQL Error code
 * @return 1 Non-zero if data was sent
 *
 */
int
blr_send_custom_error(DCB *dcb, int packet_number, int affected_rows, char *msg, char *statemsg, unsigned int errcode) 
{
uint8_t		*outbuf = NULL;
uint32_t	mysql_payload_size = 0;
uint8_t		mysql_packet_header[4];
uint8_t		*mysql_payload = NULL;
uint8_t		field_count = 0;
uint8_t		mysql_err[2];
uint8_t		mysql_statemsg[6];
unsigned int	mysql_errno = 0;
const char	*mysql_error_msg = NULL;
const char	*mysql_state = NULL;
GWBUF		*errbuf = NULL;
       
	if (errcode == 0) 
		mysql_errno = 1064;
	else
		mysql_errno = errcode;

	mysql_error_msg = "An errorr occurred ...";
	if (statemsg == NULL)
        	mysql_state = "42000";
	else
		mysql_state = statemsg;
        
        field_count = 0xff;
        gw_mysql_set_byte2(mysql_err, mysql_errno);
        mysql_statemsg[0]='#';
        memcpy(mysql_statemsg+1, mysql_state, 5);
        
        if (msg != NULL) {
                mysql_error_msg = msg;
        }
        
        mysql_payload_size = sizeof(field_count) + 
                                sizeof(mysql_err) + 
                                sizeof(mysql_statemsg) + 
                                strlen(mysql_error_msg);
        
        /** allocate memory for packet header + payload */
        errbuf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
        ss_dassert(errbuf != NULL);
        
        if (errbuf == NULL)
        {
                return 0;
        }
        outbuf = GWBUF_DATA(errbuf);
        
        /** write packet header and packet number */
        gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
        mysql_packet_header[3] = packet_number;
        
        /** write header */
        memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));
        
        mysql_payload = outbuf + sizeof(mysql_packet_header);
        
        /** write field */
        memcpy(mysql_payload, &field_count, sizeof(field_count));
        mysql_payload = mysql_payload + sizeof(field_count);
        
        /** write errno */
        memcpy(mysql_payload, mysql_err, sizeof(mysql_err));
        mysql_payload = mysql_payload + sizeof(mysql_err);
        
        /** write sqlstate */
        memcpy(mysql_payload, mysql_statemsg, sizeof(mysql_statemsg));
        mysql_payload = mysql_payload + sizeof(mysql_statemsg);
        
        /** write error message */
        memcpy(mysql_payload, mysql_error_msg, strlen(mysql_error_msg));

        return dcb->func.write(dcb, errbuf);
}

/**
 * Config item handler for the ini file reader
 *
 * @param userdata      The config context element
 * @param section       The config file section
 * @param name          The Parameter name
 * @param value         The Parameter value
 * @return zero on error
 */

static int
blr_handler_config(void *userdata, const char *section, const char *name, const char *value)
{
ROUTER_INSTANCE *inst = (ROUTER_INSTANCE *) userdata;
SERVICE         *service;

	service = inst->service;

	if (strcasecmp(section, "binlog_configuration") == 0)
	{
		return blr_handle_config_item(name, value, inst);
	} else  {
		MXS_ERROR("master.ini has an invalid section [%s], it should be [binlog_configuration]. "
                          "Service %s",
                          section,
                          service->name);

		return 0;
	}
}

/**
 * Configuration handler for items in the [binlog_configuration] section
 *
 * @param name	The item name
 * @param value	The item value
 * @param inst	The current router instance
 * @return 0 on error
 */
static  int
blr_handle_config_item(const char *name, const char *value, ROUTER_INSTANCE *inst)
{
SERVICE         *service;

        service = inst->service;

        if (strcmp(name, "master_host") == 0) {
                 server_update_address(service->dbref->server, (char *)value);
        } else if (strcmp(name, "master_port") == 0) {
                server_update_port(service->dbref->server, (short)atoi(value));
        } else if (strcmp(name, "filestem") == 0) {
                        free(inst->fileroot);
                inst->fileroot = strdup(value);
        }  else if (strcmp(name, "master_user") == 0) {
                if (inst->user)
                        free(inst->user);
                inst->user = strdup(value);
        } else if (strcmp(name, "master_password") == 0) {
                if (inst->password)
                        free(inst->password);
                inst->password = strdup(value);
        } else {
                return 0;
        }

        return 1;
}

/**
 * Add the service user to mysql dbusers (service->users)
 * via mysql_users_alloc and add_mysql_users_with_host_ipv4
 * User is added for '%' and 'localhost' hosts
 *
 * @param service	The current service
 * @return		0 on success, 1 on failure
 */
static int
blr_set_service_mysql_user(SERVICE *service) {
char	*dpwd = NULL;
char	*newpasswd = NULL;
char	*service_user = NULL;
char	*service_passwd = NULL;

	if (serviceGetUser(service, &service_user, &service_passwd) == 0) {
		MXS_ERROR("failed to get service user details for service %s",
                          service->name);

		return 1;
	}

	dpwd = decryptPassword(service->credentials.authdata);

	if (!dpwd) {
		MXS_ERROR("decrypt password failed for service user %s, service %s",
                          service_user,
                          service->name);

		return 1;
        }

	newpasswd = create_hex_sha1_sha1_passwd(dpwd);

	if (!newpasswd) {
		MXS_ERROR("create hex_sha1_sha1_password failed for service user %s",
                          service_user);

		free(dpwd);
		return 1;
	}

	/* add service user for % and localhost */
	(void)add_mysql_users_with_host_ipv4(service->users, service->credentials.name, "%", newpasswd, "Y", "");
	(void)add_mysql_users_with_host_ipv4(service->users, service->credentials.name, "localhost", newpasswd, "Y", "");

	free(newpasswd);
	free(dpwd);

	return 0;
}

/**
 * Load mysql dbusers into (service->users)
 *
 * @param router	The router instance
 * @return              -1 on failure, 0 for no users found, > 0 for found users
 */
int
blr_load_dbusers(ROUTER_INSTANCE *router)
{
int loaded = -1;
char	path[PATH_MAX+1] = "";
SERVICE *service;
	service = router->service;

	/* File path for router cached authentication data */
	strncpy(path, router->binlogdir, PATH_MAX);
	strncat(path, "/cache", PATH_MAX);
	strncat(path, "/dbusers", PATH_MAX);

	/* Try loading dbusers from configured backends */
	loaded = load_mysql_users(service);

	if (loaded < 0)
	{
		MXS_ERROR("Unable to load users for service %s",
                          service->name);

		/* Try loading authentication data from file cache */

		loaded = dbusers_load(router->service->users, path);

		if (loaded != -1)
		{
			MXS_ERROR("Service %s, Using cached credential information file %s.",
                                  service->name,
                                  path);
		} else {
			MXS_ERROR("Service %s, Unable to read cache credential information from %s."
                                  " No database user added to service users table.",
                                  service->name,
                                  path);
		}
	} else {
		/* don't update cache if no user was loaded */
		if (loaded == 0)
		{
			MXS_ERROR("Service %s: failed to load any user "
                                  "information. Authentication will "
                                  "probably fail as a result.",
                                  service->name);
		} else {
			/* update cached data */
			blr_save_dbusers(router);
		}
	}

	/* At service start last update is set to USERS_REFRESH_TIME seconds earlier.
	 * This way MaxScale could try reloading users' just after startup
	 */
	service->rate_limit.last=time(NULL) - USERS_REFRESH_TIME;
	service->rate_limit.nloads=1;

	return loaded;
}

/**
 * Save dbusers to cache file
 *
 * @param router	The router instance
 * @return              -1 on failure, >= 0 on success
 */
int
blr_save_dbusers(ROUTER_INSTANCE *router)
{
SERVICE *service;
char	path[PATH_MAX+1] = "";
int	mkdir_rval = 0;

        service = router->service;

        /* File path for router cached authentication data */
        strncpy(path, router->binlogdir, PATH_MAX);
        strncat(path, "/cache", PATH_MAX);

	/* check and create dir */
	if (access(path, R_OK) == -1)
	{
		mkdir_rval = mkdir(path, 0700);
	}

	if (mkdir_rval == -1)
	{
		char err_msg[STRERROR_BUFLEN];
		MXS_ERROR("Service %s, Failed to create directory '%s': [%d] %s",
                          service->name,
                          path,
                          errno,
                          strerror_r(errno, err_msg, sizeof(err_msg)));

		return -1;
	}

	/* set cache file name */
	strncat(path, "/dbusers", PATH_MAX);

	return dbusers_save(service->users, path);
}

/**
 * Extract a numeric field from a packet of the specified number of bits
 *
 * @param src	The raw packet source
 * @param birs	The number of bits to extract (multiple of 8)
 */
uint32_t
extract_field(uint8_t *src, int bits)
{
uint32_t	rval = 0, shift = 0;

	while (bits > 0)
	{
		rval |= (*src++) << shift;
		shift += 8;
		bits -= 8;
	}
	return rval;
}

/**
 * Check whether current binlog is valid.
 * In case of errors BLR_SLAVE_STOPPED state is set
 * If a partial transaction is found
 * inst->binlog_position is set the pos where it started
 *
 * @param router	The router instance
 * @return		1 on success, 0 on failure
 */
/** 1 is succes, 0 is faulure */
static int blr_check_binlog(ROUTER_INSTANCE *router) {
	int n;

	/** blr_read_events_all() may set master_state
	 * to BLR_SLAVE_STOPPED state in case of found errors.
	 * In such conditions binlog file is NOT truncated and
	 * router state is set to BLR_SLAVE_STOPPED
	 * Last commited pos is set for both router->binlog_position
	 * and router->current_pos.
	 *
	 * If an open transaction is detected at pos XYZ
	 * inst->binlog_position will be set to XYZ while
	 * router->current_pos is the last event found.
	 */

	n = blr_read_events_all_events(router, 0, 0);

	MXS_DEBUG("blr_read_events_all_events() ret = %i\n", n);

	if (n != 0) {
		char msg_err[BINLOG_ERROR_MSG_LEN + 1] = "";
		router->master_state = BLRM_SLAVE_STOPPED;

		snprintf(msg_err, BINLOG_ERROR_MSG_LEN, "Error found in binlog %s. Safe pos is %lu", router->binlog_name, router->binlog_position);
		/* set mysql_errno */
		router->m_errno = 2032;

		/* set io error message */
		router->m_errmsg = strdup(msg_err);

		/* set last_safe_pos */
		router->last_safe_pos = router->binlog_position;

		MXS_ERROR("Error found in binlog file %s. Safe starting pos is %lu",
                          router->binlog_name,
                          router->binlog_position);

		return 0;
	} else {
		return 1;
	}
}


/**
 * Return last event description
 *
 * @param router	The router instance
 * @return		The event description or NULL
 */
char *
blr_last_event_description(ROUTER_INSTANCE *router) {
char *event_desc = NULL;

	if (!router->mariadb10_compat) {
		if (router->lastEventReceived >= 0 &&
			router->lastEventReceived <= MAX_EVENT_TYPE) {
			event_desc = event_names[router->lastEventReceived];
		}
	} else {
		if (router->lastEventReceived >= 0 &&
			router->lastEventReceived <= MAX_EVENT_TYPE) {
			event_desc = event_names[router->lastEventReceived];
		} else {
			/* Check MariaDB 10 new events */
			if (router->lastEventReceived >= MARIADB_NEW_EVENTS_BEGIN &&
				router->lastEventReceived <= MAX_EVENT_TYPE_MARIADB10) {
				event_desc = event_names_mariadb10[(router->lastEventReceived - MARIADB_NEW_EVENTS_BEGIN)];
			}
		}
	}

	return event_desc;
}

/**
 * Return the event description
 *
 * @param router	The router instance
 * @param event		The current event
 * @return		The event description or NULL
 */
char *
blr_get_event_description(ROUTER_INSTANCE *router, uint8_t event) {
char *event_desc = NULL;

	if (!router->mariadb10_compat) {
		if (event >= 0 &&
			event <= MAX_EVENT_TYPE) {
			event_desc = event_names[event];
		}
	} else {
		if (event >= 0 &&
			event <= MAX_EVENT_TYPE) {
			event_desc = event_names[event];
		} else {
			/* Check MariaDB 10 new events */
			if (event >= MARIADB_NEW_EVENTS_BEGIN &&
				event <= MAX_EVENT_TYPE_MARIADB10) {
				event_desc = event_names_mariadb10[(event - MARIADB_NEW_EVENTS_BEGIN)];
			}
		}
	}

	return event_desc;
}

