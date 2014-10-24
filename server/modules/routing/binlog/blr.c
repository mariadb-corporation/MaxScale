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
 * Copyright SkySQL Ab 2014
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
 * Date		Who		Description
 * 02/04/2014	Mark Riddoch		Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <service.h>
#include <server.h>
#include <router.h>
#include <atomic.h>
#include <spinlock.h>
#include <blr.h>
#include <dcb.h>
#include <spinlock.h>
#include <time.h>

#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#include <mysql_client_server_protocol.h>

extern int lm_enabled_logfiles_bitmask;

static char *version_str = "V1.0.6";

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
static  uint8_t getCapabilities (ROUTER* inst, void* router_session);


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

static bool rses_begin_locked_router_action(ROUTER_SLAVE *);
static void rses_end_locked_router_action(ROUTER_SLAVE *);

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
        LOGIF(LM, (skygw_log_write(
                           LOGFILE_MESSAGE,
                           "Initialise binlog router module %s.\n", version_str)));
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

        if ((inst = calloc(1, sizeof(ROUTER_INSTANCE))) == NULL) {
                return NULL;
        }

	memset(&inst->stats, 0, sizeof(ROUTER_STATS));
	memset(&inst->saved_master, 0, sizeof(MASTER_RESPONSES));

	inst->service = service;
	spinlock_init(&inst->lock);

	inst->low_water = DEF_LOW_WATER;
	inst->high_water = DEF_HIGH_WATER;
	inst->initbinlog = 0;

	/*
	 * We only support one server behind this router, since the server is
	 * the master from which we replicate binlog records. Therefore check
	 * that only one server has been defined.
	 *
	 * A later improvement will be to define multiple servers and have the
	 * router use the information that is supplied by the monitor to find
	 * which of these servers is currently the master and replicate from
	 * that server.
	 */
	if (service->databases == NULL || service->databases->nextdb != NULL)
	{
		LOGIF(LE, (skygw_log_write(
			LOGFILE_ERROR,
				"Error : Exactly one database server may be "
				"for use with the binlog router.")));
	}


	/*
	 * Process the options.
	 * We have an array of attrbute values passed to us that we must
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
                            LOGIF(LE, (skygw_log_write(
				LOGFILE_ERROR, "Warning : Unsupported router "
					"option %s for binlog router.",
					options[i])));
			}
			else
			{
				*value = 0;
				value++;
				if (strcmp(options[i], "uuid") == 0)
				{
					inst->uuid = strdup(value);
				}
				else if (strcmp(options[i], "server-id") == 0)
				{
					inst->serverid = atoi(value);
				}
				else if (strcmp(options[i], "user") == 0)
				{
					inst->user = strdup(value);
				}
				else if (strcmp(options[i], "password") == 0)
				{
					inst->password = strdup(value);
				}
				else if (strcmp(options[i], "master-id") == 0)
				{
					inst->masterid = atoi(value);
				}
				else if (strcmp(options[i], "filestem") == 0)
				{
					inst->fileroot = strdup(value);
				}
				else if (strcmp(options[i], "initialfile") == 0)
				{
					inst->initbinlog = atoi(value);
				}
				else if (strcmp(options[i], "lowwater") == 0)
				{
					inst->low_water = atoi(value);
				}
				else if (strcmp(options[i], "highwater") == 0)
				{
					inst->high_water = atoi(value);
				}
				else
				{
					LOGIF(LE, (skygw_log_write(
						LOGFILE_ERROR,
						"Warning : Unsupported router "
						"option %s for binlog router.",
						options[i])));
				}
			}
		}
		if (inst->fileroot == NULL)
			inst->fileroot = strdup(BINLOG_NAME_ROOT);
	}

	/*
	 * We have completed the creation of the instance data, so now
	 * insert this router instance into the linked list of routers
	 * that have been created with this module.
	 */
	spinlock_acquire(&instlock);
	inst->next = instances;
	instances = inst;
	spinlock_release(&instlock);

	inst->active_logs = 0;
	inst->reconnect_pending = 0;
	inst->handling_threads = 0;
	inst->residual = NULL;
	inst->slaves = NULL;
	inst->next = NULL;

	/*
	 * Initialise the binlog file and position
	 */
	blr_file_init(inst);
	LOGIF(LT, (skygw_log_write(
			LOGFILE_TRACE,
			"Binlog router: current binlog file is: %s, current position %u\n",
						inst->binlog_name, inst->binlog_position)));

	/*
	 * Initialise the binlog cache for this router instance
	 */
	blr_init_cache(inst);

	/*
	 * Now start the replication from the master to MaxScale
	 */
	blr_start_master(inst);

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

        LOGIF(LD, (skygw_log_write_flush(
                LOGFILE_DEBUG,
                "binlog router: %lu [newSession] new router session with "
                "session %p, and inst %p.",
                pthread_self(),
                session,
                inst)));


	if ((slave = (ROUTER_SLAVE *)calloc(1, sizeof(ROUTER_SLAVE))) == NULL)
	{
		LOGIF(LD, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Insufficient memory to create new slave session for binlog router")));
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
        spinlock_init(&slave->catch_lock);
	slave->dcb = session->client;
	slave->router = inst;

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

        LOGIF(LD, (skygw_log_write_flush(
                LOGFILE_DEBUG,
                "%lu [freeSession] Unlinked router_client_session %p from "
                "router %p. Connections : %d. ",
                pthread_self(),
                slave,
                router,
                prev_val-1)));

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
		 *
		 * TODO: Handle closure of master session
		 */
        	LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Binlog router close session with master server %s",
			router->service->databases->unique_name)));
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

		/*
		 * Mark the slave as unregistered to prevent the forwarding
		 * of any more binlog records to this slave.
		 */
		slave->state = BLRS_UNREGISTERED;

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
int		i = 0;
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

	dcb_printf(dcb, "\tMaster connection DCB:  		%p\n",
			router_inst->master);
	dcb_printf(dcb, "\tMaster connection state:		%s\n",
			blrm_states[router_inst->master_state]);

	localtime_r(&router_inst->stats.lastReply, &tm);
	asctime_r(&tm, buf);
	
	dcb_printf(dcb, "\tNumber of master connects:	  	%d\n",
                   router_inst->stats.n_masterstarts);
	dcb_printf(dcb, "\tNumber of delayed reconnects:      	%d\n",
                   router_inst->stats.n_delayedreconnects);
	dcb_printf(dcb, "\tCurrent binlog file:		  	%s\n",
                   router_inst->binlog_name);
	dcb_printf(dcb, "\tCurrent binlog position:	  	%u\n",
                   router_inst->binlog_position);
	dcb_printf(dcb, "\tNumber of slave servers:	   	%u\n",
                   router_inst->stats.n_slaves);
	dcb_printf(dcb, "\tNumber of binlog events received:  	%u\n",
                   router_inst->stats.n_binlogs);
	dcb_printf(dcb, "\tNumber of fake binlog events:      	%u\n",
                   router_inst->stats.n_fakeevents);
	dcb_printf(dcb, "\tNumber of artificial binlog events: 	%u\n",
                   router_inst->stats.n_artificial);
	dcb_printf(dcb, "\tNumber of binlog events in error:  	%u\n",
                   router_inst->stats.n_binlog_errors);
	dcb_printf(dcb, "\tNumber of binlog rotate events:  	%u\n",
                   router_inst->stats.n_rotates);
	dcb_printf(dcb, "\tNumber of binlog cache hits:	  	%u\n",
                   router_inst->stats.n_cachehits);
	dcb_printf(dcb, "\tNumber of binlog cache misses:  	%u\n",
                   router_inst->stats.n_cachemisses);
	dcb_printf(dcb, "\tNumber of heartbeat events:     	%u\n",
                   router_inst->stats.n_heartbeats);
	dcb_printf(dcb, "\tNumber of packets received:		%u\n",
		   router_inst->stats.n_reads);
	dcb_printf(dcb, "\tNumber of residual data packets:	%u\n",
		   router_inst->stats.n_residuals);
	dcb_printf(dcb, "\tAverage events per packet		%.1f\n",
		   (double)router_inst->stats.n_binlogs / router_inst->stats.n_reads);
	dcb_printf(dcb, "\tLast event from master at:  		%s",
				buf);
	dcb_printf(dcb, "\t					(%d seconds ago)\n",
			time(0) - router_inst->stats.lastReply);
	dcb_printf(dcb, "\tLast event from master:  		0x%x\n",
			router_inst->lastEventReceived);
	if (router_inst->active_logs)
		dcb_printf(dcb, "\tRouter processing binlog records\n");
	if (router_inst->reconnect_pending)
		dcb_printf(dcb, "\tRouter pending reconnect to master\n");
	dcb_printf(dcb, "\tEvents received:\n");
	for (i = 0; i < 0x24; i++)
	{
		dcb_printf(dcb, "\t\t%-38s:  %u\n", event_names[i], router_inst->stats.events[i]);
	}

#if SPINLOCK_PROFILE
	dcb_printf(dcb, "\tSpinlock statistics (instlock):\n");
	spinlock_stats(&instlock, spin_reporter, dcb);
	dcb_printf(dcb, "\tSpinlock statistics (instance lock):\n");
	spinlock_stats(&router_inst->lock, spin_reporter, dcb);
#endif

	if (router_inst->slaves)
	{
		dcb_printf(dcb, "\tSlaves:\n");
		spinlock_acquire(&router_inst->lock);
		session = router_inst->slaves;
		while (session)
		{
			dcb_printf(dcb, "\t\tServer-id:			%d\n", session->serverid);
			if (session->hostname)
				dcb_printf(dcb, "\t\tHostname:			%s\n", session->hostname);
			dcb_printf(dcb, "\t\tSlave DCB:			%p\n", session->dcb);
			dcb_printf(dcb, "\t\tNext Sequence No:		%d\n", session->seqno);
			dcb_printf(dcb, "\t\tState:    			%s\n", blrs_states[session->state]);
			dcb_printf(dcb, "\t\tBinlog file:			%s\n", session->binlogfile);
			dcb_printf(dcb, "\t\tBinlog position:		%u\n", session->binlog_pos);
			if (session->nocrc)
				dcb_printf(dcb, "\t\tMaster Binlog CRC:		None\n");
			dcb_printf(dcb, "\t\tNo. requests:   		%u\n", session->stats.n_requests);
			dcb_printf(dcb, "\t\tNo. events sent:		%u\n", session->stats.n_events);
			dcb_printf(dcb, "\t\tNo. bursts sent:		%u\n", session->stats.n_bursts);
			dcb_printf(dcb, "\t\tNo. flow control:		%u\n", session->stats.n_flows);
			dcb_printf(dcb, "\t\tNo. catchup NRs:		%u\n", session->stats.n_catchupnr);
			dcb_printf(dcb, "\t\tNo. already up to date:		%u\n", session->stats.n_alreadyupd);
			dcb_printf(dcb, "\t\tNo. up to date:			%u\n", session->stats.n_upd);
			dcb_printf(dcb, "\t\tNo. of low water cbs		%u\n", session->stats.n_cb);
			dcb_printf(dcb, "\t\tNo. of drained cbs 		%u\n", session->stats.n_dcb);
			dcb_printf(dcb, "\t\tNo. of low water cbs N/A	%u\n", session->stats.n_cbna);
			dcb_printf(dcb, "\t\tNo. of events > high water 	%u\n", session->stats.n_above);
			dcb_printf(dcb, "\t\tNo. of failed reads		%u\n", session->stats.n_failed_read);
			dcb_printf(dcb, "\t\tNo. of nested distribute events	%u\n", session->stats.n_overrun);
			dcb_printf(dcb, "\t\tNo. of distribute action 1	%u\n", session->stats.n_actions[0]);
			dcb_printf(dcb, "\t\tNo. of distribute action 2	%u\n", session->stats.n_actions[1]);
			dcb_printf(dcb, "\t\tNo. of distribute action 3	%u\n", session->stats.n_actions[2]);
			if ((session->cstate & CS_UPTODATE) == 0)
			{
				dcb_printf(dcb, "\t\tSlave is in catchup mode. %s\n", 
			((session->cstate & CS_EXPECTCB) == 0 ? "" :
					"Waiting for DCB queue to drain."));

			}
			else
			{
				dcb_printf(dcb, "\t\tSlave is in normal mode.\n");
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
 * @param       action     	The action: REPLY, REPLY_AND_CLOSE, NEW_CONNECTION
 * @param	succp		Result of action
 *
 */
static  void
errorReply(ROUTER *instance, void *router_session, GWBUF *message, DCB *backend_dcb, error_action_t action, bool *succp)
{
       	LOGIF(LE, (skygw_log_write_flush(
		LOGFILE_ERROR, "Erorr Reply '%s'", message)));
	*succp = false;
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


static uint8_t getCapabilities(ROUTER *inst, void *router_session)
{
        return 0;
}
