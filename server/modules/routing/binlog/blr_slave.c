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
 * @file blr_slave.c - contains code for the router to slave communication
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
 * 14/04/2014	Mark Riddoch		Initial implementation
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
#include <spinlock.h>
#include <blr.h>
#include <dcb.h>
#include <spinlock.h>
#include <housekeeper.h>

#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

static uint32_t extract_field(uint8_t *src, int bits);
static void encode_value(unsigned char *data, unsigned int value, int len);
static int blr_slave_query(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *queue);
static int blr_slave_replay(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *master);
static void blr_slave_send_error(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, char  *msg);
static int blr_slave_send_timestamp(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave);
static int blr_slave_register(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *queue);
static int blr_slave_binlog_dump(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *queue);
int blr_slave_catchup(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, bool large);
uint8_t *blr_build_header(GWBUF	*pkt, REP_HEADER *hdr);
int blr_slave_callback(DCB *dcb, DCB_REASON reason, void *data);
static int blr_slave_fake_rotate(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave);

extern int lm_enabled_logfiles_bitmask;

/**
 * Process a request packet from the slave server.
 *
 * The router can handle a limited subset of requests from the slave, these
 * include a subset of general SQL queries, a slave registeration command and
 * the binlog dump command.
 *
 * The strategy for responding to these commands is to use caches responses
 * for the the same commands that have previously been made to the real master
 * if this is possible, if it is not then the router itself will synthesize a
 * response.
 *
 * @param router	The router instance this defines the master for this replication chain
 * @param slave		The slave specific data
 * @param queue		The incoming request packet
 */
int
blr_slave_request(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *queue)
{
	if (slave->state < 0 || slave->state > BLRS_MAXSTATE)
	{
        	LOGIF(LE, (skygw_log_write(
                           LOGFILE_ERROR, "Invalid slave state machine state (%d) for binlog router.",
					slave->state)));
		gwbuf_consume(queue, gwbuf_length(queue));
		return 0;
	}

	slave->stats.n_requests++;
	switch (MYSQL_COMMAND(queue))
	{
	case COM_QUERY:
		return blr_slave_query(router, slave, queue);
		break;
	case COM_REGISTER_SLAVE:
		return blr_slave_register(router, slave, queue);
		break;
	case COM_BINLOG_DUMP:
		return blr_slave_binlog_dump(router, slave, queue);
		break;
	case COM_QUIT:
		LOGIF(LD, (skygw_log_write(LOGFILE_DEBUG,
			"COM_QUIT received from slave with server_id %d",
				slave->serverid)));
		break;
	default:
        	LOGIF(LE, (skygw_log_write(
                           LOGFILE_ERROR,
			"Unexpected MySQL Command (%d) received from slave",
			MYSQL_COMMAND(queue))));	
		break;
	}
	return 0;
}

/**
 * Handle a query from the slave. This is expected to be one of the "standard"
 * queries we expect as part of the registraton process. Most of these can
 * be dealt with by replying the stored responses we got from the master
 * when MaxScale registered as a slave. The exception to the rule is the
 * request to obtain the current timestamp value of the server.
 *
 * Five select statements are currently supported:
 *	SELECT UNIX_TIMESTAMP();
 *	SELECT @master_binlog_checksum
 *	SELECT @@GLOBAL.GTID_MODE
 *	SELECT VERSION()
 *	SELECT 1
 *
 * Two show commands are supported:
 *	SHOW VARIABLES LIKE 'SERVER_ID'
 *	SHOW VARIABLES LIKE 'SERVER_UUID'
 *
 * Five set commands are supported:
 *	SET @master_binlog_checksum = @@global.binlog_checksum
 *	SET @master_heartbeat_period=...
 *	SET @slave_slave_uuid=...
 *	SET NAMES latin1
 *	SET NAMES utf8
 *
 * @param router        The router instance this defines the master for this replication chain
 * @param slave         The slave specific data
 * @param queue         The incoming request packet
 * @return		Non-zero if data has been sent
 */
static int
blr_slave_query(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *queue)
{
char	*qtext, *query_text;
char	*sep = " 	,=";
char	*word, *brkb;
int	query_len;

	qtext = GWBUF_DATA(queue);
	query_len = extract_field((uint8_t *)qtext, 24) - 1;
	qtext += 5;		// Skip header and first byte of the payload
	query_text = strndup(qtext, query_len);

	LOGIF(LT, (skygw_log_write(
		LOGFILE_TRACE, "Execute statement from the slave '%s'", query_text)));
	/*
	 * Implement a very rudimental "parsing" of the query text by extarcting the
	 * words from the statement and matchng them against the subset of queries we
	 * are expecting from the slave. We already have responses to these commands,
	 * except for the select of UNIX_TIMESTAMP(), that we have saved from MaxScale's
	 * own interaction with the real master. We simply replay these saved responses
	 * to the slave.
	 */
	word = strtok_r(query_text, sep, &brkb);
	if (strcasecmp(word, "SELECT") == 0)
	{
		word = strtok_r(NULL, sep, &brkb);
		if (strcasecmp(word, "UNIX_TIMESTAMP()") == 0)
		{
			free(query_text);
			return blr_slave_send_timestamp(router, slave);
		}
		else if (strcasecmp(word, "@master_binlog_checksum") == 0)
		{
			free(query_text);
			return blr_slave_replay(router, slave, router->saved_master.chksum2);
		}
		else if (strcasecmp(word, "@@GLOBAL.GTID_MODE") == 0)
		{
			free(query_text);
			return blr_slave_replay(router, slave, router->saved_master.gtid_mode);
		}
		else if (strcasecmp(word, "1") == 0)
		{
			free(query_text);
			return blr_slave_replay(router, slave, router->saved_master.select1);
		}
		else if (strcasecmp(word, "VERSION()") == 0)
		{
			free(query_text);
			return blr_slave_replay(router, slave, router->saved_master.selectver);
		}
	}
	else if (strcasecmp(word, "SHOW") == 0)
	{
		word = strtok_r(NULL, sep, &brkb);
		if (strcasecmp(word, "VARIABLES") == 0)
		{
			word = strtok_r(NULL, sep, &brkb);
			if (strcasecmp(word, "LIKE") == 0)
			{
				word = strtok_r(NULL, sep, &brkb);
				if (strcasecmp(word, "'SERVER_ID'") == 0)
				{
					free(query_text);
					return blr_slave_replay(router, slave, router->saved_master.server_id);
				}
				else if (strcasecmp(word, "'SERVER_UUID'") == 0)
				{
					free(query_text);
					return blr_slave_replay(router, slave, router->saved_master.uuid);
				}
			}
		}
	}
	else if (strcasecmp(query_text, "SET") == 0)
	{
		word = strtok_r(NULL, sep, &brkb);
		if (strcasecmp(word, "@master_heartbeat_period") == 0)
		{
			free(query_text);
			return blr_slave_replay(router, slave, router->saved_master.heartbeat);
		}
		else if (strcasecmp(word, "@master_binlog_checksum") == 0)
		{
			word = strtok_r(NULL, sep, &brkb);
			if (strcasecmp(word, "'none'") == 0)
				slave->nocrc = 1;
			else
				slave->nocrc = 0;
			free(query_text);
			return blr_slave_replay(router, slave, router->saved_master.chksum1);
		}
		else if (strcasecmp(word, "@slave_uuid") == 0)
		{
			free(query_text);
			return blr_slave_replay(router, slave, router->saved_master.setslaveuuid);
		}
		else if (strcasecmp(word, "NAMES") == 0)
		{
			word = strtok_r(NULL, sep, &brkb);
			if (strcasecmp(word, "latin1") == 0)
			{
				free(query_text);
				return blr_slave_replay(router, slave, router->saved_master.setnames);
			}
			else if (strcasecmp(word, "utf8") == 0)
			{
				free(query_text);
				return blr_slave_replay(router, slave, router->saved_master.utf8);
			}
		}
	}
	free(query_text);

	query_text = strndup(qtext, query_len);
	LOGIF(LE, (skygw_log_write(
		LOGFILE_ERROR, "Unexpected query from slave server %s", query_text)));
	free(query_text);
	blr_slave_send_error(router, slave, "Unexpected SQL query received from slave.");
	return 0;
}


/**
 * Send a reply to a command we have received from the slave. The reply itself
 * is merely a copy of a previous message we received from the master when we
 * registered as a slave. Hence we just replay this saved reply.
 *
 * @param	router		The binlog router instance
 * @param	slave		The slave server to which we are sending the response
 * @param	master		The saved master response
 * @return	Non-zero if data was sent
 */
static int
blr_slave_replay(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *master)
{
GWBUF	*clone;

	if (!master)
		return 0;
	if ((clone = gwbuf_clone(master)) != NULL)
	{
		return slave->dcb->func.write(slave->dcb, clone);
	}
	else
	{
		LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
			"Failed to clone server response to send to slave.")));
		return 0;
	}
}

/**
 * Construct an error response
 *
 * @param router	The router instance
 * @param slave		The slave server instance
 * @param msg		The error message to send
 */
static void
blr_slave_send_error(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, char  *msg)
{
GWBUF		*pkt;
unsigned char   *data;
int             len;

        if ((pkt = gwbuf_alloc(strlen(msg) + 13)) == NULL)
                return;
        data = GWBUF_DATA(pkt);
        len = strlen(msg) + 1;
        encode_value(&data[0], len, 24);	// Payload length
        data[3] = 0;				// Sequence id
						// Payload
        data[4] = 0xff;				// Error indicator
	data[5] = 0;				// Error Code
	data[6] = 0;				// Error Code
	strncpy((char *)&data[7], "#00000", 6);
        memcpy(&data[13], msg, strlen(msg));	// Error Message
	slave->dcb->func.write(slave->dcb, pkt);
}

/*
 * Some standard packets that have been captured from a network trace of server
 * interactions. These packets are the schema definition sent in response to
 * a SELECT UNIX_TIMESTAMP() statement and the EOF packet that marks the end
 * of transmission of the result set.
 */
static uint8_t timestamp_def[] = {
 0x01, 0x00, 0x00, 0x01, 0x01, 0x26, 0x00, 0x00, 0x02, 0x03, 0x64, 0x65, 0x66, 0x00, 0x00, 0x00,
 0x10, 0x55, 0x4e, 0x49, 0x58, 0x5f, 0x54, 0x49, 0x4d, 0x45, 0x53, 0x54, 0x41, 0x4d, 0x50, 0x28,
 0x29, 0x00, 0x0c, 0x3f, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x08, 0x81, 0x00, 0x00, 0x00, 0x00, 0x05,
 0x00, 0x00, 0x03, 0xfe, 0x00, 0x00, 0x02, 0x00
};
static uint8_t timestamp_eof[] = { 0x05, 0x00, 0x00, 0x05, 0xfe, 0x00, 0x00, 0x02, 0x00 };

/**
 * Send a response to a "SELECT UNIX_TIMESTAMP()" request. This differs from the other
 * requests since we do not save a copy of the original interaction with the master
 * and simply replay it. We want to always send the current time. We have stored a typcial
 * response, which gives us the schema information normally returned. This is sent to the
 * client and then we add a dynamic part that will insert the current timestamp data.
 * Finally we send a preprepaed EOF packet to end the response stream.
 *
 * @param	router		The binlog router instance
 * @param	slave		The slave server to which we are sending the response
 * @return	Non-zero if data was sent
 */
static int
blr_slave_send_timestamp(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave)
{
GWBUF	*pkt;
char	timestamp[20];
uint8_t *ptr;
int	len, ts_len;

	sprintf(timestamp, "%ld", time(0));
	ts_len = strlen(timestamp);
	len = sizeof(timestamp_def) + sizeof(timestamp_eof) + 5 + ts_len;
	if ((pkt = gwbuf_alloc(len)) == NULL)
		return 0;
	ptr = GWBUF_DATA(pkt);
	memcpy(ptr, timestamp_def, sizeof(timestamp_def));	// Fixed preamble
	ptr += sizeof(timestamp_def);
	encode_value(ptr, ts_len + 1, 24);			// Add length of data packet
	ptr += 3;
	*ptr++ = 0x04;						// Sequence number in response
	*ptr++ = ts_len;					// Length of result string
	strncpy((char *)ptr, timestamp, ts_len);		// Result string
	ptr += ts_len;
	memcpy(ptr, timestamp_eof, sizeof(timestamp_eof));	// EOF packet to terminate result
	return slave->dcb->func.write(slave->dcb, pkt);
}

/**
 * Process a slave replication registration message.
 *
 * We store the various bits of information the slave gives us and generate
 * a reply message.
 *
 * @param	router		The router instance
 * @param	slave		The slave server
 * @param	queue		The BINLOG_DUMP packet
 * @return			Non-zero if data was sent
 */
static int
blr_slave_register(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *queue)
{
GWBUF	*resp;
uint8_t	*ptr;
int	len, slen;

	ptr = GWBUF_DATA(queue);
	len = extract_field(ptr, 24);
	ptr += 4;		// Skip length and sequence number
	if (*ptr++ != COM_REGISTER_SLAVE)
		return 0;
	slave->serverid = extract_field(ptr, 32);
	ptr += 4;
	slen = *ptr++;
	if (slen != 0)
	{
		slave->hostname = strndup((char *)ptr, slen);
		ptr += slen;
	}
	else
		slave->hostname = NULL;
	slen = *ptr++;
	if (slen != 0)
	{
		ptr += slen;
		slave->user = strndup((char *)ptr, slen);
	}
	else
		slave->user = NULL;
	slen = *ptr++;
	if (slen != 0)
	{
		slave->passwd = strndup((char *)ptr, slen);
		ptr += slen;
	}
	else
		slave->passwd = NULL;
	slave->port = extract_field(ptr, 16);
	ptr += 2;
	slave->rank = extract_field(ptr, 32);

	/*
	 * Now construct a response
	 */
	if ((resp = gwbuf_alloc(11)) == NULL)
		return 0;
	ptr = GWBUF_DATA(resp);
	encode_value(ptr, 7, 24);	// Payload length
	ptr += 3;
	*ptr++ = 1;			// Sequence number
	encode_value(ptr, 0, 24);
	ptr += 3;
	encode_value(ptr, slave->serverid, 32);
	slave->state = BLRS_REGISTERED;
	return slave->dcb->func.write(slave->dcb, resp);
}

/**
 * Process a COM_BINLOG_DUMP message from the slave. This is the
 * final step in the process of registration. The new master, MaxScale
 * must send a response packet and generate a fake BINLOG_ROTATE event
 * with the binlog file requested by the slave. And then send a
 * FORMAT_DESCRIPTION_EVENT that has been saved from the real master.
 *
 * Once send MaxScale must continue to send binlog events to the slave.
 *
 * @param	router		The router instance
 * @param	slave		The slave server
 * @param	queue		The BINLOG_DUMP packet
 * @return			The number of bytes written to the slave
 */
static int
blr_slave_binlog_dump(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, GWBUF *queue)
{
GWBUF		*resp;
uint8_t		*ptr;
int		len, flags, serverid, rval, binlognamelen;
REP_HEADER	hdr;
uint32_t	chksum;

	ptr = GWBUF_DATA(queue);
	len = extract_field(ptr, 24);
	binlognamelen = len - 11;
	ptr += 4;		// Skip length and sequence number
	if (*ptr++ != COM_BINLOG_DUMP)
	{
        	LOGIF(LE, (skygw_log_write(
			LOGFILE_ERROR,
			"blr_slave_binlog_dump expected a COM_BINLOG_DUMP but received %d",
			*(ptr-1))));
		return 0;
	}

	slave->binlog_pos = extract_field(ptr, 32);
	ptr += 4;
	flags = extract_field(ptr, 16);
	ptr += 2;
	serverid = extract_field(ptr, 32);
	ptr += 4;
	strncpy(slave->binlogfile, (char *)ptr, binlognamelen);
	slave->binlogfile[binlognamelen] = 0;

	slave->seqno = 1;


	if (slave->nocrc)
		len = 19 + 8 + binlognamelen;
	else
		len = 19 + 8 + 4 + binlognamelen;

	// Build a fake rotate event
	resp = gwbuf_alloc(len + 5);
	hdr.payload_len = len + 1;
	hdr.seqno = slave->seqno++;
	hdr.ok = 0;
	hdr.timestamp = 0L;
	hdr.event_type = ROTATE_EVENT;
	hdr.serverid = router->masterid;
	hdr.event_size = len;
	hdr.next_pos = 0;
	hdr.flags = 0x20;
	ptr = blr_build_header(resp, &hdr);
	encode_value(ptr, slave->binlog_pos, 64);
	ptr += 8;
	memcpy(ptr, slave->binlogfile, binlognamelen);
	ptr += binlognamelen;

	if (!slave->nocrc)
	{
		/*
		 * Now add the CRC to the fake binlog rotate event.
		 *
		 * The algorithm is first to compute the checksum of an empty buffer
		 * and then the checksum of the event portion of the message, ie we do not
		 * include the length, sequence number and ok byte that makes up the first
		 * 5 bytes of the message. We also do not include the 4 byte checksum itself.
		 */
		chksum = crc32(0L, NULL, 0);
		chksum = crc32(chksum, GWBUF_DATA(resp) + 5, hdr.event_size - 4);
		encode_value(ptr, chksum, 32);
	}

	rval = slave->dcb->func.write(slave->dcb, resp);

	/* Send the FORMAT_DESCRIPTION_EVENT */
	if (router->saved_master.fde_event)
	{
		resp = gwbuf_alloc(router->saved_master.fde_len + 5);
		ptr = GWBUF_DATA(resp);
		encode_value(ptr, router->saved_master.fde_len + 1, 24); // Payload length
		ptr += 3;
		*ptr++ = slave->seqno++;
		*ptr++ = 0;		// OK
		memcpy(ptr, router->saved_master.fde_event, router->saved_master.fde_len);
		encode_value(ptr, time(0), 32);		// Overwrite timestamp
		/*
		 * Since we have changed the timestamp we must recalculate the CRC
		 *
		 * Position ptr to the start of the event header,
		 * calculate a new checksum
		 * and write it into the header
		 */
		ptr = GWBUF_DATA(resp) + 5 + router->saved_master.fde_len - 4;
		chksum = crc32(0L, NULL, 0);
		chksum = crc32(chksum, GWBUF_DATA(resp) + 5, router->saved_master.fde_len - 4);
		encode_value(ptr, chksum, 32);
		rval = slave->dcb->func.write(slave->dcb, resp);
	}

	slave->dcb->low_water  = router->low_water;
	slave->dcb->high_water = router->high_water;
	dcb_add_callback(slave->dcb, DCB_REASON_DRAINED, blr_slave_callback, slave);
	slave->state = BLRS_DUMPING;

	LOGIF(LM, (skygw_log_write(
		LOGFILE_MESSAGE,
			"%s: New slave %s requested binlog file %s from position %lu",
				router->service->name, slave->dcb->remote,
					slave->binlogfile, slave->binlog_pos)));

	if (slave->binlog_pos != router->binlog_position ||
			strcmp(slave->binlogfile, router->binlog_name) != 0)
	{
		spinlock_acquire(&slave->catch_lock);
		slave->cstate &= ~CS_UPTODATE;
		slave->cstate |= CS_EXPECTCB;
		spinlock_release(&slave->catch_lock);
		poll_fake_write_event(slave->dcb);
	}
	return rval;
}

/** 
 * Extract a numeric field from a packet of the specified number of bits,
 * the number of bits must be a multiple of 8.
 *
 * @param src	The raw packet source
 * @param bits	The number of bits to extract (multiple of 8)
 * @return 	The extracted value
 */
static uint32_t
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
 * Encode a value into a number of bits in a MySQL packet
 *
 * @param	data	Pointer to location in target packet
 * @param	value	The value to encode into the buffer
 * @param	len	Number of bits to encode value into
 */
static void
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
 * Populate a header structure for a replication message from a GWBUF structure.
 *
 * @param pkt	The incoming packet in a GWBUF chain
 * @param hdr	The packet header to populate
 * @return 	A pointer to the first byte following the event header
 */
uint8_t *
blr_build_header(GWBUF	*pkt, REP_HEADER *hdr)
{
uint8_t	*ptr;

	ptr = GWBUF_DATA(pkt);

	encode_value(ptr, hdr->payload_len, 24);
	ptr += 3;
	*ptr++ = hdr->seqno;
	*ptr++ = hdr->ok;
	encode_value(ptr, hdr->timestamp, 32);
	ptr += 4;
	*ptr++ = hdr->event_type;
	encode_value(ptr, hdr->serverid, 32);
	ptr += 4;
	encode_value(ptr, hdr->event_size, 32);
	ptr += 4;
	encode_value(ptr, hdr->next_pos, 32);
	ptr += 4;
	encode_value(ptr, hdr->flags, 16);
	ptr += 2;

	return ptr;
}

/**
 * We have a registered slave that is behind the current leading edge of the 
 * binlog. We must replay the log entries to bring this node up to speed.
 *
 * There may be a large number of records to send to the slave, the process
 * is triggered by the slave COM_BINLOG_DUMP message and all the events must
 * be sent without receiving any new event. This measn there is no trigger into
 * MaxScale other than this initial message. However, if we simply send all the
 * events we end up with an extremely long write queue on the DCB and risk
 * running the server out of resources.
 *
 * The slave catchup routine will send a burst of replication events per single
 * call. The paramter "long" control the number of events in the burst. The
 * short burst is intended to be used when the master receive an event and 
 * needs to put the slave into catchup mode. This prevents the slave taking
 * too much tiem away from the thread that is processing the master events.
 *
 * At the end of the burst a fake EPOLLOUT event is added to the poll event
 * queue. This ensures that the slave callback for processing DCB write drain
 * will be called and future catchup requests will be handled on another thread.
 *
 * @param	router		The binlog router
 * @param	slave		The slave that is behind
 * @param	large		Send a long or short burst of events
 * @return			The number of bytes written
 */
int
blr_slave_catchup(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, bool large)
{
GWBUF		*head, *record;
REP_HEADER	hdr;
int		written, rval = 1, burst;
int		rotating;
unsigned long	burst_size;
uint8_t		*ptr;

	if (large)
		burst = router->long_burst;
	else
		burst = router->short_burst;
	burst_size = router->burst_size;
	spinlock_acquire(&slave->catch_lock);
	if (slave->cstate & CS_BUSY)
	{
		spinlock_release(&slave->catch_lock);
		return 0;
	}
	slave->cstate |= CS_BUSY;
	spinlock_release(&slave->catch_lock);

	if (slave->file == NULL)
	{
		rotating = router->rotating;
		if ((slave->file = blr_open_binlog(router, slave->binlogfile)) == NULL)
		{
			if (rotating)
			{
				spinlock_acquire(&slave->catch_lock);
				slave->cstate |= CS_EXPECTCB;
				slave->cstate &= ~CS_BUSY;
				spinlock_release(&slave->catch_lock);
				poll_fake_write_event(slave->dcb);
				return rval;
			}
			LOGIF(LE, (skygw_log_write(
				LOGFILE_ERROR,
				"blr_slave_catchup failed to open binlog file %s",
					slave->binlogfile)));
			slave->cstate &= ~CS_BUSY;
			slave->state = BLRS_ERRORED;
			dcb_close(slave->dcb);
			return 0;
		}
	}
	slave->stats.n_bursts++;
	while (burst-- && burst_size > 0 &&
		(record = blr_read_binlog(router, slave->file, slave->binlog_pos, &hdr)) != NULL)
	{
		head = gwbuf_alloc(5);
		ptr = GWBUF_DATA(head);
		encode_value(ptr, hdr.event_size + 1, 24);
		ptr += 3;
		*ptr++ = slave->seqno++;
		*ptr++ = 0;		// OK
		head = gwbuf_append(head, record);
		if (hdr.event_type == ROTATE_EVENT)
		{
unsigned long beat1 = hkheartbeat;
			blr_close_binlog(router, slave->file);
if (hkheartbeat - beat1 > 1) LOGIF(LE, (skygw_log_write(
                                        LOGFILE_ERROR, "blr_close_binlog took %d beats",
				hkheartbeat - beat1)));
			blr_slave_rotate(slave, GWBUF_DATA(record));
beat1 = hkheartbeat;
			if ((slave->file = blr_open_binlog(router, slave->binlogfile)) == NULL)
			{
				if (rotating)
				{
					spinlock_acquire(&slave->catch_lock);
					slave->cstate |= CS_EXPECTCB;
					slave->cstate &= ~CS_BUSY;
					spinlock_release(&slave->catch_lock);
					poll_fake_write_event(slave->dcb);
					return rval;
				}
				LOGIF(LE, (skygw_log_write(
					LOGFILE_ERROR,
					"blr_slave_catchup failed to open binlog file %s",
					slave->binlogfile)));
				slave->state = BLRS_ERRORED;
				dcb_close(slave->dcb);
				break;
			}
if (hkheartbeat - beat1 > 1) LOGIF(LE, (skygw_log_write(
                                        LOGFILE_ERROR, "blr_open_binlog took %d beats",
				hkheartbeat - beat1)));
		}
		written = slave->dcb->func.write(slave->dcb, head);
		if (written && hdr.event_type != ROTATE_EVENT)
		{
			slave->binlog_pos = hdr.next_pos;
		}
		rval = written;
		slave->stats.n_events++;
		burst_size -= hdr.event_size;
	}
	if (record == NULL)
		slave->stats.n_failed_read++;
	spinlock_acquire(&slave->catch_lock);
	slave->cstate &= ~CS_BUSY;
	spinlock_release(&slave->catch_lock);

	if (record)
	{
		slave->stats.n_flows++;
		spinlock_acquire(&slave->catch_lock);
		slave->cstate |= CS_EXPECTCB;
		spinlock_release(&slave->catch_lock);
		poll_fake_write_event(slave->dcb);
	}
	else if (slave->binlog_pos == router->binlog_position &&
			strcmp(slave->binlogfile, router->binlog_name) == 0)
	{
		int state_change = 0;
		spinlock_acquire(&router->binlog_lock);
		spinlock_acquire(&slave->catch_lock);

		/*
		 * Now check again since we hold the router->binlog_lock
		 * and slave->catch_lock.
		 */
		if (slave->binlog_pos != router->binlog_position ||
			strcmp(slave->binlogfile, router->binlog_name) != 0)
		{
			slave->cstate &= ~CS_UPTODATE;
			slave->cstate |= CS_EXPECTCB;
			spinlock_release(&slave->catch_lock);
			spinlock_release(&router->binlog_lock);
			poll_fake_write_event(slave->dcb);
		}
		else
		{
			if ((slave->cstate & CS_UPTODATE) == 0)
			{
				slave->stats.n_upd++;
				slave->cstate |= CS_UPTODATE;
				spinlock_release(&slave->catch_lock);
				spinlock_release(&router->binlog_lock);
				state_change = 1;
			}
		}

		if (state_change)
		{
			LOGIF(LM, (skygw_log_write(LOGFILE_MESSAGE,
				"%s: Slave %s is up to date %s, %u.",
					router->service->name,
					slave->dcb->remote,
					slave->binlogfile, slave->binlog_pos)));
		}
	}
	else
	{
		if (slave->binlog_pos >= blr_file_size(slave->file)
				&& router->rotating == 0
				&& strcmp(router->binlog_name, slave->binlogfile) != 0
				&& blr_master_connected(router))
		{
			/* We may have reached the end of file of a non-current
			 * binlog file.
			 *
			 * Note if the master is rotating there is a window during
			 * which the rotate event has been written to the old binlog
			 * but the new binlog file has not yet been created. Therefore
			 * we ignore these issues during the rotate processing.
			 */
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"Slave reached end of file for binlong file %s at %u "
				"which is not the file currently being downloaded. "
				"Master binlog is %s, %lu.",
				slave->binlogfile, slave->binlog_pos,
				router->binlog_name, router->binlog_position)));
			if (blr_slave_fake_rotate(router, slave))
			{
				spinlock_acquire(&slave->catch_lock);
				slave->cstate |= CS_EXPECTCB;
				spinlock_release(&slave->catch_lock);
				poll_fake_write_event(slave->dcb);
			}
			else
			{
				slave->state = BLRS_ERRORED;
				dcb_close(slave->dcb);
			}
		}
		else
		{
			spinlock_acquire(&slave->catch_lock);
			slave->cstate |= CS_EXPECTCB;
			spinlock_release(&slave->catch_lock);
			poll_fake_write_event(slave->dcb);
		}
	}
	return rval;
}

/**
 * The DCB callback used by the slave to obtain DCB_REASON_LOW_WATER callbacks
 * when the server sends all the the queue data for a DCB. This is the mechanism
 * that is used to implement the flow control mechanism for the sending of
 * large quantities of binlog records during the catchup process.
 *
 * @param dcb		The DCB of the slave connection
 * @param reason	The reason the callback was called
 * @param data		The user data, in this case the server structure
 */
int
blr_slave_callback(DCB *dcb, DCB_REASON reason, void *data)
{
ROUTER_SLAVE		*slave = (ROUTER_SLAVE *)data;
ROUTER_INSTANCE		*router = slave->router;

	if (reason == DCB_REASON_DRAINED)
	{
		if (slave->state == BLRS_DUMPING)
		{
			spinlock_acquire(&slave->catch_lock);
			slave->cstate &= ~(CS_UPTODATE|CS_EXPECTCB);
			spinlock_release(&slave->catch_lock);
			slave->stats.n_dcb++;
			blr_slave_catchup(router, slave, true);
		}
		else
		{
        		LOGIF(LD, (skygw_log_write(
                           LOGFILE_DEBUG, "Ignored callback due to slave state %s",
					blrs_states[slave->state])));
		}
	}

	if (reason == DCB_REASON_LOW_WATER)
	{
		if (slave->state == BLRS_DUMPING)
		{
			slave->stats.n_cb++;
			blr_slave_catchup(router, slave, true);
		}
		else
		{
			slave->stats.n_cbna++;
		}
	}
	return 0;
}

/**
 * Rotate the slave to the new binlog file
 *
 * @param slave 	The slave instance
 * @param ptr		The rotate event (minus header and OK byte)
 */
void
blr_slave_rotate(ROUTER_SLAVE *slave, uint8_t *ptr)
{
int	len = EXTRACT24(ptr + 9);	// Extract the event length

	len = len - (19 + 8 + 4);	// Remove length of header, checksum and position
	if (len > BINLOG_FNAMELEN)
		len = BINLOG_FNAMELEN;
	ptr += 19;	// Skip header
	slave->binlog_pos = extract_field(ptr, 32);
	slave->binlog_pos += (extract_field(ptr+4, 32) << 32);
	memcpy(slave->binlogfile, ptr + 8, len);
	slave->binlogfile[len] = 0;
}

/**
 *  Generate an internal rotate event that we can use to cause the slave to move beyond
 * a binlog file that is misisng the rotate eent at the end.
 *
 * @param router	The router instance
 * @param slave		The slave to rotate
 * @return  Non-zero if the rotate took place
 */
static int
blr_slave_fake_rotate(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave)
{
char		*sptr;
int		filenum;
GWBUF		*resp;
uint8_t		*ptr;
int		len, binlognamelen;
REP_HEADER	hdr;
uint32_t	chksum;

	if ((sptr = strrchr(slave->binlogfile, '.')) == NULL)
		return 0;
	blr_close_binlog(router, slave->file);
	filenum = atoi(sptr + 1);
	sprintf(slave->binlogfile, BINLOG_NAMEFMT, router->fileroot, filenum + 1);
	slave->binlog_pos = 4;
	if ((slave->file = blr_open_binlog(router, slave->binlogfile)) == NULL)
		return 0;

	binlognamelen = strlen(slave->binlogfile);

	if (slave->nocrc)
		len = 19 + 8 + binlognamelen;
	else
		len = 19 + 8 + 4 + binlognamelen;

	// Build a fake rotate event
	resp = gwbuf_alloc(len + 5);
	hdr.payload_len = len + 1;
	hdr.seqno = slave->seqno++;
	hdr.ok = 0;
	hdr.timestamp = 0L;
	hdr.event_type = ROTATE_EVENT;
	hdr.serverid = router->masterid;
	hdr.event_size = len;
	hdr.next_pos = 0;
	hdr.flags = 0x20;
	ptr = blr_build_header(resp, &hdr);
	encode_value(ptr, slave->binlog_pos, 64);
	ptr += 8;
	memcpy(ptr, slave->binlogfile, binlognamelen);
	ptr += binlognamelen;

	if (!slave->nocrc)
	{
		/*
		 * Now add the CRC to the fake binlog rotate event.
		 *
		 * The algorithm is first to compute the checksum of an empty buffer
		 * and then the checksum of the event portion of the message, ie we do not
		 * include the length, sequence number and ok byte that makes up the first
		 * 5 bytes of the message. We also do not include the 4 byte checksum itself.
		 */
		chksum = crc32(0L, NULL, 0);
		chksum = crc32(chksum, GWBUF_DATA(resp) + 5, hdr.event_size - 4);
		encode_value(ptr, chksum, 32);
	}

	slave->dcb->func.write(slave->dcb, resp);
	return 1;
}
