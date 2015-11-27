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
 * Copyright MariaDB Corporation Ab 2014-2015
 */

/**
 * @file blr.h - The binlog router header file
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 02/04/14	Mark Riddoch		Initial implementation
 * 25/05/15	Massimiliano Pinto	Added BLRM_SLAVE_STOPPED state
 * 05/06/15	Massimiliano Pinto	Addition of m_errno, m_errmsg fields
 * 08/06/15	Massimiliano Pinto	Modification of MYSQL_ERROR_CODE and MYSQL_ERROR_MSG
 * 11/05/15	Massimiliano Pinto	Added mariadb10_compat to master and slave structs
 * 12/06/15	Massimiliano Pinto	Added mariadb10 new events
 * 23/06/15	Massimiliano Pinto	Addition of MASTER_SERVER_CFG struct
 * 24/06/15	Massimiliano Pinto	Added BLRM_UNCONFIGURED state
 * 05/08/15	Massimiliano Pinto	Initial implementation of transaction safety
 * 23/10/15	Markus Makela		Added current_safe_event
 *
 * @endverbatim
 */
#include <dcb.h>
#include <buffer.h>
#include <pthread.h>
#include <stdint.h>
#include <memlog.h>
#include <zlib.h>

#define BINLOG_FNAMELEN		16
#define BLR_PROTOCOL		"MySQLBackend"
#define BINLOG_MAGIC		{ 0xfe, 0x62, 0x69, 0x6e }
#define BINLOG_NAMEFMT		"%s.%06d"
#define BINLOG_NAME_ROOT	"mysql-bin"

#define BINLOG_EVENT_HDR_LEN	19

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

#define MAX_EVENT_TYPE				0x23

/* New MariaDB event numbers start from 0xa0 */
#define MARIADB_NEW_EVENTS_BEGIN		0xa0
#define MARIADB_ANNOTATE_ROWS_EVENT		0xa0
/* New MariaDB 10 event numbers start from here */
#define MARIADB10_BINLOG_CHECKPOINT_EVENT	0xa1
#define MARIADB10_GTID_EVENT			0xa2
#define MARIADB10_GTID_GTID_LIST_EVENT		0xa3

#define MAX_EVENT_TYPE_MARIADB10		0xa3

/* Maximum event type so far */
#define MAX_EVENT_TYPE_END			MAX_EVENT_TYPE_MARIADB10

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

/**
 * How often to call the binlog status function (seconds)
 */
#define	BLR_STATS_FREQ		60
#define BLR_NSTATS_MINUTES	30

/**
 * High and Low water marks for the slave dcb. These values can be overriden
 * by the router options highwater and lowwater.
 */
#define DEF_LOW_WATER		1000
#define	DEF_HIGH_WATER		10000

/**
 * Default burst sizes for slave catchup
 */
#define DEF_SHORT_BURST		15
#define DEF_LONG_BURST		500
#define DEF_BURST_SIZE		1024000	/* 1 Mb */

/**
 * master reconnect backoff constants
 * BLR_MASTER_BACKOFF_TIME	The increments of the back off time (seconds)
 * BLR_MAX_BACKOFF		Maximum number of increments to backoff to
 */
#define	BLR_MASTER_BACKOFF_TIME	10
#define BLR_MAX_BACKOFF		60

/* max size for error message returned to client */
#define BINLOG_ERROR_MSG_LEN	385

/* network latency extra wait tme for heartbeat check */
#define BLR_NET_LATENCY_WAIT_TIME	1

/* default heartbeat interval in seconds */
#define BLR_HEARTBEAT_DEFAULT_INTERVAL	300

/* strings and numbers in SQL replies */
#define BLR_TYPE_STRING			0xf
#define BLR_TYPE_INT			0x03

/* string len for COM_STATISTICS output */
#define BLRM_COM_STATISTICS_SIZE	1000

/* string len for strerror_r message */
#define BLRM_STRERROR_R_MSG_SIZE	128

/* string len for task message name */
#define BLRM_TASK_NAME_LEN		80

/* string len for temp binlog filename  */
#define BLRM_BINLOG_NAME_STR_LEN	80

/* string len for temp binlog filename  */
#define BLRM_SET_HEARTBEAT_QUERY_LEN	80

/* string len for master registration query  */
#define BLRM_MASTER_REGITRATION_QUERY_LEN	255

/* Read Binlog position states */
#define SLAVE_POS_READ_OK			0x00
#define SLAVE_POS_READ_ERR			0xff
#define SLAVE_POS_READ_UNSAFE			0xfe
#define SLAVE_POS_BAD_FD			0xfd

/**
 * Some useful macros for examining the MySQL Response packets
 */
#define MYSQL_RESPONSE_OK(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 4) == 0x00)
#define MYSQL_RESPONSE_EOF(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 4) == 0xfe)
#define MYSQL_RESPONSE_ERR(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 4) == 0xff)
#define MYSQL_ERROR_CODE(buf)	((uint8_t *)GWBUF_DATA(buf) + 5)
#define MYSQL_ERROR_MSG(buf)	((uint8_t *)GWBUF_DATA(buf) + 7)
#define MYSQL_COMMAND(buf)	(*((uint8_t *)GWBUF_DATA(buf) + 4))

/* Master Server configuration struct */
typedef struct master_server_config {
	char *host;
	unsigned short port;
	char logfile[BINLOG_FNAMELEN+1];
	uint64_t pos;
	uint64_t safe_pos;
	char *user;
	char *password;
	char *filestem;
} MASTER_SERVER_CFG;

/* Config struct for CHANGE MASTER TO options */
typedef struct change_master_options {
	char *host;
	char *port;
	char *binlog_file;
	char *binlog_pos;
	char *user;
	char *password;
} CHANGE_MASTER_OPTIONS;

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
 * The binlog record structure. This contains the actual packet read from the binlog
 * file.
 */
typedef struct {
	unsigned long	position;	/*< binlog record position for this cache entry */
	GWBUF		*pkt;		/*< The packet received from the master */
	REP_HEADER	hdr;		/*< The packet header */
} BLCACHE_RECORD;

/**
 * The binlog cache. A cache exists for each file that hold cached bin log records.
 * Caches will be used for all files being read by more than 1 slave.
 */
typedef struct {
	BLCACHE_RECORD	**records;	/*< The actual binlog records */
	int		current;	/*< The next record that will be inserted */
	int		cnt;		/*< The number of records in the cache */
	SPINLOCK	lock;		/*< The spinlock for the cache */
} BLCACHE;

typedef struct blfile {
	char		binlogname[BINLOG_FNAMELEN+1];	/*< Name of the binlog file */
	int		fd;				/*< Actual file descriptor */
	int		refcnt;				/*< Reference count for file */
	BLCACHE		*cache;				/*< Record cache for this file */
	SPINLOCK	lock;				/*< The file lock */
	struct blfile	*next;				/*< Next file in list */
} BLFILE;

/**
 * Slave statistics
 */
typedef struct {
	int		n_events;	/*< Number of events sent */
	unsigned long	n_bytes;	/*< Number of bytes sent */
	int		n_bursts;	/*< Number of bursts sent */
	int		n_requests;	/*< Number of requests received */
	int		n_flows;	/*< Number of flow control restarts */
	int		n_queries;	/*< Number of SQL queries */
	int		n_upd;
	int		n_cb;
	int		n_cbna;
	int		n_dcb;
	int		n_above;
	int		n_failed_read;
	int		n_overrun;
	int		n_caughtup;
	int		n_actions[3];
	uint64_t	lastsample;
	int		minno;
	int		minavgs[BLR_NSTATS_MINUTES];
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
	uint32_t	binlog_pos;	/*< Binlog position for this slave */
	char		binlogfile[BINLOG_FNAMELEN+1];
					/*< Current binlog file for this slave */
	char		*uuid;		/*< Slave UUID */
	BLFILE		*file;		/*< Currently open binlog file */
	int		serverid;	/*< Server-id of the slave */
	char		*hostname;	/*< Hostname of the slave, if known */
	char		*user;		/*< Username if given */
	char		*passwd;	/*< Password if given */
	short		port;		/*< MySQL port */
	int		nocrc;		/*< Disable CRC */
	int		overrun;
	uint32_t	rank;		/*< Replication rank */
	uint8_t		seqno;		/*< Replication dump sequence no */
	uint32_t	lastEventTimestamp;/*< Last event timestamp sent */
	SPINLOCK	catch_lock;	/*< Event catchup lock */
	unsigned int	cstate;		/*< Catch up state */
	bool            mariadb10_compat;/*< MariaDB 10.0 compatibility */
        SPINLOCK        rses_lock;	/*< Protects rses_deleted */
	pthread_t	pthread;
	struct router_instance
			*router;	/*< Pointer to the owning router */
	struct router_slave *next;
	SLAVE_STATS	stats;		/*< Slave statistics */
	time_t		connect_time;	/*< Connect time of slave */
	char		*warning_msg;	/*< Warning message */
	int		heartbeat;	/*< Heartbeat in seconds */
	uint8_t		lastEventReceived; /*< Last event received */
	time_t		lastReply;	/*< Last event sent */
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
	uint64_t	n_binlogs_ses;	/*< Number of binlog records from master */
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
	int		n_badcrc;	/*< No. of bad CRC's from master */
	uint64_t	events[MAX_EVENT_TYPE_END + 1];	/*< Per event counters */
	uint64_t	lastsample;
	int		minno;
	int		minavgs[BLR_NSTATS_MINUTES];
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
	GWBUF		*selectvercom;	/*< select @@version_comment */
	GWBUF		*selecthostname;/*< select @@hostname */
	GWBUF		*map;		/*< select @@max_allowed_packet */
	GWBUF		*mariadb10;	/*< set @mariadb_slave_capability */
	uint8_t		*fde_event;	/*< Format Description Event */
	int		fde_len;	/*< Length of fde_event */
} MASTER_RESPONSES;

/**
 * The per instance data for the router.
 */
typedef struct router_instance {
	SERVICE			*service;	/*< Pointer to the service using this router */
	ROUTER_SLAVE		*slaves;	/*< Link list of all the slave connections  */
	SPINLOCK		lock;	        /*< Spinlock for the instance data */
	char			*uuid;		/*< UUID for the router to use w/master */
	int			masterid;	/*< Set ID of the master, sent to slaves */
	int			serverid;	/*< ID for the router to use w/master */
	int			initbinlog;	/*< Initial binlog file number */
	char			*user;		/*< User name to use with master */
	char			*password;	/*< Password to use with master */
	char			*fileroot;	/*< Root of binlog filename */
	bool			master_chksum;	/*< Does the master provide checksums */
	bool			mariadb10_compat; /*< MariaDB 10.0 compatibility */
	char			*master_uuid;	/*< Set UUID of the master, sent to slaves */
	DCB			*master;	/*< DCB for master connection */
	DCB			*client;	/*< DCB for dummy client */
	SESSION			*session;	/*< Fake session for master connection */
	unsigned int		master_state;	/*< State of the master FSM */
	uint8_t			lastEventReceived; /*< Last even received */
	uint32_t		lastEventTimestamp; /*< Timestamp from last event */
	GWBUF	 		*residual;	/*< Any residual binlog event */
	MASTER_RESPONSES	saved_master;	/*< Saved master responses */
	char			*binlogdir;	/*< The directory with the binlog files */
	SPINLOCK		binlog_lock;	/*< Lock to control update of the binlog position */
	int			trx_safe;	/*< Detect and handle partial transactions */
	int			pending_transaction; /*< Pending transaction */
	uint64_t		last_safe_pos; /* last committed transaction */
	char			binlog_name[BINLOG_FNAMELEN+1];
					/*< Name of the current binlog file */
	uint64_t		binlog_position;
					/*< last committed transaction position */
	uint64_t		current_pos;
					/*< Current binlog position */
	int			binlog_fd;	/*< File descriptor of the binlog
					 *  file being written
					 */
	uint64_t	  last_written;	/*< Position of last event written */
	uint64_t	  current_safe_event;
	/*< Position of the latest safe event being sent to slaves */
	char		  prevbinlog[BINLOG_FNAMELEN+1];
	int		  rotating;	/*< Rotation in progress flag */
	BLFILE		  *files;	/*< Files used by the slaves */
	SPINLOCK	  fileslock;	/*< Lock for the files queue above */
	unsigned int	  low_water;	/*< Low water mark for client DCB */
	unsigned int	  high_water;	/*< High water mark for client DCB */
	unsigned int	  short_burst;	/*< Short burst for slave catchup */
	unsigned int	  long_burst;	/*< Long burst for slave catchup */
	unsigned long	  burst_size;	/*< Maximum size of burst to send */
	unsigned long	  heartbeat;	/*< Configured heartbeat value */
	ROUTER_STATS	  stats;	/*< Statistics for this router */
	int		  active_logs;
	int		  reconnect_pending;
	int		  retry_backoff;
	time_t		  connect_time;
	int		  handling_threads;
	unsigned long	  m_errno;	/*< master response mysql errno */
	char		  *m_errmsg;	/*< master response mysql error message */
	char		  *set_master_version; /*< Send custom Version to slaves */
	char		  *set_master_hostname; /*< Send custom Hostname to slaves */
	char		  *set_master_uuid; /*< Send custom Master UUID to slaves */
	char		  *set_master_server_id; /*< Send custom Master server_id to slaves */
	int		  send_slave_heartbeat; /*< Enable sending heartbeat to slaves */
	struct router_instance	*next;
} ROUTER_INSTANCE;

/**
 * State machine for the master to MaxScale replication
 */
#define BLRM_UNCONFIGURED	0x0000
#define BLRM_UNCONNECTED	0x0001
#define BLRM_CONNECTING		0x0002
#define	BLRM_AUTHENTICATED	0x0003
#define BLRM_TIMESTAMP		0x0004
#define BLRM_SERVERID		0x0005
#define BLRM_HBPERIOD		0x0006
#define BLRM_CHKSUM1		0x0007
#define BLRM_CHKSUM2		0x0008
#define BLRM_GTIDMODE		0x0009
#define BLRM_MUUID		0x000A
#define BLRM_SUUID		0x000B
#define	BLRM_LATIN1		0x000C
#define	BLRM_UTF8		0x000D
#define	BLRM_SELECT1		0x000E
#define	BLRM_SELECTVER		0x000F
#define BLRM_SELECTVERCOM	0x0010
#define BLRM_SELECTHOSTNAME	0x0011
#define BLRM_MAP		0x0012
#define	BLRM_REGISTER		0x0013
#define	BLRM_BINLOGDUMP		0x0014
#define	BLRM_SLAVE_STOPPED	0x0015
#define	BLRM_MARIADB10		0x0016

#define BLRM_MAXSTATE		0x0016

static char *blrm_states[] = { "Unconfigured", "Unconnected", "Connecting", "Authenticated", "Timestamp retrieval",
	"Server ID retrieval", "HeartBeat Period setup", "binlog checksum config",
	"binlog checksum rerieval", "GTID Mode retrieval", "Master UUID retrieval",
	"Set Slave UUID", "Set Names latin1", "Set Names utf8", "select 1",
	"select version()", "select @@version_comment", "select @@hostname",
	"select @@max_allowed_packet", "Register slave", "Binlog Dump", "Slave stopped", "Set MariaDB slave capability" };

#define BLRS_CREATED		0x0000
#define BLRS_UNREGISTERED	0x0001
#define BLRS_REGISTERED		0x0002
#define BLRS_DUMPING		0x0003
#define BLRS_ERRORED		0x0004

#define BLRS_MAXSTATE		0x0004

static char *blrs_states[] = { "Created", "Unregistered", "Registered",
	"Sending binlogs", "Errored" };

/**
 * Slave catch-up status
 */
#define CS_UPTODATE		0x0004
#define CS_EXPECTCB		0x0008
#define	CS_DIST			0x0010
#define	CS_DISTLATCH		0x0020
#define	CS_THRDWAIT		0x0040
#define CS_BUSY			0x0100
#define CS_HOLD			0x0200

/**
 * MySQL protocol OpCodes needed for replication
 */
#define	COM_QUIT				0x01
#define	COM_QUERY				0x03
#define	COM_STATISTICS				0x09
#define	COM_PING				0x0e
#define COM_REGISTER_SLAVE			0x15
#define COM_BINLOG_DUMP				0x12

/**
 * Macros to extract common fields
 */
#define INLINE_EXTRACT		1	/* Set to 0 for debug purposes */

#if INLINE_EXTRACT
#define	EXTRACT16(x)		(*(uint8_t *)(x) | (*((uint8_t *)(x) + 1) << 8))
#define	EXTRACT24(x)		(*(uint8_t *)(x) | \
					(*((uint8_t *)(x) + 1) << 8) | \
					(*((uint8_t *)(x) + 2) << 16))
#define	EXTRACT32(x)		(*(uint8_t *)(x) | \
					(*((uint8_t *)(x) + 1) << 8) | \
					(*((uint8_t *)(x) + 2) << 16) | \
					(*((uint8_t *)(x) + 3) << 24))
#else
#define	EXTRACT16(x)		extract_field((x), 16)
#define	EXTRACT24(x)		extract_field((x), 24)
#define	EXTRACT32(x)		extract_field((x), 32)
#endif

/*
 * Externals within the router
 */
extern void blr_start_master(void *);
extern void blr_master_response(ROUTER_INSTANCE *, GWBUF *);
extern void blr_master_reconnect(ROUTER_INSTANCE *);
extern int blr_master_connected(ROUTER_INSTANCE *);

extern int blr_slave_request(ROUTER_INSTANCE *, ROUTER_SLAVE *, GWBUF *);
extern void blr_slave_rotate(ROUTER_INSTANCE *, ROUTER_SLAVE *, uint8_t *);
extern int blr_slave_catchup(ROUTER_INSTANCE *router, ROUTER_SLAVE *slave, bool large);
extern void blr_init_cache(ROUTER_INSTANCE *);

extern int  blr_file_init(ROUTER_INSTANCE *);
extern int  blr_write_binlog_record(ROUTER_INSTANCE *, REP_HEADER *,uint8_t *);
extern int  blr_file_rotate(ROUTER_INSTANCE *, char *, uint64_t);
extern void blr_file_flush(ROUTER_INSTANCE *);
extern BLFILE *blr_open_binlog(ROUTER_INSTANCE *, char *);
extern GWBUF *blr_read_binlog(ROUTER_INSTANCE *, BLFILE *, unsigned long, REP_HEADER *, char *);
extern void blr_close_binlog(ROUTER_INSTANCE *, BLFILE *);
extern unsigned long blr_file_size(BLFILE *);
extern int blr_statistics(ROUTER_INSTANCE *, ROUTER_SLAVE *, GWBUF *);
extern int blr_ping(ROUTER_INSTANCE *, ROUTER_SLAVE *, GWBUF *);
extern int blr_send_custom_error(DCB *, int, int, char *, char *, unsigned int);
extern int blr_file_next_exists(ROUTER_INSTANCE *, ROUTER_SLAVE *);
uint32_t extract_field(uint8_t *src, int bits);
#endif
