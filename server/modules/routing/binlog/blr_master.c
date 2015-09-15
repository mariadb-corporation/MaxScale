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
 * Copyright MariaDB Corporation Ab 2014
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


extern int lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static GWBUF *blr_make_query(char *statement);
static GWBUF *blr_make_registration(ROUTER_INSTANCE *router);
static GWBUF *blr_make_binlog_dump(ROUTER_INSTANCE *router);
void encode_value(unsigned char *data, unsigned int value, int len);
void blr_handle_binlog_record(ROUTER_INSTANCE *router, GWBUF *pkt);
static int  blr_rotate_event(ROUTER_INSTANCE *router, uint8_t *pkt, REP_HEADER *hdr);
void blr_distribute_binlog_record(ROUTER_INSTANCE *router, REP_HEADER *hdr, uint8_t *ptr);
static void *CreateMySQLAuthData(char *username, char *password, char *database);
void blr_extract_header(uint8_t *pkt, REP_HEADER *hdr);
static void blr_log_packet(logfile_id_t file, char *msg, uint8_t *ptr, int len);
static void blr_master_close(ROUTER_INSTANCE *);
static char *blr_extract_column(GWBUF *buf, int col);
void blr_cache_response(ROUTER_INSTANCE *router, char *response, GWBUF *buf);
void poll_fake_write_event(DCB *dcb);
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
GWBUF	*buf;

	router->stats.n_binlogs_ses = 0;
	spinlock_acquire(&router->lock);
	if (router->master_state != BLRM_UNCONNECTED)
	{
		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
			"%s: Master Connect: Unexpected master state %s\n",
			router->service->name, blrm_states[router->master_state])));
		spinlock_release(&router->lock);
		return;
	}
	router->master_state = BLRM_CONNECTING;

	/* Discard the queued residual data */
	buf = router->residual;
	while (buf)
	{
		buf = gwbuf_consume(buf, GWBUF_LENGTH(buf));
	}
	router->residual = NULL;

	spinlock_release(&router->lock);
	if ((client = dcb_alloc(DCB_ROLE_INTERNAL)) == NULL)
	{
		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
			"Binlog router: failed to create DCB for dummy client")));
		return;
	}
	router->client = client;
	client->state = DCB_STATE_POLLING;	/* Fake the client is reading */
	client->data = CreateMySQLAuthData(router->user, router->password, "");
	if ((router->session = session_alloc(router->service, client)) == NULL)
	{
		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
			"Binlog router: failed to create session for connection to master")));
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
		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
		   "Binlog router: failed to connect to master server '%s'",
			router->service->dbref->server->unique_name)));
		return;
	}
	router->master->remote = strdup(router->service->dbref->server->name);
        LOGIF(LM,(skygw_log_write(
                        LOGFILE_MESSAGE,
				"%s: attempting to connect to master server %s.",
			router->service->name, router->service->dbref->server->name)));
	router->connect_time = time(0);

if (setsockopt(router->master->fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive , sizeof(keepalive )))
perror("setsockopt");

	router->master_state = BLRM_AUTHENTICATED;
	buf = blr_make_query("SELECT UNIX_TIMESTAMP()");
	router->master->func.write(router->master, buf);
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
GWBUF	*ptr;

	dcb_close(router->client);

	/* Discard the queued residual data */
	ptr = router->residual;
	while (ptr)
	{
		ptr = gwbuf_consume(ptr, GWBUF_LENGTH(ptr));
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
char	query[128];

	atomic_add(&router->handling_threads, 1);
	ss_dassert(router->handling_threads == 1);
	spinlock_acquire(&router->lock);
	router->active_logs = 1;
	spinlock_release(&router->lock);
	if (router->master_state < 0 || router->master_state > BLRM_MAXSTATE)
	{
        	LOGIF(LE, (skygw_log_write(
			LOGFILE_ERROR,
			"Invalid master state machine state (%d) for binlog router.",
			router->master_state)));
		gwbuf_consume(buf, gwbuf_length(buf));

		spinlock_acquire(&router->lock);
		if (router->reconnect_pending)
		{
			router->active_logs = 0;
			spinlock_release(&router->lock);
			atomic_add(&router->handling_threads, -1);
	        	LOGIF(LE, (skygw_log_write(
				LOGFILE_ERROR,
				"%s: Pending reconnect in state %s.",
				router->service->name,
				blrm_states[router->master_state]
				)));
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
        	LOGIF(LE, (skygw_log_write(
                           LOGFILE_ERROR,
			"%s: Master server does not support GTID Mode.",
				router->service->name)));
	}
	else if (router->master_state != BLRM_BINLOGDUMP && MYSQL_RESPONSE_ERR(buf))
	{
        	LOGIF(LE, (skygw_log_write(
                           LOGFILE_ERROR,
			"%s: Received error: %d, %s from master during %s phase "
			"of the master state machine.",
			router->service->name,
			MYSQL_ERROR_CODE(buf), MYSQL_ERROR_MSG(buf),
			blrm_states[router->master_state]
			)));
		gwbuf_consume(buf, gwbuf_length(buf));
		spinlock_acquire(&router->lock);
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
		char str[80];
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
		char *val = blr_extract_column(buf, 2);
		router->master_uuid = val;

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
        	LOGIF(LM,(skygw_log_write(
                           LOGFILE_MESSAGE,
				"%s: Request binlog records from %s at "
				"position %d from master server %s.",
			router->service->name, router->binlog_name,
			router->binlog_position, router->service->dbref->server->name)));
		break;
	case BLRM_BINLOGDUMP:
		// Main body, we have received a binlog record from the master
		blr_handle_binlog_record(router, buf);
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
		router->binlog_position, 32);		// binlog position
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
        			LOGIF(LE,(skygw_log_write(
		                           LOGFILE_ERROR,
					"Insufficient memory to buffer event "
					"of %d bytes. Binlog %s @ %d.",
					len, router->binlog_name,
					router->binlog_position)));
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
        			LOGIF(LE,(skygw_log_write(
		                           LOGFILE_ERROR,
					"Expected entire message in buffer "
					"chain, but failed to create complete "
					"message as expected. %s @ %d",
					router->binlog_name,
					router->binlog_position)));
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
	        	LOGIF(LD,(skygw_log_write(
                           LOGFILE_DEBUG,
			   "Residual data left after %d records. %s @ %d",
					router->stats.n_binlogs,
			   router->binlog_name, router->binlog_position)));
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
			LOGIF(LM,(skygw_log_write(
				   LOGFILE_MESSAGE,
					"Non-event message (%s) from master.",
					msg)));
		}
		else
		{
			router->stats.n_binlogs++;
			router->stats.n_binlogs_ses++;
			router->lastEventReceived = hdr.event_type;

			blr_extract_header(ptr, &hdr);

			if (hdr.event_size != len - 5)	/* Sanity check */
			{
				LOGIF(LE,(skygw_log_write(
				   LOGFILE_ERROR,
					"Packet length is %d, but event size is %d, "
					"binlog file %s position %d "
					"reslen is %d and preslen is %d, "
					"length of previous event %d. %s",
						len, hdr.event_size,
						router->binlog_name,
						router->binlog_position,
						reslen, preslen, prev_length,
					(prev_length == -1 ?
					(no_residual ? "No residual data from previous call" : "Residual data from previous call") : "")
					)));
				blr_log_packet(LOGFILE_ERROR, "Packet:", ptr, len);
				LOGIF(LE,(skygw_log_write(
				   LOGFILE_ERROR,
					"This event (0x%x) was contained in %d GWBUFs, "
					"the previous events was contained in %d GWBUFs",
					router->lastEventReceived, n_bufs, pn_bufs)));
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
						LOGIF(LE,(skygw_log_write(LOGFILE_ERROR,
							"%s: Checksum error in event "
							"from master, "
							"binlog %s @ %d. "
							"Closing master connection.",
							router->service->name,
							router->binlog_name,
							router->binlog_position)));
						blr_master_close(router);
						blr_master_delayed_connect(router);
						return;
					}
				}

				router->lastEventReceived = hdr.event_type;
				router->lastEventTimestamp = hdr.timestamp;

// #define SHOW_EVENTS
#ifdef SHOW_EVENTS
				printf("blr: event type 0x%02x, flags 0x%04x, event size %d", hdr.event_type, hdr.flags, hdr.event_size);
#endif
				event_limit = router->mariadb10_compat ? MAX_EVENT_TYPE_MARIADB10 : MAX_EVENT_TYPE;

				if (hdr.event_type >= 0 && hdr.event_type <= event_limit)
					router->stats.events[hdr.event_type]++;

				if (hdr.event_type == FORMAT_DESCRIPTION_EVENT && hdr.next_pos == 0)
				{
					// Fake format description message
					LOGIF(LD,(skygw_log_write(LOGFILE_DEBUG,
						"Replication fake event. "
							"Binlog %s @ %d.",
						router->binlog_name,
						router->binlog_position)));
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
							LOGIF(LE,(skygw_log_write(LOGFILE_ERROR,
								"%s: Received a format description "
								"event that MaxScale was unable to "
								"record. Event length is %d.",
								router->service->name,
								hdr.event_size)));
							blr_log_packet(LOGFILE_ERROR,
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
						LOGIF(LD,(skygw_log_write(
							   LOGFILE_DEBUG,
							"Replication heartbeat. "
							"Binlog %s @ %d.",
							router->binlog_name,
							router->binlog_position)));
						router->stats.n_heartbeats++;
					}
					else if (hdr.flags != LOG_EVENT_ARTIFICIAL_F)
					{
						ptr = ptr + 5;	// We don't put the first byte of the payload
								// into the binlog file
						if (hdr.event_type == ROTATE_EVENT)
							router->rotating = 1;
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
						blr_distribute_binlog_record(router, &hdr, ptr);
					}
					else
					{
						router->stats.n_artificial++;
						LOGIF(LD,(skygw_log_write(
							   LOGFILE_DEBUG,
						"Artificial event not written "
						"to disk or distributed. "
						"Type 0x%x, Length %d, Binlog "
						"%s @ %d.",
							hdr.event_type,
							hdr.event_size,
							router->binlog_name,
							router->binlog_position)));
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
				LOGIF(LE,(skygw_log_write(LOGFILE_ERROR,
					"Error packet in binlog stream.%s @ %d.",
							router->binlog_name,
							router->binlog_position)));
				blr_log_packet(LOGFILE_ERROR, "Error Packet:",
					ptr, len);
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
        	LOGIF(LE,(skygw_log_write(
                           LOGFILE_ERROR,
				"You must specify both username and password for the binlog router.\n")));
		return NULL;
	}

	if ((auth_info = calloc(1, sizeof(MYSQL_session))) == NULL)
		return NULL;
	strncpy(auth_info->user, username,MYSQL_USER_MAXLEN);
	strncpy(auth_info->db, database,MYSQL_DATABASE_MAXLEN);
	gw_sha1_str((const uint8_t *)password, strlen(password), auth_info->client_sha1);

	return auth_info;
}

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
			if (slave->binlog_pos == router->last_written &&
				(strcmp(slave->binlogfile, router->binlog_name) == 0 ||
				(hdr->event_type == ROTATE_EVENT &&
				strcmp(slave->binlogfile, router->prevbinlog))))
			{
				/*
				 * The slave should be up to date, check that the binlog
				 * position matches the event we have to distribute or
				 * this is a rotate event. Send the event directly from
				 * memory to the slave.
				 */
				slave->lastEventTimestamp = hdr->timestamp;
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
				if (hdr->event_type != ROTATE_EVENT)
				{
					slave->binlog_pos = hdr->next_pos;
				}
				spinlock_acquire(&slave->catch_lock);
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
			}
			else if (slave->binlog_pos == hdr->next_pos
				&& strcmp(slave->binlogfile, router->binlog_name) == 0)
			{
				/*
				 * Slave has already read record from file, no
				 * need to distrbute this event
				 */
				spinlock_acquire(&slave->catch_lock);
				slave->cstate &= ~CS_BUSY;
				spinlock_release(&slave->catch_lock);
			}
			else if ((slave->binlog_pos > hdr->next_pos - hdr->event_size)
				&& strcmp(slave->binlogfile, router->binlog_name) == 0)
			{
				/*
				 * The slave is ahead of the master, this should never
				 * happen. Force the slave to catchup mode in order to
				 * try to resolve the issue.
				 */
				LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
					"Slave %d is ahead of expected position %s@%d. "
					"Expected position %d",
						slave->serverid, slave->binlogfile,
						(unsigned long)slave->binlog_pos,
						hdr->next_pos - hdr->event_size)));
				spinlock_acquire(&slave->catch_lock);
				slave->cstate &= ~(CS_UPTODATE|CS_BUSY);
				slave->cstate |= CS_EXPECTCB;
				spinlock_release(&slave->catch_lock);
				poll_fake_write_event(slave->dcb);
			}
			else
			{
				/*
				 * The slave is not at the position it should be. Force it into
				 * catchup mode rather than send this event.
				 */
				spinlock_acquire(&slave->catch_lock);
				slave->cstate &= ~(CS_UPTODATE|CS_BUSY);
				slave->cstate |= CS_EXPECTCB;
				spinlock_release(&slave->catch_lock);
				poll_fake_write_event(slave->dcb);
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
 * @param file	The logfile to write to
 * @param msg	A textual message to write before the packet
 * @param ptr	Pointer to the message buffer
 * @param len	Length of message packet
 */
static void
blr_log_packet(logfile_id_t file, char *msg, uint8_t *ptr, int len)
{
char	buf[400], *bufp;
int	i;

	bufp = buf;
	bufp += sprintf(bufp, "%s length = %d: ", msg, len);
	for (i = 0; i < len && i < 40; i++)
		bufp += sprintf(bufp, "0x%02x ", ptr[i]);
	if (i < len)
		skygw_log_write_flush(file, "%s...", buf);
	else
		skygw_log_write_flush(file, "%s", buf);
	
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
static char *
blr_extract_column(GWBUF *buf, int col)
{
uint8_t	*ptr;
int	len, ncol, collen;
char	*rval;

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
