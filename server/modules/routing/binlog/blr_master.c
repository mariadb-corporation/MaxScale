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
 * @file blr_master.c - contains code for the router to master communication
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
 * 07/05/2015	Massimiliano Pinto	Added MariaDB 10 Compatibility
 * 25/05/2015	Massimiliano Pinto	Added BLRM_SLAVE_STOPPED state
 * 08/06/2015	Massimiliano Pinto	Added m_errno and m_errmsg
 * 23/06/2015	Massimiliano Pinto	Master communication goes into BLRM_SLAVE_STOPPED state
 *					when an error is encountered in BLRM_BINLOGDUMP state.
 *					Server error code and msg are reported via SHOW SLAVE STATUS
 * 03/08/2015	Massimiliano Pinto	Initial implementation of transaction safety
 * 13/08/2015	Massimiliano Pinto	Addition of heartbeat check
 * 23/08/2015	Massimiliano Pinto	Added strerror_r
 * 26/08/2015	Massimiliano Pinto	Added MariaDB 10 GTID event check with flags = 0
 *					This is the current supported condition for detecting
 *					MariaDB 10 transaction start point.
 *					It's no longer using QUERY_EVENT with BEGIN	
 * 25/09/2015	Massimiliano Pinto	Addition of lastEventReceived for slaves
 * 23/10/15	Markus Makela		Added current_safe_event
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <service.h>
#include <server.h>
#include <router.h>
#include <atomic.h>
#include <session.h>
#include <blr.h>
#include <dcb.h>
#include <spinlock.h>
#include <housekeeper.h>
#include <buffer.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#include <rdtsc.h>

/* Temporary requirement for auth data */
#include <mysql_client_server_protocol.h>


static GWBUF *blr_make_query(char *statement);
static GWBUF *blr_make_registration(ROUTER_INSTANCE *router);
static GWBUF *blr_make_binlog_dump(ROUTER_INSTANCE *router);
void encode_value(unsigned char *data, unsigned int value, int len);
void blr_handle_binlog_record(ROUTER_INSTANCE *router, GWBUF *pkt);
static int  blr_rotate_event(ROUTER_INSTANCE *router, uint8_t *pkt, REP_HEADER *hdr);
void blr_distribute_binlog_record(ROUTER_INSTANCE *router, REP_HEADER *hdr, uint8_t *ptr);
static void *CreateMySQLAuthData(char *username, char *password, char *database);
void blr_extract_header(uint8_t *pkt, REP_HEADER *hdr);
static void blr_log_packet(int priority, char *msg, uint8_t *ptr, int len);
void blr_master_close(ROUTER_INSTANCE *);
char *blr_extract_column(GWBUF *buf, int col);
void blr_cache_response(ROUTER_INSTANCE *router, char *response, GWBUF *buf);
void poll_fake_write_event(DCB *dcb);
GWBUF *blr_read_events_from_pos(ROUTER_INSTANCE *router, unsigned long long pos, REP_HEADER *hdr, unsigned long long pos_end);
static void blr_check_last_master_event(void *inst);
extern int blr_check_heartbeat(ROUTER_INSTANCE *router);
extern char * blr_last_event_description(ROUTER_INSTANCE *router);
static void blr_log_identity(ROUTER_INSTANCE *router);
static void blr_distribute_error_message(ROUTER_INSTANCE *router, char *message, char *state, unsigned int err_code);

static int keepalive = 1;

/**
 * blr_start_master - controls the connection of the binlog router to the
 * master MySQL server and triggers the slave registration process for
 * the router.
 *
 * @param	router		The router instance
 */
void
blr_start_master(void* data)
{
    ROUTER_INSTANCE *router = (ROUTER_INSTANCE*)data;
DCB	*client;

	router->stats.n_binlogs_ses = 0;
	spinlock_acquire(&router->lock);
	if (router->master_state != BLRM_UNCONNECTED)
	{
		if (router->master_state != BLRM_SLAVE_STOPPED) {
			MXS_ERROR("%s: Master Connect: Unexpected master state %s\n",
                      router->service->name, blrm_states[router->master_state]);
		} else {
			MXS_NOTICE("%s: Master Connect: binlog state is %s\n",
                       router->service->name, blrm_states[router->master_state]);
		}
		spinlock_release(&router->lock);
		return;
	}
	router->master_state = BLRM_CONNECTING;

	/* Discard the queued residual data */
	while (router->residual)
	{
		router->residual = gwbuf_consume(router->residual, GWBUF_LENGTH(router->residual));
	}
	router->residual = NULL;

	spinlock_release(&router->lock);
	if ((client = dcb_alloc(DCB_ROLE_INTERNAL)) == NULL)
	{
		MXS_ERROR("Binlog router: failed to create DCB for dummy client");
		return;
	}
	router->client = client;
	client->state = DCB_STATE_POLLING;	/* Fake the client is reading */
	client->data = CreateMySQLAuthData(router->user, router->password, "");
	if ((router->session = session_alloc(router->service, client)) == NULL)
	{
		MXS_ERROR("Binlog router: failed to create session for connection to master");
		return;
	}
	client->session = router->session;
	if ((router->master = dcb_connect(router->service->dbref->server, router->session, BLR_PROTOCOL)) == NULL)
	{
		char *name;
		if ((name = malloc(strlen(router->service->name)
					+ strlen(" Master") + 1)) != NULL)
		{
			sprintf(name, "%s Master", router->service->name);
			hktask_oneshot(name, blr_start_master, router,
				BLR_MASTER_BACKOFF_TIME * router->retry_backoff++);
			free(name);
		}
		if (router->retry_backoff > BLR_MAX_BACKOFF)
			router->retry_backoff = BLR_MAX_BACKOFF;
		MXS_ERROR("Binlog router: failed to connect to master server '%s'",
                  router->service->dbref->server->unique_name);
		return;
	}
	router->master->remote = strdup(router->service->dbref->server->name);

	MXS_NOTICE("%s: attempting to connect to master server %s:%d, binlog %s, pos %lu",
               router->service->name, router->service->dbref->server->name,
               router->service->dbref->server->port, router->binlog_name, router->current_pos);

	router->connect_time = time(0);

	if (setsockopt(router->master->fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive , sizeof(keepalive )))
		perror("setsockopt");

	router->master_state = BLRM_AUTHENTICATED;
	router->master->func.write(router->master, blr_make_query("SELECT UNIX_TIMESTAMP()"));
	router->master_state = BLRM_TIMESTAMP;

	router->stats.n_masterstarts++;
}

/**
 * Reconnect to the master server.
 *
 * IMPORTANT - must be called with router->active_logs set by the
 * thread that set active_logs.
 *
 * @param	router		The router instance
 */
static void
blr_restart_master(ROUTER_INSTANCE *router)
{
	dcb_close(router->client);

	/* Discard the queued residual data */
	while (router->residual)
	{
		router->residual = gwbuf_consume(router->residual, GWBUF_LENGTH(router->residual));
	}
	router->residual = NULL;

	/* Now it is safe to unleash other threads on this router instance */
	spinlock_acquire(&router->lock);
	router->reconnect_pending = 0;
	router->active_logs = 0;
	spinlock_release(&router->lock);
	if (router->master_state < BLRM_BINLOGDUMP)
	{
		char *name;

		router->master_state = BLRM_UNCONNECTED;

		if ((name = malloc(strlen(router->service->name)
						+ strlen(" Master")+1)) != NULL)
		{
			sprintf(name, "%s Master", router->service->name);
			hktask_oneshot(name, blr_start_master, router,
				BLR_MASTER_BACKOFF_TIME * router->retry_backoff++);
			free(name);
		}
		if (router->retry_backoff > BLR_MAX_BACKOFF)
			router->retry_backoff = BLR_MAX_BACKOFF;
	}
	else
	{
		router->master_state = BLRM_UNCONNECTED;
		blr_start_master(router);
	}
}

/**
 * Request a reconnect to the master.
 *
 * If another thread is active processing messages from the master
 * then merely set a flag for that thread to do the restart. If no
 * threads are active then directly call the restart routine to
 * reconnect to the master.
 *
 * @param	router		The router instance
 */
void
blr_master_reconnect(ROUTER_INSTANCE *router)
{
int	do_reconnect = 0;

	if (router->master_state == BLRM_SLAVE_STOPPED)
		return;

	spinlock_acquire(&router->lock);
	if (router->active_logs)
	{
		/* Currently processing a response, set a flag
		 * and get the thread that is process a response
		 * to deal with the reconnect.
		 */
		router->reconnect_pending = 1;
		router->stats.n_delayedreconnects++;
	}
	else
	{
		router->active_logs = 1;
		do_reconnect = 1;
	}
	spinlock_release(&router->lock);
	if (do_reconnect)
	{
		blr_restart_master(router);
		spinlock_acquire(&router->lock);
		router->active_logs = 0;
		spinlock_release(&router->lock);
	}
}

/**
 * Shutdown a connection to the master
 *
 * @param router	The router instance
 */
void
blr_master_close(ROUTER_INSTANCE *router)
{
	dcb_close(router->master);
	router->master_state = BLRM_UNCONNECTED;
}

/**
 * Mark this master connection for a delayed reconnect, used during
 * error recovery to cause a reconnect after 60 seconds.
 *
 * @param router	The router instance
 */
void
blr_master_delayed_connect(ROUTER_INSTANCE *router)
{
char *name;

	if ((name = malloc(strlen(router->service->name)
					+ strlen(" Master Recovery")+1)) != NULL)
	{
		sprintf(name, "%s Master Recovery", router->service->name);
		hktask_oneshot(name, blr_start_master, router, 60);
		free(name);
	}
}

/**
 * Binlog router master side state machine event handler.
 *
 * Handles an incoming response from the master server to the binlog
 * router.
 *
 * @param router	The router instance
 * @param buf		The incoming packet
 */
void
blr_master_response(ROUTER_INSTANCE *router, GWBUF *buf)
{
char	query[BLRM_MASTER_REGITRATION_QUERY_LEN+1];
char	task_name[BLRM_TASK_NAME_LEN + 1] = "";

	atomic_add(&router->handling_threads, 1);
	ss_dassert(router->handling_threads == 1);
	spinlock_acquire(&router->lock);
	router->active_logs = 1;
	spinlock_release(&router->lock);
	if (router->master_state < 0 || router->master_state > BLRM_MAXSTATE)
	{
        	MXS_ERROR("Invalid master state machine state (%d) for binlog router.",
                      router->master_state);
		gwbuf_consume(buf, gwbuf_length(buf));

		spinlock_acquire(&router->lock);
		if (router->reconnect_pending)
		{
			router->active_logs = 0;
			spinlock_release(&router->lock);
			atomic_add(&router->handling_threads, -1);
	        	MXS_ERROR("%s: Pending reconnect in state %s.",
                          router->service->name,
                          blrm_states[router->master_state]);
			blr_restart_master(router);
			return;
		}
		router->active_logs = 0;
		spinlock_release(&router->lock);
		atomic_add(&router->handling_threads, -1);
		return;
	}

	if (router->master_state == BLRM_GTIDMODE && MYSQL_RESPONSE_ERR(buf))
	{
		/*
		 * If we get an error response to the GTID Mode then we
		 * asusme the server does not support GTID modes and
		 * continue. The error is saved and replayed to slaves if
		 * they also request the GTID mode.
		 */
        	MXS_ERROR("%s: Master server does not support GTID Mode.",
                      router->service->name);
	}
	else if (router->master_state != BLRM_BINLOGDUMP && MYSQL_RESPONSE_ERR(buf))
	{
		char *msg_err = NULL;
		int msg_len=0;
		int len = gwbuf_length(buf);
		unsigned long mysql_errno = extract_field(MYSQL_ERROR_CODE(buf), 16);

		msg_len = len-7-6; // +7 is where msg starts, 6 is skipped the status message (#42000)
		msg_err = (char *)malloc(msg_len + 1);

		// skip status message only as MYSQL_RESPONSE_ERR(buf) points to GWBUF_DATA(buf) +7
		strncpy(msg_err, (char *)(MYSQL_ERROR_MSG(buf) + 6), msg_len);

		/* NULL terminated error string */
		*(msg_err+msg_len)='\0';

		MXS_ERROR("%s: Received error: %lu, '%s' from master during '%s' phase "
                  "of the master state machine.",
                  router->service->name,
                  mysql_errno, msg_err,
                  blrm_states[router->master_state]);
		gwbuf_consume(buf, gwbuf_length(buf));

		spinlock_acquire(&router->lock);

		/* set mysql errno */
		router->m_errno = mysql_errno;

		/* set mysql error message */
		if (router->m_errmsg)
			free(router->m_errmsg);
		router->m_errmsg = msg_err;

		router->active_logs = 0;
		if (router->reconnect_pending)
		{
			spinlock_release(&router->lock);
			atomic_add(&router->handling_threads, -1);
			blr_restart_master(router);
			return;
		}
		spinlock_release(&router->lock);
		atomic_add(&router->handling_threads, -1);
		return;
	}
	switch (router->master_state)
	{
	case BLRM_TIMESTAMP:
		// Response to a timestamp message, no need to save this.
		gwbuf_consume(buf, GWBUF_LENGTH(buf));
		buf = blr_make_query("SHOW VARIABLES LIKE 'SERVER_ID'");
		router->master_state = BLRM_SERVERID;
		router->master->func.write(router->master, buf);
		router->retry_backoff = 1;
		break;
	case BLRM_SERVERID:
		{
		char *val = blr_extract_column(buf, 2);

		// Response to fetch of master's server-id
		if (router->saved_master.server_id)
			GWBUF_CONSUME_ALL(router->saved_master.server_id);
		router->saved_master.server_id = buf;
		blr_cache_response(router, "serverid", buf);

		// set router->masterid from master server-id if it's not set by the config option
		if (router->masterid == 0) {
			router->masterid = atoi(val);
		}

		{
		char str[BLRM_SET_HEARTBEAT_QUERY_LEN];
		sprintf(str, "SET @master_heartbeat_period = %lu000000000", router->heartbeat);
		buf = blr_make_query(str);
		}
		router->master_state = BLRM_HBPERIOD;
		router->master->func.write(router->master, buf);
		free(val);
		break;
		}
	case BLRM_HBPERIOD:
		// Response to set the heartbeat period
		if (router->saved_master.heartbeat)
			GWBUF_CONSUME_ALL(router->saved_master.heartbeat);
		router->saved_master.heartbeat = buf;
		blr_cache_response(router, "heartbeat", buf);
		buf = blr_make_query("SET @master_binlog_checksum = @@global.binlog_checksum");
		router->master_state = BLRM_CHKSUM1;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_CHKSUM1:
		// Response to set the master binlog checksum
		if (router->saved_master.chksum1)
			GWBUF_CONSUME_ALL(router->saved_master.chksum1);
		router->saved_master.chksum1 = buf;
		blr_cache_response(router, "chksum1", buf);
		buf = blr_make_query("SELECT @master_binlog_checksum");
		router->master_state = BLRM_CHKSUM2;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_CHKSUM2:
		{
		char *val = blr_extract_column(buf, 1);

		if (val && strncasecmp(val, "NONE", 4) == 0)
		{
			router->master_chksum = false;
		}
		if (val)
			free(val);
		// Response to the master_binlog_checksum, should be stored
		if (router->saved_master.chksum2)
			GWBUF_CONSUME_ALL(router->saved_master.chksum2);
		router->saved_master.chksum2 = buf;
		blr_cache_response(router, "chksum2", buf);

		if (router->mariadb10_compat) {
			buf = blr_make_query("SET @mariadb_slave_capability=4");
			router->master_state = BLRM_MARIADB10;
		} else {
			buf = blr_make_query("SELECT @@GLOBAL.GTID_MODE");
			router->master_state = BLRM_GTIDMODE;
		}
		router->master->func.write(router->master, buf);
		break;
		}
	case BLRM_MARIADB10:
		// Response to the SET @mariadb_slave_capability=4, should be stored
		if (router->saved_master.mariadb10)
			GWBUF_CONSUME_ALL(router->saved_master.mariadb10);
		router->saved_master.mariadb10 = buf;
		blr_cache_response(router, "mariadb10", buf);
		buf = blr_make_query("SHOW VARIABLES LIKE 'SERVER_UUID'");
		router->master_state = BLRM_MUUID;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_GTIDMODE:
		// Response to the GTID_MODE, should be stored
		if (router->saved_master.gtid_mode)
			GWBUF_CONSUME_ALL(router->saved_master.gtid_mode);
		router->saved_master.gtid_mode = buf;
		blr_cache_response(router, "gtidmode", buf);
		buf = blr_make_query("SHOW VARIABLES LIKE 'SERVER_UUID'");
		router->master_state = BLRM_MUUID;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_MUUID:
		{
		char *key;
		char *val = NULL;

		key = blr_extract_column(buf, 1);
		if (key && strlen(key))
			val = blr_extract_column(buf, 2);
		if (key)
			free(key);

		/* set the master_uuid from master if not set by the option */
		if (router->set_master_uuid == NULL) {
			free(router->master_uuid);
			router->master_uuid = val;
		} else {
			router->master_uuid = router->set_master_uuid;
		}

		// Response to the SERVER_UUID, should be stored
		if (router->saved_master.uuid)
			GWBUF_CONSUME_ALL(router->saved_master.uuid);
		router->saved_master.uuid = buf;
		blr_cache_response(router, "uuid", buf);
		sprintf(query, "SET @slave_uuid='%s'", router->uuid);
		buf = blr_make_query(query);
		router->master_state = BLRM_SUUID;
		router->master->func.write(router->master, buf);
		break;
		}
	case BLRM_SUUID:
		// Response to the SET @server_uuid, should be stored
		if (router->saved_master.setslaveuuid)
			GWBUF_CONSUME_ALL(router->saved_master.setslaveuuid);
		router->saved_master.setslaveuuid = buf;
		blr_cache_response(router, "ssuuid", buf);
		buf = blr_make_query("SET NAMES latin1");
		router->master_state = BLRM_LATIN1;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_LATIN1:
		// Response to the SET NAMES latin1, should be stored
		if (router->saved_master.setnames)
			GWBUF_CONSUME_ALL(router->saved_master.setnames);
		router->saved_master.setnames = buf;
		blr_cache_response(router, "setnames", buf);
		buf = blr_make_query("SET NAMES utf8");
		router->master_state = BLRM_UTF8;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_UTF8:
		// Response to the SET NAMES utf8, should be stored
		if (router->saved_master.utf8)
			GWBUF_CONSUME_ALL(router->saved_master.utf8);
		router->saved_master.utf8 = buf;
		blr_cache_response(router, "utf8", buf);
		buf = blr_make_query("SELECT 1");
		router->master_state = BLRM_SELECT1;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_SELECT1:
		// Response to the SELECT 1, should be stored
		if (router->saved_master.select1)
			GWBUF_CONSUME_ALL(router->saved_master.select1);
		router->saved_master.select1 = buf;
		blr_cache_response(router, "select1", buf);
		buf = blr_make_query("SELECT VERSION()");
		router->master_state = BLRM_SELECTVER;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_SELECTVER:
		// Response to SELECT VERSION should be stored
		if (router->saved_master.selectver)
			GWBUF_CONSUME_ALL(router->saved_master.selectver);
		router->saved_master.selectver = buf;
		blr_cache_response(router, "selectver", buf);
		buf = blr_make_query("SELECT @@version_comment limit 1");
		router->master_state = BLRM_SELECTVERCOM;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_SELECTVERCOM:
		// Response to SELECT @@version_comment should be stored
		if (router->saved_master.selectvercom)
			GWBUF_CONSUME_ALL(router->saved_master.selectvercom);
		router->saved_master.selectvercom = buf;
		blr_cache_response(router, "selectvercom", buf);
		buf = blr_make_query("SELECT @@hostname");
		router->master_state = BLRM_SELECTHOSTNAME;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_SELECTHOSTNAME:
		// Response to SELECT @@hostname should be stored
		if (router->saved_master.selecthostname)
			GWBUF_CONSUME_ALL(router->saved_master.selecthostname);
		router->saved_master.selecthostname = buf;
		blr_cache_response(router, "selecthostname", buf);
		buf = blr_make_query("SELECT @@max_allowed_packet");
		router->master_state = BLRM_MAP;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_MAP:
		// Response to SELECT @@max_allowed_packet should be stored
		if (router->saved_master.map)
			GWBUF_CONSUME_ALL(router->saved_master.map);
		router->saved_master.map = buf;
		blr_cache_response(router, "map", buf);
		buf = blr_make_registration(router);
		router->master_state = BLRM_REGISTER;
		router->master->func.write(router->master, buf);
		break;
	case BLRM_REGISTER:
		// Request a dump of the binlog file
		buf = blr_make_binlog_dump(router);
		router->master_state = BLRM_BINLOGDUMP;
		router->master->func.write(router->master, buf);
		MXS_NOTICE("%s: Request binlog records from %s at "
                   "position %lu from master server %s:%d",
                   router->service->name, router->binlog_name,
                   router->current_pos,
                   router->service->dbref->server->name,
                   router->service->dbref->server->port);

		/* Log binlog router identity */
		blr_log_identity(router);

		break;
	case BLRM_BINLOGDUMP:
		/**
		 * Main body, we have received a binlog record from the master
		 */
		blr_handle_binlog_record(router, buf);

		/**
		 * Set heartbeat check task
		 */
		snprintf(task_name, BLRM_TASK_NAME_LEN, "%s heartbeat", router->service->name);
		hktask_add(task_name, blr_check_last_master_event, router, router->heartbeat);

		break;
	}

	if (router->reconnect_pending)
		blr_restart_master(router);
	spinlock_acquire(&router->lock);
	router->active_logs = 0;
	spinlock_release(&router->lock);
	atomic_add(&router->handling_threads, -1);
}

/**
 * Build a MySQL query into a GWBUF that we can send to the master database
 *
 * @param	query		The text of the query to send
 */
static GWBUF *
blr_make_query(char *query)
{
GWBUF		*buf;
unsigned char	*data;
int		len;

	if ((buf = gwbuf_alloc(strlen(query) + 5)) == NULL)
		return NULL;
	data = GWBUF_DATA(buf);
	len = strlen(query) + 1;
	encode_value(&data[0], len, 24);	// Payload length
	data[3] = 0;				// Sequence id
						// Payload
	data[4] = COM_QUERY;			// Command
	memcpy(&data[5], query, strlen(query));

	return buf;
}

/**
 * Build a MySQL slave registration into a GWBUF that we can send to the
 * master database
 *
 * @param	router 	The router instance
 * @return	A MySQL Replication registration message in a GWBUF structure
 */
static GWBUF *
blr_make_registration(ROUTER_INSTANCE *router)
{
GWBUF		*buf;
unsigned char	*data;
int		len = 18;

	if ((buf = gwbuf_alloc(len + 4)) == NULL)
		return NULL;
	data = GWBUF_DATA(buf);
	encode_value(&data[0], len, 24);		// Payload length
	data[3] = 0;					// Sequence ID
	data[4] = COM_REGISTER_SLAVE;			// Command
	encode_value(&data[5], router->serverid, 32);	// Slave Server ID
	data[9] = 0;					// Slave hostname length
	data[10] = 0;					// Slave username length
	data[11] = 0;					// Slave password length
	encode_value(&data[12],
		router->service->ports->port, 16);	// Slave master port
	encode_value(&data[14], 0, 32);			// Replication rank
	encode_value(&data[18], router->masterid, 32);	// Master server-id

	return buf;
}


/**
 * Build a Binlog dump command into a GWBUF that we can send to the
 * master database
 *
 * @param	router 	The router instance
 * @return	A MySQL Replication COM_BINLOG_DUMP message in a GWBUF structure
 */
static GWBUF *
blr_make_binlog_dump(ROUTER_INSTANCE *router)
{
GWBUF		*buf;
unsigned char	*data;
int		len = 0x1b;

	if ((buf = gwbuf_alloc(len + 4)) == NULL)
		return NULL;
	data = GWBUF_DATA(buf);

	encode_value(&data[0], len,24);			// Payload length
	data[3] = 0;					// Sequence ID
	data[4] = COM_BINLOG_DUMP;			// Command
	encode_value(&data[5],
		router->current_pos, 32);		// binlog position
	encode_value(&data[9], 0, 16);			// Flags
	encode_value(&data[11],
		router->serverid, 32);			// Server-id of MaxScale
	strncpy((char *)&data[15], router->binlog_name,
				BINLOG_FNAMELEN);	// binlog filename
	return buf;
}


/**
 * Encode a value into a number of bits in a MySQL packet
 *
 * @param	data	Point to location in target packet
 * @param	value	The value to pack
 * @param	len	Number of bits to encode value into
 */
void
encode_value(unsigned char *data, unsigned int value, int len)
{
	while (len > 0)
	{
		*data++ = value & 0xff;
		value >>= 8;
		len -= 8;
	}
}

/**
 * blr_handle_binlog_record - we have received binlog records from
 * the master and we must now work out what to do with them.
 *
 * @param router	The router instance
 * @param pkt		The binlog records
 */
void
blr_handle_binlog_record(ROUTER_INSTANCE *router, GWBUF *pkt)
{
uint8_t			*msg = NULL, *ptr, *pdata;
REP_HEADER		hdr;
unsigned int		len = 0, reslen;
unsigned int		pkt_length;
int			no_residual = 1;
int			preslen = -1;
int			prev_length = -1;
int			n_bufs = -1, pn_bufs = -1;

	/*
	 * Prepend any residual buffer to the buffer chain we have
	 * been called with.
	 */
	if (router->residual)
	{
		pkt = gwbuf_append(router->residual, pkt);
		router->residual = NULL;
		no_residual = 0;
	}

	pkt_length = gwbuf_length(pkt);
	/*
	 * Loop over all the packets while we still have some data
	 * and the packet length is enough to hold a replication event
	 * header.
	 */
	while (pkt && pkt_length > 24)
	{
		reslen = GWBUF_LENGTH(pkt);
		pdata = GWBUF_DATA(pkt);
		if (reslen < 3)	// Payload length straddles buffers
		{
			/* Get the length of the packet from the residual and new packet */
			if (reslen >= 3)
			{
				len = EXTRACT24(pdata);
			}
			else if (reslen == 2)
			{
				len = EXTRACT16(pdata);
				len |= (*(uint8_t *)GWBUF_DATA(pkt->next) << 16);
			}
			else if (reslen == 1)
			{
				len = *pdata;
				len |= (EXTRACT16(GWBUF_DATA(pkt->next)) << 8);
			}
			len += 4; 	// Allow space for the header
		}
		else
		{
			len = EXTRACT24(pdata) + 4;
		}
		/* len is now the payload length for the packet we are working on */

		if (reslen < len && pkt_length >= len)
		{
			/*
			 * The message is contained in more than the current
			 * buffer, however we have the complete messasge in
			 * this buffer and the chain of remaining buffers.
			 *
			 * Allocate a contiguous buffer for the binlog message
			 * and copy the complete message into this buffer.
			 */
			int remainder = len;
			GWBUF *p = pkt;

			if ((msg = malloc(len)) == NULL)
			{
        			MXS_ERROR("Insufficient memory to buffer event "
                              "of %d bytes. Binlog %s @ %lu.",
                              len, router->binlog_name,
                              router->current_pos);
				break;
			}

			n_bufs = 0;
			ptr = msg;
			while (p && remainder > 0)
			{
				int plen = GWBUF_LENGTH(p);
				int n = (remainder > plen ? plen : remainder);
				memcpy(ptr, GWBUF_DATA(p), n);
				remainder -= n;
				ptr += n;
				if (remainder > 0)
					p = p->next;
			n_bufs++;
			}
			if (remainder)
			{
        			MXS_ERROR("Expected entire message in buffer "
                              "chain, but failed to create complete "
                              "message as expected. %s @ %lu",
                              router->binlog_name,
                              router->current_pos);
				free(msg);
				msg = NULL;
				break;
			}

			ptr = msg;
		}
		else if (reslen < len)
		{
			/*
			 * The message is not fully contained in the current
			 * and we do not have the complete message in the
			 * buffer chain. Therefore we must stop processing
			 * until we receive the next buffer.
			 */
			router->stats.n_residuals++;
	        	MXS_DEBUG("Residual data left after %lu records. %s @ %lu",
                          router->stats.n_binlogs,
                          router->binlog_name, router->current_pos);
			break;
		}
		else
		{
			/*
			 * The message is fully contained in the current buffer
			 */
			ptr = pdata;
			n_bufs = 1;
		}

		/*
		 * ptr now points at the current message in a contiguous buffer,
		 * this buffer is either within the GWBUF or in a malloc'd
		 * copy if the message straddles GWBUF's.
		 */

		if (len < BINLOG_EVENT_HDR_LEN)
		{
		char	*msg = "";

			/* Packet is too small to be a binlog event */
			if (ptr[4] == 0xfe)	/* EOF Packet */
			{
				msg = "end of file";
			}
			else if (ptr[4] == 0xff)	/* EOF Packet */
			{
				msg = "error";
			}
			MXS_NOTICE("Non-event message (%s) from master.", msg);
		}
		else
		{
			router->stats.n_binlogs++;
			router->stats.n_binlogs_ses++;

			blr_extract_header(ptr, &hdr);

			/* Sanity check */
			if (hdr.ok == 0 && hdr.event_size != len - 5)
			{
				MXS_ERROR("Packet length is %d, but event size is %d, "
                          "binlog file %s position %lu "
                          "reslen is %d and preslen is %d, "
                          "length of previous event %d. %s",
                          len, hdr.event_size,
                          router->binlog_name,
                          router->current_pos,
                          reslen, preslen, prev_length,
                          (prev_length == -1 ?
                           (no_residual ? "No residual data from previous call" :
                            "Residual data from previous call") : ""));

				blr_log_packet(LOG_ERR, "Packet:", ptr, len);
				MXS_ERROR("This event (0x%x) was contained in %d GWBUFs, "
                          "the previous events was contained in %d GWBUFs",
                          router->lastEventReceived, n_bufs, pn_bufs);
				if (msg)
				{
					free(msg);
					msg = NULL;
				}
				break;
			}

			if (hdr.ok == 0)
			{
				int event_limit;

				spinlock_acquire(&router->lock);

				/* set mysql errno to 0 */
				router->m_errno = 0;

				/* Remove error message */
				if (router->m_errmsg)
					free(router->m_errmsg);
				router->m_errmsg = NULL;

				spinlock_release(&router->lock);

#ifdef SHOW_EVENTS
				printf("blr: event type 0x%02x, flags 0x%04x, event size %d, event timestamp %lu\n", hdr.event_type, hdr.flags, hdr.event_size, hdr.timestamp);
#endif

				/*
				 * First check that the checksum we calculate matches the
				 * checksum in the packet we received.
				 */
				if (router->master_chksum)
				{
					uint32_t	chksum, pktsum;

					chksum = crc32(0L, NULL, 0);
					chksum = crc32(chksum, ptr + 5, hdr.event_size  - 4);
					pktsum = EXTRACT32(ptr + hdr.event_size + 1);
					if (pktsum != chksum)
					{
						router->stats.n_badcrc++;
						if (msg)
						{
							free(msg);
							msg = NULL;
						}
						MXS_ERROR("%s: Checksum error in event "
                                  "from master, "
                                  "binlog %s @ %lu. "
                                  "Closing master connection.",
                                  router->service->name,
                                  router->binlog_name,
                                  router->current_pos);
						blr_master_close(router);
						blr_master_delayed_connect(router);
						return;
					}
				}

				router->lastEventReceived = hdr.event_type;
				router->lastEventTimestamp = hdr.timestamp;

				/**
				 * Check for an open transaction, if the option is set
				 * Only complete transactions should be sent to sleves
				 *
				 * If a trasaction is pending router->binlog_position
				 * won't be updated to router->current_pos
				 */

				spinlock_acquire(&router->binlog_lock);
				if (router->trx_safe == 0 || (router->trx_safe && router->pending_transaction == 0)) {
					/* no pending transaction: set current_pos to binlog_position */
					router->binlog_position = router->current_pos;
					router->current_safe_event = router->current_pos;
				}
				spinlock_release(&router->binlog_lock);

				/**
				 * Detect transactions in events
				 * Only complete transactions should be sent to sleves
				*/

		                /* If MariaDB 10 compatibility:
				 * check for MARIADB10_GTID_EVENT with flags = 0
				 * This marks the transaction starts instead of
				 * QUERY_EVENT with "BEGIN"
				*/
				if (router->trx_safe) {
					if (router->mariadb10_compat) {
						if (hdr.event_type == MARIADB10_GTID_EVENT) {
							uint64_t n_sequence;
							uint32_t domainid;
							unsigned int flags;	
							n_sequence = extract_field(ptr+4+20, 64);
							domainid = extract_field(ptr+4+20 + 8, 32);
							flags = *(ptr+4+20 + 8 + 4);

							if (flags == 0) {
								spinlock_acquire(&router->binlog_lock);

								if (router->pending_transaction > 0) {
									MXS_ERROR("A MariaDB 10 transaction "
                                              "is already open "
                                              "@ %lu (GTID %u-%u-%lu) and "
                                              "a new one starts @ %lu",
                                              router->binlog_position,
                                              domainid, hdr.serverid,
                                              n_sequence,
                                              router->current_pos);

										// An action should be taken here
								}

								router->pending_transaction = 1;

								spinlock_release(&router->binlog_lock);
							}
						}
					}

					/**
					 * look for QUERY_EVENT [BEGIN / COMMIT] and XID_EVENT
					 */

					if(hdr.event_type == QUERY_EVENT) {
						char *statement_sql;
						int db_name_len, var_block_len, statement_len;
						db_name_len = ptr[4+20+ 4 + 4];
						var_block_len = ptr[4+20+ 4 + 4 + 1 + 2];

						statement_len = len - (4+20+4+4+1+2+2+var_block_len+1+db_name_len);
						statement_sql = calloc(1, statement_len+1);
						strncpy(statement_sql, (char *)ptr+4+20+4+4+1+2+2+var_block_len+1+db_name_len, statement_len);

						spinlock_acquire(&router->binlog_lock);

						/* Check for BEGIN (it comes for START TRANSACTION too) */
						if (strncmp(statement_sql, "BEGIN", 5) == 0) {
							if (router->pending_transaction > 0) {
								MXS_ERROR("A transaction is already open "
                                          "@ %lu and a new one starts @ %lu",
                                          router->binlog_position,
                                          router->current_pos);

									// An action should be taken here
							}

							router->pending_transaction = 1;
						}

						/* Check for COMMIT in non transactional store engines */
						if (strncmp(statement_sql, "COMMIT", 6) == 0) {

							router->pending_transaction = 2;
						}

						spinlock_release(&router->binlog_lock);

						free(statement_sql);
					}

					/* Check for COMMIT in Transactional engines, i.e InnoDB */
					if(hdr.event_type == XID_EVENT) {
						spinlock_acquire(&router->binlog_lock);

						if (router->pending_transaction) {
							router->pending_transaction = 3;
						}
						spinlock_release(&router->binlog_lock);
					}
				}

				event_limit = router->mariadb10_compat ? MAX_EVENT_TYPE_MARIADB10 : MAX_EVENT_TYPE;

				if (hdr.event_type <= event_limit)
					router->stats.events[hdr.event_type]++;

				if (hdr.event_type == FORMAT_DESCRIPTION_EVENT && hdr.next_pos == 0)
				{
					// Fake format description message
					MXS_DEBUG("Replication fake event. "
                              "Binlog %s @ %lu.",
                              router->binlog_name,
                              router->current_pos);
					router->stats.n_fakeevents++;

					if (hdr.event_type == FORMAT_DESCRIPTION_EVENT)
					{
						uint8_t 	*new_fde;
						unsigned int	new_fde_len;
						/*
						 * We need to save this to replay to new
						 * slaves that attach later.
						 */
						new_fde_len = hdr.event_size;
						new_fde = malloc(hdr.event_size);
						if (new_fde)
						{
							memcpy(new_fde, ptr + 5, hdr.event_size);
							if (router->saved_master.fde_event)
								free(router->saved_master.fde_event);
							router->saved_master.fde_event = new_fde;
							router->saved_master.fde_len = new_fde_len;
						}
						else
						{
							MXS_ERROR("%s: Received a format description "
                                      "event that MaxScale was unable to "
                                      "record. Event length is %d.",
                                      router->service->name,
                                      hdr.event_size);
							blr_log_packet(LOG_ERR,
								"Format Description Event:", ptr, len);
						}
					}
				}
				else
				{
					if (hdr.event_type == HEARTBEAT_EVENT)
					{
#ifdef SHOW_EVENTS
						printf("Replication heartbeat\n");
#endif
						MXS_DEBUG("Replication heartbeat. "
                                  "Binlog %s @ %lu.",
                                  router->binlog_name,
                                  router->current_pos);

						router->stats.n_heartbeats++;

						if (router->pending_transaction)
							router->stats.lastReply = time(0);
					}
					else if (hdr.flags != LOG_EVENT_ARTIFICIAL_F)
					{
						ptr = ptr + 5;	// We don't put the first byte of the payload
								// into the binlog file
						if (hdr.event_type == ROTATE_EVENT)
							router->rotating = 1;

						/* current event is being written to disk file */
						if (blr_write_binlog_record(router, &hdr, ptr) == 0)
						{
							/*
							 * Failed to write to the
							 * binlog file, destroy the
							 * buffer chain and close the
							 * connection with the master
							 */
							while ((pkt = gwbuf_consume(pkt,
								 GWBUF_LENGTH(pkt))) != NULL);
							blr_master_close(router);
							blr_master_delayed_connect(router);
							return;
						}

						/* Check for rotete event */
						if (hdr.event_type == ROTATE_EVENT)
						{
							if (!blr_rotate_event(router, ptr, &hdr))
							{
								/*
								 * Failed to write to the
								 * binlog file, destroy the
								 * buffer chain and close the
								 * connection with the master
								 */
								while ((pkt = gwbuf_consume(pkt,
									 GWBUF_LENGTH(pkt))) != NULL);
								blr_master_close(router);
								blr_master_delayed_connect(router);
								return;
							}
						}

						/**
						 * Distributing binlog events to slaves
						 * may depend on pending transaction
						 */

						spinlock_acquire(&router->binlog_lock);

						if (router->trx_safe == 0 || (router->trx_safe && router->pending_transaction == 0)) {

							router->binlog_position = router->current_pos;
							router->current_safe_event = router->current_pos;

							spinlock_release(&router->binlog_lock);

							/* Now distribute events */
							blr_distribute_binlog_record(router, &hdr, ptr);
						} else {
							/**
							 * If transaction is closed:
							 *
							 * 1) read current binlog starting
							 * 	from router->binlog_position
							 *
							 * 2) distribute read event
							 *
							 * 3) set router->binlog_position to
							 *    router->current_pos
							 *
							 */

							 if (router->pending_transaction > 1) {
								unsigned long long pos;
								unsigned long long end_pos;
								GWBUF   *record;
								uint8_t *raw_data;
								REP_HEADER      new_hdr;
								int i=0;

								pos = router->binlog_position;
								end_pos = router->current_pos;

								spinlock_release(&router->binlog_lock);

								while ((record = blr_read_events_from_pos(router, pos, &new_hdr, end_pos)) != NULL) {
									i++;
									raw_data = GWBUF_DATA(record);

									/* distribute event */
									blr_distribute_binlog_record(router, &new_hdr, raw_data);

									spinlock_acquire(&router->binlog_lock);

									/** The current safe position is only updated
									* if it points to the event we just distributed. */
									if(router->current_safe_event == pos)
									{
										router->current_safe_event = new_hdr.next_pos;
									}

									pos = new_hdr.next_pos;

									spinlock_release(&router->binlog_lock);

									gwbuf_free(record);
								}

								/* Check whether binlog records has been read in previous loop */
								if (pos < router->current_pos) {
									char err_message[BINLOG_ERROR_MSG_LEN+1];

									err_message[BINLOG_ERROR_MSG_LEN] = '\0';

									/* No event has been sent */
									if (pos == router->binlog_position) {
										MXS_ERROR("No events distributed to slaves for a pending "
                                                  "transaction in %s at %lu. "
                                                  "Last event from master at %lu",
                                                  router->binlog_name,
                                                  router->binlog_position,
                                                  router->current_pos);

										strncpy(err_message, "No transaction events sent", BINLOG_ERROR_MSG_LEN);
									} else {
										/* Some events have been sent */
										MXS_ERROR("Some events were not distributed to slaves for a "
                                                  "pending transaction in %s at %lu. Last distributed "
                                                  "even at %llu, last event from master at %lu",
                                                  router->binlog_name,
                                                  router->binlog_position,
                                                  pos,
                                                  router->current_pos);

										strncpy(err_message, "Incomplete transaction events sent", BINLOG_ERROR_MSG_LEN);
									}

									/* distribute error message to registered slaves */
									blr_distribute_error_message(router, err_message, "HY000", 1236);
								}

								/* update binlog_position and set pending to 0 */
								spinlock_acquire(&router->binlog_lock);

								router->binlog_position = router->current_pos;
								router->pending_transaction = 0;

								spinlock_release(&router->binlog_lock);
							} else {
								spinlock_release(&router->binlog_lock);
							}
						}
					}
					else
					{
						router->stats.n_artificial++;
						MXS_DEBUG("Artificial event not written "
                                  "to disk or distributed. "
                                  "Type 0x%x, Length %d, Binlog "
                                  "%s @ %lu.",
                                  hdr.event_type,
                                  hdr.event_size,
                                  router->binlog_name,
                                  router->current_pos);
						ptr += 5;
						if (hdr.event_type == ROTATE_EVENT)
						{
							router->rotating = 1;
							if (!blr_rotate_event(router, ptr, &hdr))
							{
								/*
								 * Failed to write to the
								 * binlog file, destroy the
								 * buffer chain and close the
								 * connection with the master
								 */
								while ((pkt = gwbuf_consume(pkt,
									 GWBUF_LENGTH(pkt))) != NULL);
								blr_master_close(router);
								blr_master_delayed_connect(router);
								return;
							}
						}
					}
				}
			}
			else
			{
				unsigned long mysql_errno = extract_field(ptr+5, 16);
				char *msg_err = NULL;
				int msg_len=0;
				msg_err = (char *)ptr+7+6;	// err msg starts after 7 bytes + 6 of status message
				msg_len = len-7-6;	// msg len is decreased by 7 and 6
				msg_err = (char *)malloc(msg_len + 1);
				strncpy(msg_err, (char *)ptr+7+6, msg_len);
				/* NULL terminate error string */
				*(msg_err+msg_len)='\0';

				spinlock_acquire(&router->lock);

				/* set mysql_errno */
				router->m_errno = mysql_errno;

				/* set io error message */
				if (router->m_errmsg)
					free(router->m_errmsg);
				router->m_errmsg = msg_err;

				/* Force stopped state */
				router->master_state = BLRM_SLAVE_STOPPED;

				spinlock_release(&router->lock);

				MXS_ERROR("Error packet in binlog stream.%s @ %lu.",
                          router->binlog_name,
                          router->current_pos);

				router->stats.n_binlog_errors++;
			}
		}

		if (msg)
		{
			free(msg);
			msg = NULL;
		}
		prev_length = len;
		while (len > 0)
		{
			int n, plen;
			plen = GWBUF_LENGTH(pkt);
			n = (plen < len ? plen : len);
			pkt = gwbuf_consume(pkt, n);
			len -= n;
			pkt_length -= n;
		}
		preslen = reslen;
		pn_bufs = n_bufs;
	}

	/*
	 * Check if we have a residual, part binlog message to deal with.
	 * Just simply store the GWBUF for next time
	 */
	if (pkt)
	{
		router->residual = pkt;
		ss_dassert(pkt_length != 0);
	}
	else
	{
		ss_dassert(pkt_length == 0);
	}
	blr_file_flush(router);
}

/**
 * Populate a header structure for a replication message from a GWBUF structure.
 *
 * @param pkt	The incoming packet in a GWBUF chain
 * @param hdr	The packet header to populate
 */
void
blr_extract_header(register uint8_t *ptr, register REP_HEADER *hdr)
{

	hdr->payload_len = EXTRACT24(ptr);
	hdr->seqno = ptr[3];
	hdr->ok = ptr[4];
	hdr->timestamp = EXTRACT32(&ptr[5]);
	hdr->event_type = ptr[9];
	hdr->serverid = EXTRACT32(&ptr[10]);
	hdr->event_size = EXTRACT32(&ptr[14]);
	hdr->next_pos = EXTRACT32(&ptr[18]);
	hdr->flags = EXTRACT16(&ptr[22]);
}

/**
 * Process a binlog rotate event.
 *
 * @param router	The instance of the router
 * @param ptr		The packet containing the rotate event
 * @param hdr		The replication message header
 */
static int
blr_rotate_event(ROUTER_INSTANCE *router, uint8_t *ptr, REP_HEADER *hdr)
{
int		len, slen;
uint64_t	pos;
char		file[BINLOG_FNAMELEN+1];

	ptr += 19;		// Skip event header
	len = hdr->event_size - 19;	// Event size minus header
	pos = extract_field(ptr+4, 32);
	pos <<= 32;
	pos |= extract_field(ptr, 32);
	slen = len - (8 + 4);		// Allow for position and CRC
	if (router->master_chksum == 0)
		slen += 4;
	if (slen > BINLOG_FNAMELEN)
		slen = BINLOG_FNAMELEN;
	memcpy(file, ptr + 8, slen);
	file[slen] = 0;

#ifdef VERBOSE_ROTATE
	printf("binlog rotate: ");
	while (len--)
		printf("0x%02x ", *ptr++);
	printf("\n");
	printf("New file: %s @ %ld\n", file, pos);
#endif

	strcpy(router->prevbinlog, router->binlog_name);

	if (strncmp(router->binlog_name, file, slen) != 0)
	{
		router->stats.n_rotates++;
		if (blr_file_rotate(router, file, pos) == 0)
		{
			router->rotating = 0;
			return 0;
		}
	}
	router->rotating = 0;
	return 1;
}

/**
 * Create the auth data needed to be able to call dcb_connect.
 * 
 * This doesn't really belong here and should be moved at some stage.
 */
static void *
CreateMySQLAuthData(char *username, char *password, char *database)
{
MYSQL_session	*auth_info;

	if (username == NULL || password == NULL)
	{
        	MXS_ERROR("You must specify both username and password for the binlog router.\n");
		return NULL;
	}

	if ((auth_info = calloc(1, sizeof(MYSQL_session))) == NULL)
		return NULL;
	strncpy(auth_info->user, username,MYSQL_USER_MAXLEN);
	strncpy(auth_info->db, database,MYSQL_DATABASE_MAXLEN);
	gw_sha1_str((const uint8_t *)password, strlen(password), auth_info->client_sha1);

	return auth_info;
}

/** Actions that can be taken when an event is being distributed to the slaves*/
typedef enum
{
    SLAVE_SEND_EVENT, /*< Send the event to the slave */
    SLAVE_FORCE_CATCHUP, /*< Force the slave into catchup mode */
    SLAVE_EVENT_ALREADY_SENT /*< The slave already has the event, don't send it */
} slave_event_action_t;

/**
 * Distribute the binlog record we have just received to all the registered slaves.
 *
 * @param	router		The router instance
 * @param	hdr		The replication event header
 * @param	ptr		The raw replication event data
 */
void
blr_distribute_binlog_record(ROUTER_INSTANCE *router, REP_HEADER *hdr, uint8_t *ptr)
{
GWBUF		*pkt;
uint8_t		*buf;
ROUTER_SLAVE	*slave, *nextslave;
int		action;
unsigned int cstate;

	spinlock_acquire(&router->lock);
	slave = router->slaves;
	while (slave)
	{
		if (slave->state != BLRS_DUMPING)
		{
			slave = slave->next;
			continue;
		}
		spinlock_acquire(&slave->catch_lock);
		if ((slave->cstate & (CS_UPTODATE|CS_BUSY)) == CS_UPTODATE)
		{
			/*
			 * This slave is reporting it is to date with the binlog of the
			 * master running on this slave.
			 * It has no thread running currently that is sending binlog
			 * events.
			 */
			action = 1;
			slave->cstate |= CS_BUSY;
		}
		else if ((slave->cstate & (CS_UPTODATE|CS_BUSY)) == (CS_UPTODATE|CS_BUSY))
		{
			/*
			 * The slave is up to date with the binlog and a process is
			 * running on this slave to send binlog events.
			 */
			slave->overrun = 1;
			action = 2;
		}
		else if ((slave->cstate & CS_UPTODATE) == 0)
		{
			/* Slave is in catchup mode */
			action = 3;
		}
		slave->stats.n_actions[action-1]++;
		spinlock_release(&slave->catch_lock);

		if (action == 1)
		{
			spinlock_acquire(&router->binlog_lock);

			slave_event_action_t slave_action = SLAVE_FORCE_CATCHUP;

			if(router->trx_safe && slave->binlog_pos == router->current_safe_event &&
			   (strcmp(slave->binlogfile, router->binlog_name) == 0 ||
				(hdr->event_type == ROTATE_EVENT &&
				strcmp(slave->binlogfile, router->prevbinlog))))
			{
				/**
				 * Slave needs the current event being distributed
				 */
				slave_action = SLAVE_SEND_EVENT;
			}
			else if (slave->binlog_pos == router->last_written &&
				(strcmp(slave->binlogfile, router->binlog_name) == 0 ||
				(hdr->event_type == ROTATE_EVENT &&
				strcmp(slave->binlogfile, router->prevbinlog))))
			{
				/**
				 * Transaction safety is off or there are no pending transactions
				 */

				slave_action = SLAVE_SEND_EVENT;
			}
			else if (slave->binlog_pos == hdr->next_pos
				&& strcmp(slave->binlogfile, router->binlog_name) == 0)
			{
				/*
				 * Slave has already read record from file, no
				 * need to distrbute this event
				 */
				slave_action = SLAVE_EVENT_ALREADY_SENT;
			}
			else if ((slave->binlog_pos > hdr->next_pos - hdr->event_size)
				&& strcmp(slave->binlogfile, router->binlog_name) == 0)
			{
				/*
				 * The slave is ahead of the master, this should never
				 * happen. Force the slave to catchup mode in order to
				 * try to resolve the issue.
				 */
				MXS_ERROR("Slave %d is ahead of expected position %s@%lu. "
                          "Expected position %d",
                          slave->serverid, slave->binlogfile,
                          (unsigned long)slave->binlog_pos,
                          hdr->next_pos - hdr->event_size);
			}

			spinlock_release(&router->binlog_lock);

			/*
			 * If slave_action is SLAVE_FORCE_CATCHUP then
			 * the slave is not at the position it should be. Force it into
			 * catchup mode rather than send this event.
			 */

			switch(slave_action)
			{
				case SLAVE_SEND_EVENT:
					/*
					 * The slave should be up to date, check that the binlog
					 * position matches the event we have to distribute or
					 * this is a rotate event. Send the event directly from
					 * memory to the slave.
					 */
					slave->lastEventTimestamp = hdr->timestamp;
					slave->lastEventReceived = hdr->event_type;

					/* set lastReply */
					if (router->send_slave_heartbeat)
						slave->lastReply = time(0);

					pkt = gwbuf_alloc(hdr->event_size + 5);
					buf = GWBUF_DATA(pkt);
					encode_value(buf, hdr->event_size + 1, 24);
					buf += 3;
					*buf++ = slave->seqno++;
					*buf++ = 0;	// OK
					memcpy(buf, ptr, hdr->event_size);
					if (hdr->event_type == ROTATE_EVENT)
					{
						blr_slave_rotate(router, slave, ptr);
					}
					slave->stats.n_bytes += gwbuf_length(pkt);
					slave->stats.n_events++;
					slave->dcb->func.write(slave->dcb, pkt);
					spinlock_acquire(&slave->catch_lock);
					if (hdr->event_type != ROTATE_EVENT)
					{
						slave->binlog_pos = hdr->next_pos;
					}
					if (slave->overrun)
					{
						slave->stats.n_overrun++;
						slave->overrun = 0;
						poll_fake_write_event(slave->dcb);
					}
					else
					{
						slave->cstate &= ~CS_BUSY;
					}
					spinlock_release(&slave->catch_lock);
					break;

				case SLAVE_EVENT_ALREADY_SENT:
					spinlock_acquire(&slave->catch_lock);
					slave->cstate &= ~CS_BUSY;
					spinlock_release(&slave->catch_lock);
					break;

				case SLAVE_FORCE_CATCHUP:
					spinlock_acquire(&slave->catch_lock);
					cstate = slave->cstate;
					slave->cstate &= ~(CS_UPTODATE|CS_BUSY);
					slave->cstate |= CS_EXPECTCB;
					spinlock_release(&slave->catch_lock);
					if ((cstate & CS_UPTODATE) == CS_UPTODATE)
					{
#ifdef STATE_CHANGE_LOGGING_ENABLED
						MXS_NOTICE("%s: Slave %s:%d, server-id %d transition from up-to-date to catch-up in blr_distribute_binlog_record, binlog file '%s', position %lu.",
							router->service->name,
							slave->dcb->remote,
							ntohs((slave->dcb->ipv4).sin_port),
							slave->serverid,
							slave->binlogfile, (unsigned long)slave->binlog_pos);
#endif
					}
					poll_fake_write_event(slave->dcb);
					break;
			}
		}
		else if (action == 3)
		{
			/* Slave is not up to date
			 * Check if it is either expecting a callback or
			 * is busy processing a callback
			 */
			spinlock_acquire(&slave->catch_lock);
			if ((slave->cstate & (CS_EXPECTCB|CS_BUSY)) == 0)
			{
				slave->cstate |= CS_EXPECTCB;
				spinlock_release(&slave->catch_lock);
				poll_fake_write_event(slave->dcb);
			}
			else
				spinlock_release(&slave->catch_lock);
		}

		slave = slave->next;
	}
	spinlock_release(&router->lock);
}

/**
 * Write a raw event (the first 40 bytes at most) to a log file
 *
 * @param priority The syslog priority of the message (LOG_ERR, LOG_WARNING, etc.)
 * @param msg	   A textual message to write before the packet
 * @param ptr	   Pointer to the message buffer
 * @param len	   Length of message packet
 */
static void
blr_log_packet(int priority, char *msg, uint8_t *ptr, int len)
{
char	buf[400] = "";
char	*bufp;
int	i;

	bufp = buf;
	bufp += sprintf(bufp, "%s length = %d: ", msg, len);
	for (i = 0; i < len && i < 40; i++)
		bufp += sprintf(bufp, "0x%02x ", ptr[i]);
	if (i < len)
		MXS_LOG_MESSAGE(priority, "%s...", buf);
	else
		MXS_LOG_MESSAGE(priority, "%s", buf);
}

/**
 * Check if the master connection is in place and we
 * are downlaoding binlogs
 *
 * @param router	The router instance
 * @return non-zero if we are recivign binlog records
 */
int
blr_master_connected(ROUTER_INSTANCE *router)
{
	return router->master_state == BLRM_BINLOGDUMP;
}

/**
 * Extract a result value from the set of messages that make up a 
 * MySQL response packet.
 *
 * @param buf	The GWBUF containing the response
 * @param col	The column number to return
 * @return	The result form the column or NULL. The caller must free the result
 */
char *
blr_extract_column(GWBUF *buf, int col)
{
uint8_t	*ptr;
int	len, ncol, collen;
char	*rval;

	if (buf == NULL)
		return NULL;

	ptr = (uint8_t *)GWBUF_DATA(buf);
	/* First packet should be the column count */
	len = EXTRACT24(ptr);
	ptr += 3;
	if (*ptr != 1)		// Check sequence number is 1
		return NULL;
	ptr++;
	ncol = *ptr++;
	if (ncol < col)		// Not that many column in result
		return NULL;
	// Now ptr points at the column definition
	while (ncol-- > 0)
	{
		len = EXTRACT24(ptr);
		ptr += 4;	// Skip to payload
		ptr += len;	// Skip over payload
	}
	// Now we should have an EOF packet
	len = EXTRACT24(ptr);
	ptr += 4;		// Skip to payload
	if (*ptr != 0xfe)
		return NULL;
	ptr += len;

	// Finally we have reached the row
	len = EXTRACT24(ptr);
	ptr += 4;

    /** The first EOF packet signals the start of the resultset rows and the second
     EOF packet signals the end of the result set. If the resultset
     contains a second EOF packet right after the first one, the result set is empty and
     contains no rows. */
    if(len == 5 && *ptr == 0xfe)
        return NULL;

	while (--col > 0)
	{
		collen = *ptr++;
		ptr += collen;
	}
	collen = *ptr++;
	if ((rval = malloc(collen + 1)) == NULL)
		return NULL;
	memcpy(rval, ptr, collen);
	rval[collen] = 0;		// NULL terminate

	return rval;
}

/**
 * Read a replication event form current opened binlog into a GWBUF structure.
 *
 * @param router	The router instance
 * @param pos		Position of binlog record to read
 * @param hdr		Binlog header to populate
 * @return		The binlog record wrapped in a GWBUF structure
 */
GWBUF
*blr_read_events_from_pos(ROUTER_INSTANCE *router, unsigned long long pos, REP_HEADER *hdr, unsigned long long pos_end) {
unsigned long long end_pos = 0;
struct  stat    statb;
uint8_t         hdbuf[19];
uint8_t         *data;
GWBUF           *result;
int             n;
int		event_limit;

	/* Get current binnlog position */
	end_pos = pos_end;

	/* end of file reached, we're done */
	if (pos == end_pos) {
		return NULL;
	}

	/* error */
	if (pos > end_pos)
	{
        MXS_ERROR("Reading saved events, the specified pos %llu "
                  "is ahead of current pos %lu for file %s",
                  pos, router->current_pos, router->binlog_name);
		return NULL;
	}

	/* Read the event header information from the file */
	if ((n = pread(router->binlog_fd, hdbuf, 19, pos)) != 19)
	{
		switch (n)
		{
		case 0:
			MXS_DEBUG("Reading saved events: reached end of binlog file at %llu.", pos);
                        break;
                case -1:
			{
			char err_msg[STRERROR_BUFLEN];
			MXS_ERROR("Reading saved events: failed to read binlog "
                      "file %s at position %llu"
                      " (%s).", router->binlog_name,
                      pos, strerror_r(errno, err_msg, sizeof(err_msg)));

			if (errno == EBADF)
				MXS_ERROR("Reading saved events: bad file descriptor for file %s"
                          ", descriptor %d.",
                          router->binlog_name, router->binlog_fd);
			break;
			}
		default:
			MXS_ERROR("Reading saved events: short read when reading the header. "
                      "Expected 19 bytes but got %d bytes. "
                      "Binlog file is %s, position %llu",
                      n, router->binlog_name, pos);
			break;
		}

		return NULL;
	}

	hdr->timestamp = EXTRACT32(hdbuf);
	hdr->event_type = hdbuf[4];
	hdr->serverid = EXTRACT32(&hdbuf[5]);
	hdr->event_size = extract_field(&hdbuf[9], 32);
	hdr->next_pos = EXTRACT32(&hdbuf[13]);
	hdr->flags = EXTRACT16(&hdbuf[17]);

	event_limit = router->mariadb10_compat ? MAX_EVENT_TYPE_MARIADB10 : MAX_EVENT_TYPE;

	if (hdr->event_type > event_limit)
	{
		MXS_ERROR("Reading saved events: invalid event type 0x%x. "
                  "Binlog file is %s, position %llu",
                  hdr->event_type,
                  router->binlog_name, pos);
		return NULL;
	}

	if ((result = gwbuf_alloc(hdr->event_size)) == NULL)
	{
		MXS_ERROR("Reading saved events: failed to allocate memory for binlog entry, "
                  "size %d at %llu.",
                  hdr->event_size, pos);
		return NULL;
	}

	/* Copy event header*/
	data = GWBUF_DATA(result);
	memcpy(data, hdbuf, 19);

	/* Read event data and put int into buffer after header */
	if ((n = pread(router->binlog_fd, &data[19], hdr->event_size - 19, pos + 19)) != hdr->event_size - 19)
	{
		if (n == -1)
		{
			char err_msg[STRERROR_BUFLEN];
			MXS_ERROR("Reading saved events: the event at %llu in %s. "
                      "%s, expected %d bytes.",
                      pos, router->binlog_name,
                      strerror_r(errno, err_msg, sizeof(err_msg)), hdr->event_size - 19);
		} else {
			MXS_ERROR("Reading saved events: short read when reading "
                      "the event at %llu in %s. "
                      "Expected %d bytes got %d bytes.",
                      pos, router->binlog_name, hdr->event_size - 19, n);

                        if (end_pos - pos < hdr->event_size)
                        {
                            MXS_ERROR("Reading saved events: binlog event "
                                      "is close to the end of the binlog file, "
                                      "current file size is %llu.", end_pos);
			}
		}

		/* free buffer */
                gwbuf_free(result);

		return NULL;
	}

	return result;
}

/**
 * Stop and start the master connection
 *
 * @param router	The router instance
 */
void
blr_stop_start_master(ROUTER_INSTANCE *router) {

        if (router->master) {
                if (router->master->fd != -1 && router->master->state == DCB_STATE_POLLING) {
                        blr_master_close(router);
                }
        }

        spinlock_acquire(&router->lock);

        router->master_state = BLRM_SLAVE_STOPPED;

        /* set last_safe_pos */
        router->last_safe_pos = router->binlog_position;

        /**
         * Set router->prevbinlog to router->binlog_name
         * The FDE event with current filename may arrive after STOP SLAVE is received
         */

        if (strcmp(router->binlog_name, router->prevbinlog) != 0)
                strncpy(router->prevbinlog, router->binlog_name, BINLOG_FNAMELEN);

        if (router->client) {
                if (router->client->fd != -1 && router->client->state == DCB_STATE_POLLING) {
                        dcb_close(router->client);
                        router->client = NULL;
                }
        }

        /* Discard the queued residual data */
        while (router->residual)
        {
                router->residual = gwbuf_consume(router->residual, GWBUF_LENGTH(router->residual));
        }
        router->residual = NULL;

        router->master_state = BLRM_UNCONNECTED;
        spinlock_release(&router->lock);

        blr_master_reconnect(router);
}

/**
 * The heartbeat check function called from the housekeeper.
 * We can try a new master connection if current one is seen out of date
 *
 * @param router	Current router instance
 */

static void
blr_check_last_master_event(void *inst) {
ROUTER_INSTANCE *router = (ROUTER_INSTANCE *)inst;
int	master_check = 1;
int	master_state =  BLRM_UNCONNECTED;
char task_name[BLRM_TASK_NAME_LEN + 1] = "";

	spinlock_acquire(&router->lock);

	master_check = blr_check_heartbeat(router);

	master_state = router->master_state;

	spinlock_release(&router->lock);

	if (!master_check) {
		/*
		 * stop current master connection
		 * and try a new connection
		 */
		blr_stop_start_master(router);
	}

	if ( (!master_check) || (master_state != BLRM_BINLOGDUMP) ) {
		/*
		 * Remove the task, it will be added again
		 * when master state is back to BLRM_BINLOGDUMP
		 * by blr_master_response()
		 */
		snprintf(task_name, BLRM_TASK_NAME_LEN, "%s heartbeat", router->service->name);

		hktask_remove(task_name);
	}
}

/**
 * Check last heartbeat or last received event against router->heartbeat time interval
 *
 * checked interval is againts (router->heartbeat + BLR_NET_LATENCY_WAIT_TIME)
 * that is currently set to 1
 *
 * @param router	Current router instance
 * @return		0 if master connection must be closed and opened again, 1 otherwise
 */

int
blr_check_heartbeat(ROUTER_INSTANCE *router) {
time_t	t_now = time(0);
char 	*event_desc  = NULL;

	if (router->master_state != BLRM_BINLOGDUMP) {
		return 1;
	}

	event_desc = blr_last_event_description(router);

	if (router->master_state == BLRM_BINLOGDUMP && router->lastEventReceived > 0) {
		if ((t_now - router->stats.lastReply) > (router->heartbeat + BLR_NET_LATENCY_WAIT_TIME)) {
			 MXS_ERROR("No event received from master %s:%d in heartbeat period (%lu seconds), "
                       "last event (%s %d) received %lu seconds ago. Assuming connection is dead "
                       "and reconnecting.",
                       router->service->dbref->server->name,
                       router->service->dbref->server->port,
                       router->heartbeat,
                       event_desc != NULL ? event_desc : "unknown",
                       router->lastEventReceived,
                       t_now - router->stats.lastReply);

			return 0;
		}
	}

	return 1;
}

/**
 * Log binlog router identy after master registration, state is BLRM_BINLOG_DUMP
 *
 * @param	router	The router instance
 */

static void blr_log_identity(ROUTER_INSTANCE *router) {

	char *master_uuid;
	char *master_hostname;
	char *master_version;

	if (router->set_master_version)
		master_version  = router->set_master_version;
	else {
		master_version = blr_extract_column(router->saved_master.selectver, 1);
	}

	if (router->set_master_hostname)
		master_hostname  = router->set_master_hostname;
	else {
		master_hostname = blr_extract_column(router->saved_master.selecthostname, 1);
	}

	if (router->set_master_uuid)
		master_uuid = router->master_uuid;
	else {
		master_uuid = blr_extract_column(router->saved_master.uuid, 2);
	}

	/* Seen by the master */
	MXS_NOTICE("%s: identity seen by the master: "
               "server_id: %d, uuid: %s",
               router->service->name,
               router->serverid, (router->uuid == NULL ? "not available" : router->uuid));

	/* Seen by the slaves */

	/* MariaDB 5.5 and MariaDB don't have the MASTER_UUID var */
	if (master_uuid == NULL) {
		MXS_NOTICE("%s: identity seen by the slaves: "
                   "server_id: %d, hostname: %s, MySQL version: %s",
                   router->service->name,
                   router->masterid, (master_hostname == NULL ? "not available" : master_hostname),
                   (master_version == NULL ? "not available" : master_version));
	} else {
        	MXS_NOTICE("%s: identity seen by the slaves: "
                       "server_id: %d, uuid: %s, hostname: %s, MySQL version: %s",
                       router->service->name,
                       router->masterid, master_uuid,
                       (master_hostname == NULL ? "not available" : master_hostname),
                       (master_version == NULL ? "not available" : master_version));
	}
}

/**
 * Distribute an error message to all the registered slaves.
 *
 * @param	router		The router instance
 * @param	message		The message to send
 * @param	state		The MySQL State for message
 * @param	err_code	The MySQL error code for message
 */
static void
blr_distribute_error_message(ROUTER_INSTANCE *router, char *message, char *state, unsigned int err_code) {
ROUTER_SLAVE    *slave;

	spinlock_acquire(&router->lock);

	slave = router->slaves;
	while (slave)
	{
		if (slave->state != BLRS_DUMPING)
		{
			slave = slave->next;
			continue;
		}

		/* send the error that stops slave replication */
		blr_send_custom_error(slave->dcb, slave->seqno++, 0, message, state, err_code);

		slave = slave->next;
	}

	spinlock_release(&router->lock);
}

