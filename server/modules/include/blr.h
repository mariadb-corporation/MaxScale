#ifndef _BLR_H
#define _BLR_H
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
 * @file blr.h - The binlog router header file
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 02/04/14	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <dcb.h>
#include <buffer.h>
#include <pthread.h>

#define BINLOG_FNAMELEN		16
#define BLR_PROTOCOL		"MySQLBackend"
#define BINLOG_MAGIC		{ 0xfe, 0x62, 0x69, 0x6e }
#define BINLOG_NAMEFMT		"%s.%06d"
#define BINLOG_NAME_ROOT	"mysql-bin"

/**
 * High and Low water marks for the slave dcb. These values can be overriden
 * by the router options highwater and lowwater.
 */
#define DEF_LOW_WATER		2000
#define	DEF_HIGH_WATER		30000

/**
 * Some useful macros for examining the MySQL Response packets
 */
#define MYSQL_RESPONSE_OK(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 4) == 0x00)
#define MYSQL_RESPONSE_EOF(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 4) == 0xfe)
#define MYSQL_RESPONSE_ERR(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 4) == 0xff)
#define MYSQL_ERROR_CODE(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 5))
#define MYSQL_ERROR_MSG(buf)	((uint8_t *)GWBUF_DATA(buf) + 6)
#define MYSQL_COMMAND(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 4))

/**
 * Slave statistics
 */
typedef struct {
	int	n_events;	/*< Number of events sent */
	int	n_bursts;	/*< Number of bursts sent */
	int	n_requests;	/*< Number of requests received */
	int	n_flows;	/*< Number of flow control restarts */
	int	n_catchupnr;	/*< No. of times catchup resulted in not entering loop */
	int	n_alreadyupd;
	int	n_upd;
	int	n_cb;
	int	n_cbna;
	int	n_dcb;
	int	n_above;
	int	n_failed_read;
	int	n_overrun;
	int	n_actions[3];
} SLAVE_STATS;

/**
 * The client session structure used within this router. This represents
 * the slaves that are replicating binlogs from MaxScale.
 */
typedef struct router_slave {
#if defined(SS_DEBUG)
        skygw_chk_t     rses_chk_top;
#endif
	DCB		*dcb;		/*< The slave server DCB */
	int		state;		/*< The state of this slave */
	int		binlog_pos;	/*< Binlog position for this slave */
	char		binlogfile[BINLOG_FNAMELEN+1];
					/*< Current binlog file for this slave */
	int		serverid;	/*< Server-id of the slave */
	char		*hostname;	/*< Hostname of the slave, if known */
	char		*user;		/*< Username if given */
	char		*passwd;	/*< Password if given */
	short		port;		/*< MySQL port */
	int		nocrc;		/*< Disable CRC */
	int		overrun;
	uint32_t	rank;		/*< Replication rank */
	uint8_t		seqno;		/*< Replication dump sequence no */
	SPINLOCK	catch_lock;	/*< Event catchup lock */
	unsigned int	cstate;		/*< Catch up state */
        SPINLOCK        rses_lock;	/*< Protects rses_deleted */
	pthread_t	pthread;
	struct router_instance
			*router;	/*< Pointer to the owning router */
	struct router_slave *next;
	SLAVE_STATS	stats;		/*< Slave statistics */
#if defined(SS_DEBUG)
        skygw_chk_t     rses_chk_tail;
#endif
} ROUTER_SLAVE;


/**
 * The statistics for this router instance
 */
typedef struct {
	int		n_slaves;	/*< Number slave sessions created     */
	int		n_reads;	/*< Number of record reads */
	uint64_t	n_binlogs;	/*< Number of binlog records from master */
	uint64_t	n_binlog_errors;/*< Number of binlog records from master */
	uint64_t	n_rotates;	/*< Number of binlog rotate events */
	uint64_t	n_cachehits;	/*< Number of hits on the binlog cache */
	uint64_t	n_cachemisses;	/*< Number of misses on the binlog cache */
	int		n_registered;	/*< Number of registered slaves */
	int		n_masterstarts;	/*< Number of times connection restarted */
	int		n_delayedreconnects;
	int		n_residuals;	/*< Number of times residual data was buffered */
	int		n_heartbeats;	/*< Number of heartbeat messages */
	time_t		lastReply;
	uint64_t	n_fakeevents;	/*< Fake events not written to disk */
	uint64_t	n_artificial;	/*< Artificial events not written to disk */
	uint64_t	events[0x24];	/*< Per event counters */
} ROUTER_STATS;

/**
 * Saved responses from the master that will be forwarded to slaves
 */
typedef struct {
	GWBUF		*server_id;	/*< Master server id */
	GWBUF		*heartbeat;	/*< Heartbeat period */
	GWBUF		*chksum1;	/*< Binlog checksum 1st response */
	GWBUF		*chksum2;	/*< Binlog checksum 2nd response */
	GWBUF		*gtid_mode;	/*< GTID Mode response */
	GWBUF		*uuid;		/*< Master UUID */
	GWBUF		*setslaveuuid;	/*< Set Slave UUID */
	GWBUF		*setnames;	/*< Set NAMES latin1 */
	GWBUF		*utf8;		/*< Set NAMES utf8 */
	GWBUF		*select1;	/*< select 1 */
	GWBUF		*selectver;	/*< select version() */
	uint8_t		*fde_event;	/*< Format Description Event */
	int		fde_len;	/*< Length of fde_event */
} MASTER_RESPONSES;

/**
 * The binlog record structure. This contains the actual packet received from the
 * master, the binlog position of the data in the packet, a point to the data and
 * the length of the binlog record.
 *
 * This allows requests for binlog records in the cache to be serviced by simply
 * sending the exact same packet as was received by MaxScale from the master.
 * Items are written to the backing file as soon as they are received. The binlog
 * cache is flushed of old records periodically, releasing the GWBUF's back to the
 * free memory pool.
 */
typedef struct {
	unsigned long	position;	/*< binlog record position for this cache entry */
	GWBUF		*pkt;		/*< The packet received from the master */
	unsigned char	*data;		/*< Pointer to the data within the packet */
	unsigned int	record_len;	/*< Binlog record length */
} BLCACHE_RECORD;

/**
 * The binlog cache. A cache exists for each file that hold cached bin log records.
 * Typically the router will hold two binlog caches, one for the current file and one
 * for the previous file.
 */
typedef struct {
	char		filename[BINLOG_FNAMELEN+1];
	BLCACHE_RECORD	*first;
	BLCACHE_RECORD	*current;
	int		cnt;
} BLCACHE;


/**
 * The per instance data for the router.
 */
typedef struct router_instance {
	SERVICE		  *service;     /*< Pointer to the service using this router */
	ROUTER_SLAVE	  *slaves;	/*< Link list of all the slave connections  */
	SPINLOCK	  lock;	        /*< Spinlock for the instance data */
	char		  *uuid;	/*< UUID for the router to use w/master */
	int		  masterid;	/*< Server ID of the master */
	int		  serverid;	/*< Server ID to use with master */
	int		  initbinlog;	/*< Initial binlog file number */
	char		  *user;	/*< User name to use with master */
	char		  *password;	/*< Password to use with master */
	char		  *fileroot;	/*< Root of binlog filename */
	DCB		  *master;	/*< DCB for master connection */
	DCB		  *client;	/*< DCB for dummy client */
	SESSION		  *session;	/*< Fake session for master connection */
	unsigned int	  master_state;	/*< State of the master FSM */
	uint8_t		  lastEventReceived;
	GWBUF	 	  *residual;	/*< Any residual binlog event */
	MASTER_RESPONSES  saved_master;	/*< Saved master responses */
	char		  binlog_name[BINLOG_FNAMELEN+1];
					/*< Name of the current binlog file */
	uint64_t	  binlog_position;
					/*< Current binlog position */
	int		  binlog_fd;	/*< File descriptor of the binlog
					 *  file being written
					 */
	unsigned int	  low_water;	/*< Low water mark for client DCB */
	unsigned int	  high_water;	/*< High water mark for client DCB */
	BLCACHE	  	  *cache[2];
	ROUTER_STATS	  stats;	/*< Statistics for this router */
	int		  active_logs;
	int		  reconnect_pending;
	int		  handling_threads;
	struct router_instance
                          *next;
} ROUTER_INSTANCE;

/**
 * Packet header for replication messages
 */
typedef struct rep_header {
	int		payload_len;	/*< Payload length (24 bits) */
	uint8_t		seqno;		/*< Response sequence number */
	uint8_t		ok;		/*< OK Byte from packet */
	uint32_t	timestamp;	/*< Timestamp - start of binlog record */
	uint8_t		event_type;	/*< Binlog event type */
	uint32_t	serverid;	/*< Server id of master */
	uint32_t	event_size;	/*< Size of header, post-header and body */
	uint32_t	next_pos;	/*< Position of next event */
	uint16_t	flags;		/*< Event flags */
} REP_HEADER;

/**
 * State machine for the master to MaxScale replication
 */
#define BLRM_UNCONNECTED	0x0000
#define	BLRM_AUTHENTICATED	0x0001
#define BLRM_TIMESTAMP		0x0002
#define BLRM_SERVERID		0x0003
#define BLRM_HBPERIOD		0x0004
#define BLRM_CHKSUM1		0x0005
#define BLRM_CHKSUM2		0x0006
#define BLRM_GTIDMODE		0x0007
#define BLRM_MUUID		0x0008
#define BLRM_SUUID		0x0009
#define	BLRM_LATIN1		0x000A
#define	BLRM_UTF8		0x000B
#define	BLRM_SELECT1		0x000C
#define	BLRM_SELECTVER		0x000D
#define	BLRM_REGISTER		0x000E
#define	BLRM_BINLOGDUMP		0x000F

#define BLRM_MAXSTATE		0x000F

static char *blrm_states[] = { "Unconnected", "Authenticated", "Timestamp retrieval",
	"Server ID retrieval", "HeartBeat Period setup", "binlog checksum config",
	"binlog checksum rerieval", "GTID Mode retrieval", "Master UUID retrieval",
	"Set Slave UUID", "Set Names latin1", "Set Names utf8", "select 1",
	"select version()", "Register slave", "Binlog Dump" };

#define BLRS_CREATED		0x0000
#define BLRS_UNREGISTERED	0x0001
#define BLRS_REGISTERED		0x0002
#define BLRS_DUMPING		0x0003

#define BLRS_MAXSTATE		0x0003

static char *blrs_states[] = { "Created", "Unregistered", "Registered",
	"Sending binlogs" };

/**
 * Slave catch-up status
 */
#define CS_READING		0x0001
#define CS_INNERLOOP		0x0002
#define CS_UPTODATE		0x0004
#define CS_EXPECTCB		0x0008
#define	CS_DIST			0x0010
#define	CS_DISTLATCH		0x0020

/**
 * MySQL protocol OpCodes needed for replication
 */
#define	COM_QUIT				0x01
#define	COM_QUERY				0x03
#define COM_REGISTER_SLAVE			0x15
#define COM_BINLOG_DUMP				0x12

/**
 * Binlog event types
 */
#define START_EVENT_V3				0x01
#define QUERY_EVENT				0x02
#define STOP_EVENT				0x03
#define ROTATE_EVENT				0x04
#define INTVAR_EVENT				0x05
#define LOAD_EVENT				0x06
#define SLAVE_EVENT				0x07
#define CREATE_FILE_EVENT			0x08
#define APPEND_BLOCK_EVENT			0x09
#define EXEC_LOAD_EVENT				0x0A
#define DELETE_FILE_EVENT			0x0B
#define NEW_LOAD_EVENT				0x0C
#define RAND_EVENT				0x0D
#define USER_VAR_EVENT				0x0E
#define FORMAT_DESCRIPTION_EVENT		0x0F
#define XID_EVENT				0x10
#define BEGIN_LOAD_QUERY_EVENT			0x11
#define EXECUTE_LOAD_QUERY_EVENT		0x12
#define TABLE_MAP_EVENT				0x13
#define WRITE_ROWS_EVENTv0			0x14
#define UPDATE_ROWS_EVENTv0			0x15
#define DELETE_ROWS_EVENTv0			0x16
#define WRITE_ROWS_EVENTv1			0x17
#define UPDATE_ROWS_EVENTv1			0x18
#define DELETE_ROWS_EVENTv1			0x19
#define INCIDENT_EVENT				0x1A
#define HEARTBEAT_EVENT				0x1B
#define IGNORABLE_EVENT				0x1C
#define ROWS_QUERY_EVENT			0x1D
#define WRITE_ROWS_EVENTv2			0x1E
#define UPDATE_ROWS_EVENTv2			0x1F
#define DELETE_ROWS_EVENTv2			0x20
#define GTID_EVENT				0x21
#define ANONYMOUS_GTID_EVENT			0x22
#define PREVIOUS_GTIDS_EVENT			0x23

/**
 * Binlog event flags
 */
#define LOG_EVENT_BINLOG_IN_USE_F		0x0001
#define LOG_EVENT_FORCED_ROTATE_F		0x0002
#define LOG_EVENT_THREAD_SPECIFIC_F		0x0004
#define LOG_EVENT_SUPPRESS_USE_F		0x0008
#define LOG_EVENT_UPDATE_TABLE_MAP_VERSION_F	0x0010
#define LOG_EVENT_ARTIFICIAL_F			0x0020
#define LOG_EVENT_RELAY_LOG_F			0x0040
#define LOG_EVENT_IGNORABLE_F			0x0080
#define LOG_EVENT_NO_FILTER_F			0x0100
#define LOG_EVENT_MTS_ISOLATE_F			0x0200

/*
 * Externals within the router
 */
extern void blr_start_master(ROUTER_INSTANCE *);
extern void blr_master_response(ROUTER_INSTANCE *, GWBUF *);
extern void blr_master_reconnect(ROUTER_INSTANCE *);

extern int blr_slave_request(ROUTER_INSTANCE *, ROUTER_SLAVE *, GWBUF *);
extern void blr_slave_rotate(ROUTER_SLAVE *slave, uint8_t *ptr);
extern int blr_slave_catchup(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave);
extern void blr_init_cache(ROUTER_INSTANCE *);

extern void blr_file_init(ROUTER_INSTANCE *);
extern int blr_open_binlog(ROUTER_INSTANCE *, char *);
extern void blr_write_binlog_record(ROUTER_INSTANCE *, REP_HEADER *,uint8_t *);
extern void blr_file_rotate(ROUTER_INSTANCE *, char *, uint64_t);
extern void blr_file_flush(ROUTER_INSTANCE *);
extern GWBUF *blr_read_binlog(int, unsigned int, REP_HEADER *);
#endif
