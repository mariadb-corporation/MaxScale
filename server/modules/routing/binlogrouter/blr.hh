#pragma once
#ifndef _BLR_H
#define _BLR_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file blr.h - The binlog router header file
 */

#define MXS_MODULE_NAME "binlogrouter"

#include <maxscale/ccdefs.hh>

#include <openssl/aes.h>
#include <pthread.h>
#include <stdint.h>
#include <zlib.h>

#include <string>
#include <thread>
#include <vector>

#include <maxscale/buffer.h>
#include <maxscale/dcb.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/router.h>
#include <maxscale/secrets.h>
#include <maxscale/service.h>
#include <maxscale/sqlite3.h>
#include <maxscale/mysql_binlog.h>

#define BINLOG_FNAMELEN         255
#define BLR_PROTOCOL            "MySQLBackend"
#define BINLOG_MAGIC            { 0xfe, 0x62, 0x69, 0x6e }
#define BINLOG_MAGIC_SIZE       4
#define BINLOG_NAMEFMT          "%s.%06d"
#define BINLOG_NAME_ROOT        "mysql-bin"

#define BINLOG_EVENT_HDR_LEN       19
#define BINLOG_EVENT_CRC_ALGO_TYPE  1
#define BINLOG_EVENT_CRC_SIZE       4
/* BINLOG_EVENT_LEN_OFFSET points to event_size in event_header */
#define BINLOG_EVENT_LEN_OFFSET     9
#define BINLOG_FATAL_ERROR_READING  1236
#define BINLOG_DATA_TRUNCATED       2032

/* Binlog Encryption */
#define BINLOG_ENC_ALGO_NAME_LEN           13
#define BINLOG_FLAG_ENCRYPT                 1
#define BINLOG_FLAG_DECRYPT                 0
#define BINLOG_AES_MAX_KEY_LEN             32
#define BINLOG_MAX_CRYPTO_SCHEME            2
#define BINLOG_SYSTEM_DATA_CRYPTO_SCHEME    1
#define BINLOG_MAX_KEYFILE_LINE_LEN       130

/* Event detail routine */
#define BLR_REPORT_CHECKSUM_FORMAT "CRC32 0x"
#define BLR_REPORT_REP_HEADER            0x02
#define BLR_CHECK_ONLY                   0x04

/* GTID slite3 query buffer size */
#define GTID_SQL_BUFFER_SIZE 1024

/* GTID slite3 database name */
#define GTID_MAPS_DB "gtid_maps.db"

/* Number of reties for a missing binlog file */
#define MISSING_FILE_READ_RETRIES 20
/**
 * Add GTID components domain and serverid as name prefix
 * in SHOW FULL BINARY LOGS
 */
#define BINLOG_FILE_EXTRA_INFO GTID_MAX_LEN

/* Default MariaDB GTID Domain Id */
#define BLR_DEFAULT_GTID_DOMAIN_ID        0

enum binlog_storage_type
{
    BLR_BINLOG_STORAGE_FLAT,
    BLR_BINLOG_STORAGE_TREE
};

/** Conecting slave checks */
enum blr_slave_check
{
    BLR_SLAVE_CONNECTING,          /*< The slave starts the registration */
    BLR_SLAVE_IS_MARIADB10,        /*< The slave is a MariaDB10 one */
    BLR_SLAVE_HAS_MARIADB10_GTID,  /*< The MariaDB10 Slave has GTID request */
};

/**
 * Supported Encryption algorithms
 *
 * Note: AES_ECB is only internally used
 * Available algorithms for binlog files
 * Encryption/Decryption are AES_CBC and AES_CTR
 */
enum blr_aes_mode
{
    BLR_AES_CBC,
    BLR_AES_CTR,
    BLR_AES_ECB
};

/* Default encryption alogorithm is AES_CBC */
#define BINLOG_DEFAULT_ENC_ALGO    BLR_AES_CBC

/**
 * Binlog event types
 */
#define START_EVENT_V3                          0x01
#define QUERY_EVENT                             0x02
#define STOP_EVENT                              0x03
#define ROTATE_EVENT                            0x04
#define INTVAR_EVENT                            0x05
#define LOAD_EVENT                              0x06
#define SLAVE_EVENT                             0x07
#define CREATE_FILE_EVENT                       0x08
#define APPEND_BLOCK_EVENT                      0x09
#define EXEC_LOAD_EVENT                         0x0A
#define DELETE_FILE_EVENT                       0x0B
#define NEW_LOAD_EVENT                          0x0C
#define RAND_EVENT                              0x0D
#define USER_VAR_EVENT                          0x0E
#define FORMAT_DESCRIPTION_EVENT                0x0F
#define XID_EVENT                               0x10
#define BEGIN_LOAD_QUERY_EVENT                  0x11
#define EXECUTE_LOAD_QUERY_EVENT                0x12
#define TABLE_MAP_EVENT                         0x13
#define WRITE_ROWS_EVENTv0                      0x14
#define UPDATE_ROWS_EVENTv0                     0x15
#define DELETE_ROWS_EVENTv0                     0x16
#define WRITE_ROWS_EVENTv1                      0x17
#define UPDATE_ROWS_EVENTv1                     0x18
#define DELETE_ROWS_EVENTv1                     0x19
#define INCIDENT_EVENT                          0x1A
#define HEARTBEAT_EVENT                         0x1B
#define IGNORABLE_EVENT                         0x1C
#define ROWS_QUERY_EVENT                        0x1D
#define WRITE_ROWS_EVENTv2                      0x1E
#define UPDATE_ROWS_EVENTv2                     0x1F
#define DELETE_ROWS_EVENTv2                     0x20
#define GTID_EVENT                              0x21
#define ANONYMOUS_GTID_EVENT                    0x22
#define PREVIOUS_GTIDS_EVENT                    0x23

#define MAX_EVENT_TYPE                          0x23

/* New MariaDB event numbers start from 0xa0 */
#define MARIADB_NEW_EVENTS_BEGIN                0xa0
#define MARIADB_ANNOTATE_ROWS_EVENT             0xa0
/* New MariaDB 10 event numbers start from here */
#define MARIADB10_BINLOG_CHECKPOINT_EVENT       0xa1
#define MARIADB10_GTID_EVENT                    0xa2
#define MARIADB10_GTID_GTID_LIST_EVENT          0xa3
#define MARIADB10_START_ENCRYPTION_EVENT        0xa4

#define MAX_EVENT_TYPE_MARIADB10                0xa4

/* Maximum event type so far */
#define MAX_EVENT_TYPE_END                      MAX_EVENT_TYPE_MARIADB10

/**
 * Binlog event flags
 */
#define LOG_EVENT_BINLOG_IN_USE_F               0x0001
#define LOG_EVENT_FORCED_ROTATE_F               0x0002
#define LOG_EVENT_THREAD_SPECIFIC_F             0x0004
#define LOG_EVENT_SUPPRESS_USE_F                0x0008
#define LOG_EVENT_UPDATE_TABLE_MAP_VERSION_F    0x0010
#define LOG_EVENT_ARTIFICIAL_F                  0x0020
#define LOG_EVENT_RELAY_LOG_F                   0x0040
#define LOG_EVENT_IGNORABLE_F                   0x0080
#define LOG_EVENT_NO_FILTER_F                   0x0100
#define LOG_EVENT_MTS_ISOLATE_F                 0x0200

/**
 * Binlog COM_BINLOG_DUMP flags
 */
#define BLR_REQUEST_ANNOTATE_ROWS_EVENT         2

/** MaxScale generated events */
typedef enum
{
    BLRM_IGNORABLE,          /*< Ignorable event */
    BLRM_START_ENCRYPTION    /*< Start Encryption event */
} generated_event_t;

/**
 * How often to call the binlog status function (seconds)
 */
#define BLR_STATS_FREQ          60
#define BLR_NSTATS_MINUTES      30

/**
 * High and Low water marks for the slave dcb.
 * These values can be overriden
 * by the router options highwater and lowwater.
 */
#define DEF_LOW_WATER           "1000"
#define DEF_HIGH_WATER          "10000"

/**
 * Default burst sizes for slave catchup
 */
#define DEF_SHORT_BURST         "15"
#define DEF_LONG_BURST          "500"
#define DEF_BURST_SIZE          "1024000" /* 1 Mb */

/**
 * master reconnect backoff constants
 * BLR_MASTER_BACKOFF_TIME      The increments of the back off time (seconds)
 * BLR_MASTER_CONNECT_RETRY     The connect retry interval
 * BLR_BLR_MASTER_RETRY_COUNT   Maximum value of retries
 */
#define BLR_MASTER_BACKOFF_TIME      10
#define BLR_MASTER_CONNECT_RETRY    "60"
#define BLR_MASTER_RETRY_COUNT     "1000"

/* Default value for @@max_connections SQL var */
#define BLR_DEFAULT_MAX_CONNS   151

/* max size for error message returned to client */
#define BINLOG_ERROR_MSG_LEN    700

/* network latency extra wait tme for heartbeat check */
#define BLR_NET_LATENCY_WAIT_TIME       1

/* default heartbeat interval in seconds */
#define BLR_HEARTBEAT_DEFAULT_INTERVAL  "300"

/* Max heartbeat interval in seconds */
#define BLR_HEARTBEAT_MAX_INTERVAL    4294967

/* strings and numbers in SQL replies */
#define BLR_TYPE_STRING                 0xf
#define BLR_TYPE_INT                    0x03

/* string len for COM_STATISTICS output */
#define BLRM_COM_STATISTICS_SIZE        1000

/* string len for strerror_r message */
#define BLRM_STRERROR_R_MSG_SIZE        128

/* string len for task message name */
#define BLRM_TASK_NAME_LEN              80

/* string len for temp binlog filename  */
#define BLRM_BINLOG_NAME_STR_LEN        80

/* string len for temp binlog filename  */
#define BLRM_SET_HEARTBEAT_QUERY_LEN    80

/* string len for master registration query  */
#define BLRM_MASTER_REGITRATION_QUERY_LEN       255

/* Read Binlog position states */
#define SLAVE_POS_READ_OK                       0x00
#define SLAVE_POS_READ_ERR                      0xff
#define SLAVE_POS_READ_UNSAFE                   0xfe
#define SLAVE_POS_BAD_FD                        0xfd
#define SLAVE_POS_BEYOND_EOF                    0xfc

/* MariadDB 10 GTID event flags */
#define MARIADB_FL_DDL                 32
#define MARIADB_FL_STANDALONE           1

/* Maxwell-related SQL queries */
#define MYSQL_CONNECTOR_SERVER_VARS_QUERY  "SELECT " \
                                              "@@session.auto_increment_increment AS auto_increment_increment, " \
                                              "@@character_set_client AS character_set_client, "                 \
                                              "@@character_set_connection AS character_set_connection, "         \
                                              "@@character_set_results AS character_set_results, "               \
                                              "@@character_set_server AS character_set_server, "                 \
                                              "@@init_connect AS init_connect, "                                 \
                                              "@@interactive_timeout AS interactive_timeout, "                   \
                                              "@@license AS license, "                                           \
                                              "@@lower_case_table_names AS lower_case_table_names, "             \
                                              "@@max_allowed_packet AS max_allowed_packet, "                     \
                                              "@@net_buffer_length AS net_buffer_length, "                       \
                                              "@@net_write_timeout AS net_write_timeout, "                       \
                                              "@@query_cache_size AS query_cache_size, "                         \
                                              "@@query_cache_type AS query_cache_type, "                         \
                                              "@@sql_mode AS sql_mode, "                                         \
                                              "@@system_time_zone AS system_time_zone, "                         \
                                              "@@time_zone AS time_zone, "                                       \
                                              "@@tx_isolation AS tx_isolation, "                                 \
                                              "@@wait_timeout AS wait_timeout"

#define MYSQL_CONNECTOR_SQL_MODE_QUERY     "SET sql_mode=" \
                                              "'NO_AUTO_CREATE_USER,NO_ENGINE_SUBSTITUTION,STRICT_TRANS_TABLES'"

/* Saved credential file name's tail */
static const char BLR_DBUSERS_DIR[] = "cache/users";
static const char BLR_DBUSERS_FILE[] = "dbusers";

/**
 * Some useful macros for examining the MySQL Response packets
 */
#define MYSQL_RESPONSE_OK(buf)  (*((uint8_t *)GWBUF_DATA(buf) + 4) == 0x00)
#define MYSQL_RESPONSE_EOF(buf) (*((uint8_t *)GWBUF_DATA(buf) + 4) == 0xfe)
#define MYSQL_RESPONSE_ERR(buf) (*((uint8_t *)GWBUF_DATA(buf) + 4) == 0xff)
#define MYSQL_ERROR_CODE(buf)   ((uint8_t *)GWBUF_DATA(buf) + 5)
#define MYSQL_ERROR_MSG(buf)    ((uint8_t *)GWBUF_DATA(buf) + 7)
#define MYSQL_COMMAND(buf)      (*((uint8_t *)GWBUF_DATA(buf) + 4))

/** Possible states of an event sent by the master */
enum blr_event_state
{
    BLR_EVENT_STARTED, /*< The first packet of an event has been received */
    BLR_EVENT_ONGOING, /*< Other packets of a multi-packet event are being processed */
    BLR_EVENT_DONE,    /*< The complete event was received */
};

/** MariaDB GTID elements */
typedef struct mariadb_gtid_elems
{
    uint32_t domain_id;   /*< The replication domain */
    uint32_t server_id;   /*< The serverid */
    uint64_t seq_no;      /*< The sequence number */
} MARIADB_GTID_ELEMS;

/** MariaDB GTID info */
typedef struct mariadb_gtid_info
{
    char gtid[GTID_MAX_LEN + 1];     /** MariaDB 10.x GTID, string value */
    char file[BINLOG_FNAMELEN + 1];  /** The binlog file */
    uint64_t start;                  /** The BEGIN pos: i.e the GTID event */
    uint64_t end;                    /** The next_pos in COMMIT event */
    MARIADB_GTID_ELEMS gtid_elms;    /** MariaDB 10.x GTID components */
} MARIADB_GTID_INFO;

/* Master Server configuration struct */
class MasterServerConfig
{
public:
    std::string    host;
    unsigned short port;
    std::string    logfile;
    uint64_t       pos;
    uint64_t       safe_pos;
    std::string    user;
    std::string    password;
    std::string    filestem;
    /* SSL options */
    std::string    ssl_key;
    std::string    ssl_cert;
    std::string    ssl_ca;
    int ssl_enabled;
    std::string    ssl_version;
    /* Connect options */
    int            heartbeat;
};

/* Config struct for CHANGE MASTER TO */
class ChangeMasterConfig
{
public:
    std::string connection_name;
    std::string host;
    int         port;
    std::string binlog_file;
    std::string binlog_pos;
    std::string user;
    std::string password;
    /* SSL options */
    std::string ssl_key;
    std::string ssl_cert;
    std::string ssl_ca;
    std::string ssl_enabled;
    std::string ssl_version;
    /* MariaDB 10 GTID */
    std::string use_mariadb10_gtid;
    /* Connection options */
    int         heartbeat_period;
    int         connect_retry;
};

struct ROUTER_INSTANCE;

/* Config struct for CHANGE MASTER TO options */
class ChangeMasterOptions
{
public:
    ChangeMasterOptions()
    {
    }

    ChangeMasterOptions(const std::string& s)
        : connection_name(s)
    {
    }

    std::string connection_name;
    std::string host;
    std::string port;
    std::string binlog_file;
    std::string binlog_pos;
    std::string user;
    std::string password;
    /* SSL options */
    std::string ssl_key;
    std::string ssl_cert;
    std::string ssl_ca;
    std::string ssl_enabled;
    std::string ssl_version;
    /* MariaDB 10 GTID */
    std::string use_mariadb10_gtid;
    /* Connection options */
    std::string heartbeat_period;
    std::string connect_retry;

    bool validate(ROUTER_INSTANCE* router,
                  char* error,
                  ChangeMasterConfig* config);
};

/**
 * Packet header for replication messages
 */
typedef struct rep_header
{
    int             payload_len;    /*< Payload length (24 bits) */
    uint8_t         seqno;          /*< Response sequence number */
    uint8_t         ok;             /*< OK Byte from packet */
    uint32_t        timestamp;      /*< Timestamp - start of binlog record */
    uint8_t         event_type;     /*< Binlog event type */
    uint32_t        serverid;       /*< Server id of master */
    uint32_t        event_size;     /*< Size of header, post-header and body */
    uint32_t        next_pos;       /*< Position of next event */
    uint16_t        flags;          /*< Event flags */
} REP_HEADER;

/**
 * The binlog record structure. This contains the actual packet read from the binlog
 * file.
 */
typedef struct
{
    unsigned long   position;       /*< binlog record position for this cache entry */
    GWBUF           *pkt;           /*< The packet received from the master */
    REP_HEADER      hdr;            /*< The packet header */
} BLCACHE_RECORD;

/**
 * The binlog cache. A cache exists for each file that hold cached bin log records.
 * Caches will be used for all files being read by more than 1 slave.
 */
typedef struct
{
    BLCACHE_RECORD  **records;      /*< The actual binlog records */
    int             current;        /*< The next record that will be inserted */
    int             cnt;            /*< The number of records in the cache */
    SPINLOCK        lock;           /*< The spinlock for the cache */
} BLCACHE;

typedef struct blfile
{
    char            binlogname[BINLOG_FNAMELEN + 1];
    /*< Name of the binlog file */
    int             fd;             /*< Actual file descriptor */
    int             refcnt;         /*< Reference count for file */
    BLCACHE         *cache;         /*< Record cache for this file */
    SPINLOCK        lock;           /*< The file lock */
    MARIADB_GTID_ELEMS   info;      /*< Elements for file prefix */
    struct blfile   *next;          /*< Next file in list */
} BLFILE;

/**
 * Slave statistics
 */
typedef struct
{
    int             n_events;       /*< Number of events sent */
    unsigned long   n_bytes;        /*< Number of bytes sent */
    int             n_bursts;       /*< Number of bursts sent */
    int             n_requests;     /*< Number of requests received */
    int             n_flows;        /*< Number of flow control restarts */
    int             n_queries;      /*< Number of SQL queries */
    int             n_upd;
    int             n_cb;
    int             n_cbna;
    int             n_dcb;
    int             n_above;
    int             n_failed_read;
    int             n_overrun;
    int             n_caughtup;
    int             n_actions[3];
    uint64_t        lastsample;
    int             minno;
    int             minavgs[BLR_NSTATS_MINUTES];
} SLAVE_STATS;

typedef enum blr_thread_role
{
    BLR_THREAD_ROLE_MASTER_LARGE_NOTRX,
    BLR_THREAD_ROLE_MASTER_NOTRX,
    BLR_THREAD_ROLE_MASTER_TRX,
    BLR_THREAD_ROLE_SLAVE
} blr_thread_role_t;

#define ROLETOSTR(r) r == BLR_THREAD_ROLE_MASTER_LARGE_NOTRX ? "master (large event, no trx)" : \
r == BLR_THREAD_ROLE_MASTER_NOTRX ? "master (no trx)" : \
r == BLR_THREAD_ROLE_MASTER_TRX ? "master (trx)" : "slave"

/**
 * Binlog encryption context of slave binlog file
 */

typedef struct slave_encryption_ctx
{
    uint8_t  binlog_crypto_scheme;   /**< Encryption scheme */
    uint32_t binlog_key_version;     /**< Encryption key version */
    uint8_t  nonce[AES_BLOCK_SIZE];  /**< nonce (random bytes) of current binlog.
                                      *   These bytes + the binlog event current pos
                                      *   form the encrryption IV for the event */
    char     *log_file;              /**< The log file the client has requested */
    uint32_t first_enc_event_pos;    /**< The position of first encrypted event
                                      *   It's the first event afte Start_encryption_event
                                      *   which is after FDE */
} SLAVE_ENCRYPTION_CTX;

/**
 * The client session structure used within this router. This represents
 * the slaves that are replicating binlogs from MaxScale.
 */
typedef struct router_slave
{
    DCB             *dcb;            /*< The slave server DCB */
    int             state;           /*< The state of this slave */
    uint32_t        binlog_pos;      /*< Binlog position for this slave */
    char            binlogfile[BINLOG_FNAMELEN + 1];
    /*< Current binlog file for this slave */
    char            *uuid;           /*< Slave UUID */
#ifdef BLFILE_IN_SLAVE
    BLFILE          *file;           /*< Currently open binlog file */
#endif
    int             serverid;        /*< Server-id of the slave */
    char            *hostname;       /*< Hostname of the slave, if known */
    char            *user;           /*< Username if given */
    char            *passwd;         /*< Password if given */
    short           port;            /*< MySQL port */
    int             nocrc;           /*< Disable CRC */
    int             overrun;
    uint32_t        rank;            /*< Replication rank */
    uint8_t         seqno;           /*< Replication dump sequence no */
    uint32_t        lastEventTimestamp;
    /*< Last event timestamp sent */
    SPINLOCK        catch_lock;      /*< Event catchup lock */
    unsigned int    cstate;          /*< Catch up state */
    bool            mariadb10_compat;/*< MariaDB 10.0 compatibility */
    SPINLOCK        rses_lock;       /*< Protects rses_deleted */
    pthread_t       pthread;
    ROUTER_INSTANCE *router;  /*< Pointer to the owning router */
    struct router_slave *next;
    SLAVE_STATS     stats;           /*< Slave statistics */
    time_t          connect_time;    /*< Connect time of slave */
    char            *warning_msg;    /*< Warning message */
    int             heartbeat;       /*< Heartbeat in seconds */
    uint8_t         lastEventReceived;
    /*< Last event received */
    time_t          lastReply;       /*< Last event sent */
    /*< lsi: Last Sent Information */
    blr_thread_role_t lsi_sender_role;
    /*< Master or slave code sent */
    std::thread::id   lsi_sender_tid;
    /*< Who sent */
    char              lsi_binlog_name[BINLOG_FNAMELEN + 1];
    /*< Which binlog file */
    uint32_t          lsi_binlog_pos;
    /*< What position */
    SLAVE_ENCRYPTION_CTX *encryption_ctx;
    /*< Encryption context */
    bool              gtid_strict_mode;
    /*< MariaDB 10 Slave sets gtid_strict_mode */
    char              *mariadb_gtid;  /*< MariaDB 10 Slave connects with GTID */
    sqlite3           *gtid_maps;     /*< GTID storage client handle, read only*/
    MARIADB_GTID_INFO f_info;         /*< GTID info for file name prefix */
    bool              annotate_rows;  /*< MariaDB 10 Slave requests ANNOTATE_ROWS */
} ROUTER_SLAVE;


/**
 * The statistics for this router instance
 */
typedef struct
{
    int             n_slaves;       /*< Number slave sessions created     */
    int             n_reads;        /*< Number of record reads */
    uint64_t        n_binlogs;      /*< Number of binlog records from master */
    uint64_t        n_binlogs_ses;  /*< Number of binlog records from master */
    uint64_t        n_binlog_errors;/*< Number of binlog records from master */
    uint64_t        n_rotates;      /*< Number of binlog rotate events */
    uint64_t        n_cachehits;    /*< Number of hits on the binlog cache */
    uint64_t        n_cachemisses;  /*< Number of misses on the binlog cache */
    int             n_registered;   /*< Number of registered slaves */
    int             n_masterstarts; /*< Number of times connection restarted */
    int             n_delayedreconnects;
    int             n_residuals;    /*< Number of times residual data was buffered */
    int             n_heartbeats;   /*< Number of heartbeat messages */
    time_t          lastReply;
    uint64_t        n_fakeevents;   /*< Fake events not written to disk */
    uint64_t        n_artificial;   /*< Artificial events not written to disk */
    int             n_badcrc;       /*< No. of bad CRC's from master */
    uint64_t        events[MAX_EVENT_TYPE_END + 1]; /*< Per event counters */
    uint64_t        lastsample;
    int             minno;
    int             minavgs[BLR_NSTATS_MINUTES];
} ROUTER_STATS;

/**
 * Saved responses from the master that will be forwarded to slaves
 */
typedef struct
{
    GWBUF           *server_id;         /*< Master server id */
    GWBUF           *heartbeat;         /*< Heartbeat period */
    GWBUF           *chksum1;           /*< Binlog checksum 1st response */
    GWBUF           *chksum2;           /*< Binlog checksum 2nd response */
    GWBUF           *gtid_mode;         /*< GTID Mode response */
    GWBUF           *uuid;              /*< Master UUID */
    GWBUF           *setslaveuuid;      /*< Set Slave UUID */
    GWBUF           *setnames;          /*< Set NAMES latin1 */
    GWBUF           *utf8;              /*< Set NAMES utf8 */
    GWBUF           *select1;           /*< select 1 */
    GWBUF           *selectver;         /*< select version() */
    GWBUF           *selectvercom;      /*< select @@version_comment */
    GWBUF           *selecthostname;    /*< select @@hostname */
    GWBUF           *map;               /*< select @@max_allowed_packet */
    GWBUF           *mariadb10;         /*< set @mariadb_slave_capability */
    GWBUF           *server_vars;       /*< MySQL Connector master server variables */
    GWBUF           *binlog_vars;       /*< SELECT @@global.log_bin,
                                         *  @@global.binlog_format,
                                         *  @@global.binlog_row_image;
                                         */
    GWBUF           *lower_case_tables; /*< select @@lower_case_table_names */
} MASTER_RESPONSES;

/**
 * The binlog encryption setup
 */
typedef struct binlog_encryption_setup
{
    bool enabled;
    int encryption_algorithm;
    char *key_management_filename;
    uint8_t key_value[BINLOG_AES_MAX_KEY_LEN];
    unsigned long key_len;
    uint8_t key_id;
} BINLOG_ENCRYPTION_SETUP;

/** Transaction States */
typedef enum
{
    BLRM_NO_TRANSACTION,       /*< No transaction */
    BLRM_TRANSACTION_START,    /*< A transaction is open*/
    BLRM_COMMIT_SEEN,          /*< Received COMMIT event in the current trx */
    BLRM_XID_EVENT_SEEN,       /*< Received XID event of current transaction */
    BLRM_STANDALONE_SEEN       /*< Received a standalone event, ie: a DDL */
} master_transaction_t;

/** Transaction Details */
typedef struct pending_transaction
{
    char gtid[GTID_MAX_LEN + 1];     /*< MariaDB 10.x GTID */
    master_transaction_t state;      /*< Transaction state */
    uint64_t start_pos;              /*< The BEGIN pos */
    uint64_t end_pos;                /*< The next_pos in COMMIT event */
    MARIADB_GTID_ELEMS gtid_elms;    /*< MariaDB 10.x GTID components */
    bool standalone;                 /*< Standalone event, such as DDL
                                      *  no terminating COMMIT
                                      */
} PENDING_TRANSACTION;

/**
 * Binlog encryption context of binlog file
 */

typedef struct binlog_encryption_ctx
{
    uint8_t  binlog_crypto_scheme;   /**< Encryption scheme */
    uint32_t binlog_key_version;     /**< Encryption key version */
    uint8_t  nonce[AES_BLOCK_SIZE];  /**< nonce (random bytes) of current binlog.
                                      *   These bytes + the binlog event current pos
                                      *   form the encrryption IV for the event */
    char     *binlog_file;           /**< Current binlog file being encrypted */
} BINLOG_ENCRYPTION_CTX;

/**
 * The per instance data for the router.
 */
struct ROUTER_INSTANCE: public MXS_ROUTER
{
    SERVICE                 *service;       /*< Pointer to the service using this router */
    ROUTER_SLAVE            *slaves;        /*< Link list of all the slave connections  */
    SPINLOCK                lock;           /*< Spinlock for the instance data */
    char                    *uuid;          /*< UUID for the router to use w/master */
    int                     orig_masterid;  /*< Server ID of the master, internally used */
    int                     masterid;       /*< Set ID of the master, sent to slaves */
    int                     serverid;       /*< ID for the router to use w/master */
    int                     initbinlog;     /*< Initial binlog file number */
    char                    *user;          /*< User name to use with master */
    char                    *password;      /*< Password to use with master */
    char                    *fileroot;      /*< Root of binlog filename */
    bool                    master_chksum;  /*< Does the master provide checksums */
    bool                    mariadb10_compat;
    /*< MariaDB 10.0 compatibility */
    bool                    maxwell_compat; /*< Zendesk's Maxwell compatibility */
    char                    *master_uuid;   /*< Set UUID of the master, sent to slaves */
    DCB                     *master;        /*< DCB for master connection */
    DCB                     *client;        /*< DCB for dummy client */
    MXS_SESSION             *session;       /*< Fake session for master connection */
    unsigned int            master_state;   /*< State of the master FSM */
    uint8_t                 lastEventReceived;
    /*< Last event received */
    uint32_t                lastEventTimestamp;
    /*< Timestamp from last event */
    MASTER_RESPONSES        saved_master;   /*< Saved master responses */
    char                    *binlogdir;     /*< The directory with the binlog files */
    SPINLOCK                binlog_lock;    /*< Lock to control update of the binlog position */
    int                     trx_safe;       /*< Detect and handle partial transactions */
    PENDING_TRANSACTION     pending_transaction;
    /*< Pending transaction */
    enum blr_event_state    master_event_state;
    /*< Packet read state */
    REP_HEADER              stored_header;
    /*< Replication header of the event the master is sending */
    GWBUF                   *stored_event;  /*< Buffer where partial events are stored */
    uint64_t                last_safe_pos;  /* last committed transaction */
    char                    binlog_name[BINLOG_FNAMELEN + 1];
    /*< Name of the current binlog file */
    uint64_t                binlog_position;/*< last committed transaction position */
    uint64_t                current_pos;    /*< Current binlog position */
    int                     binlog_fd;      /*< File descriptor of the binlog
                                             *  file being written
                                             */
    uint64_t          last_written;         /*< Position of the last write operation */
    uint64_t          last_event_pos;       /*< Position of last event written */
    uint64_t          current_safe_event;
    /*< Position of the latest safe event being sent to slaves */
    char              prevbinlog[BINLOG_FNAMELEN + 1];
    int               rotating;             /*< Rotation in progress flag */
    BLFILE            *files;               /*< Files used by the slaves */
    SPINLOCK          fileslock;            /*< Lock for the files queue above */
    unsigned int      short_burst;          /*< Short burst for slave catchup */
    unsigned int      long_burst;           /*< Long burst for slave catchup */
    unsigned long     burst_size;           /*< Maximum size of burst to send */
    unsigned long     heartbeat;            /*< Configured heartbeat value */
    ROUTER_STATS      stats;                /*< Statistics for this router */
    int               active_logs;
    int               reconnect_pending;
    int               retry_interval;       /*< Connect retry interval */
    int               retry_count;          /*< Connect retry counter */
    int               retry_limit;          /*< Retry limit */
    time_t            connect_time;
    int               handling_threads;
    unsigned long     m_errno;              /*< master response mysql errno */
    char              *m_errmsg;            /*< master response mysql error message */
    char              *set_master_version;  /*< Send custom Version to slaves */
    char              *set_master_hostname; /*< Send custom Hostname to slaves */
    bool              set_master_uuid;      /*< Send custom Master UUID to slaves */
    bool              set_master_server_id; /*< Send custom Master server_id to slaves */
    int               send_slave_heartbeat; /*< Enable sending heartbeat to slaves */
    bool              ssl_enabled;          /*< Use SSL connection to master */
    int               ssl_cert_verification_depth;
    /*< The maximum length of the certificate
     * authority chain that will be accepted.
     */
    char              *ssl_key;             /*< config Certificate Key for Master SSL connection */
    char              *ssl_ca;              /*< config CA Certificate for Master SSL connection */
    char              *ssl_cert;            /*< config Certificate for Master SSL connection */
    char              *ssl_version;         /*< config TLS Version for Master SSL connection */
    bool              request_semi_sync;    /*< Request Semi-Sync replication to master */
    int               master_semi_sync;     /*< Semi-Sync replication status of master server */
    BINLOG_ENCRYPTION_SETUP encryption;     /*< Binlog encryption setup */
    BINLOG_ENCRYPTION_CTX *encryption_ctx;      /*< Encryption context */
    char              last_mariadb_gtid[GTID_MAX_LEN  + 1];
    /*< Last seen MariaDB 10 GTID */
    bool              mariadb10_gtid;       /*< Save received MariaDB GTIDs into repo.
                                             * This allows MariaDB 10 slave servers
                                             * connecting with GTID
                                             */
    bool              mariadb10_master_gtid;/*< Enables MariaDB 10 GTID registration
                                             * to MariaDB 10.0/10.1 Master
                                             */
    uint32_t          mariadb10_gtid_domain;/*< MariaDB 10 GTID Domain ID */
    sqlite3           *gtid_maps;           /*< MariaDB 10 GTID storage */
    enum binlog_storage_type   storage_type;/*< Enables hierachical binlog file storage */
    char              *set_slave_hostname;  /*< Send custom Hostname to Master */
    ROUTER_INSTANCE  *next;
    std::vector<ChangeMasterConfig> configs;              /*< Current config. */
};

/** Master Semi-Sync capability */
typedef enum
{
    MASTER_SEMISYNC_NOT_AVAILABLE, /*< Semi-Sync replication not available */
    MASTER_SEMISYNC_DISABLED, /*< Semi-Sync is disabled */
    MASTER_SEMISYNC_ENABLED /*< Semi-Sync is enabled */
} master_semisync_capability_t;

/**
 * Holds information about:
 * truncating a corrupted file
 * or replace an event at specified pos
 * or replace a transaction that start
 * at specified pos
 */

typedef struct binlog_pos_fix
{
    bool        fix;            /**< Truncate file to last safe pos */
    uint64_t    pos;            /**< Position of the event to be replaced
                                 *   by an Ignorable Event
                                 */
    bool        replace_trx;    /**< Replace all events belonging to
                                 *   a transaction starting at pos
                                 */
} BINLOG_FILE_FIX;

/**
 * Defines and offsets for binlog encryption
 *
 * BLRM_FDE_EVENT_TYPES_OFFSET is the offset
 * in FDE event content that points to
 * the number of events the master server supports.
 */
#define BLR_FDE_EVENT_BINLOG_VERSION       2
#define BLR_FDE_EVENT_SERVER_VERSION      50
#define BLR_FDE_EVENT_BINLOG_TIME          4
#define BLR_FDE_EVENT_BINLOG_EVENT_HDR_LEN 1
#define BLRM_FDE_EVENT_TYPES_OFFSET        (BLR_FDE_EVENT_BINLOG_VERSION + \
                                            BLR_FDE_EVENT_SERVER_VERSION + \
                                            BLR_FDE_EVENT_BINLOG_TIME + \
                                            BLR_FDE_EVENT_BINLOG_EVENT_HDR_LEN)
#define BLRM_CRYPTO_SCHEME_LENGTH   1
#define BLRM_KEY_VERSION_LENGTH     4
#define BLRM_IV_LENGTH              AES_BLOCK_SIZE
#define BLRM_IV_OFFS_LENGTH         4
#define BLRM_NONCE_LENGTH           (BLRM_IV_LENGTH - BLRM_IV_OFFS_LENGTH)

/**
 * State machine for the master to MaxScale replication
 */
#define BLRM_UNCONFIGURED             0x0000
#define BLRM_UNCONNECTED              0x0001
#define BLRM_CONNECTING               0x0002
#define BLRM_AUTHENTICATED            0x0003
#define BLRM_TIMESTAMP                0x0004
#define BLRM_SERVERID                 0x0005
#define BLRM_HBPERIOD                 0x0006
#define BLRM_CHKSUM1                  0x0007
#define BLRM_CHKSUM2                  0x0008
#define BLRM_MARIADB10                0x0009
#define BLRM_MARIADB10_GTID_DOMAIN    0x000A
#define BLRM_MARIADB10_REQUEST_GTID   0x000B
#define BLRM_MARIADB10_GTID_STRICT    0x000C
#define BLRM_MARIADB10_GTID_NO_DUP    0x000D
#define BLRM_GTIDMODE                 0x000E
#define BLRM_MUUID                    0x000F
#define BLRM_SUUID                    0x0010
#define BLRM_LATIN1                   0x0011
#define BLRM_UTF8                     0x0012
#define BLRM_RESULTS_CHARSET          0x0013
#define BLRM_SQL_MODE                 0x0014
#define BLRM_SELECT1                  0x0015
#define BLRM_SELECTVER                0x0016
#define BLRM_SELECTVERCOM             0x0017
#define BLRM_SELECTHOSTNAME           0x0018
#define BLRM_MAP                      0x0019
#define BLRM_SERVER_VARS              0x001A
#define BLRM_BINLOG_VARS              0x001B
#define BLRM_LOWER_CASE_TABLES        0x001C
#define BLRM_REGISTER_READY           0x001D
#define BLRM_REGISTER                 0x001E
#define BLRM_CHECK_SEMISYNC           0x001F
#define BLRM_REQUEST_SEMISYNC         0x0020
#define BLRM_REQUEST_BINLOGDUMP       0x0021
#define BLRM_BINLOGDUMP               0x0022
#define BLRM_SLAVE_STOPPED            0x0023

#define BLRM_MAXSTATE                 0x0023

static const char *blrm_states[] =
{
    "Unconfigured",
    "Unconnected",
    "Connecting",
    "Authenticated",
    "Timestamp retrieval",
    "Server ID retrieval",
    "HeartBeat Period setup",
    "binlog checksum config",
    "binlog checksum rerieval",
    "Set MariaDB10 slave capability",
    "MariaDB10 GTID Domain retrieval",
    "Set MariaDB10 GTID Request",
    "Set MariaDB10 GTID strict mode",
    "Set MariaDB10 GTID ignore duplicates",
    "GTID Mode retrieval",
    "Master UUID retrieval",
    "Set Slave UUID",
    "Set Names latin1",
    "Set Names utf8",
    "Set results charset null",
    "Set sql_mode",
    "select 1",
    "select version()",
    "select @@version_comment",
    "select @@hostname",
    "select @@max_allowed_packet",
    "Query server variables",
    "Query binlog variables",
    "Query @@lower_case_table_names",
    "Ready to Register",
    "Register slave",
    "Semi-Sync Support retrivial",
    "Request Semi-Sync Replication",
    "Request Binlog Dump",
    "Binlog Dump",
    "Slave stopped"
};

#define BLRS_CREATED            0x0000
#define BLRS_UNREGISTERED       0x0001
#define BLRS_REGISTERED         0x0002
#define BLRS_DUMPING            0x0003
#define BLRS_ERRORED            0x0004

#define BLRS_MAXSTATE           0x0004

static const char *blrs_states[] =
{
    "Created",
    "Unregistered",
    "Registered",
    "Sending binlogs",
    "Errored"
};

/**
 * Slave catch-up status
 */
#define CS_UNDEFINED            0x0000
#define CS_EXPECTCB             0x0004
#define CS_WAIT_DATA            0x0020
#define CS_BUSY                 0x0040

/**
 * MySQL protocol OpCodes needed for replication
 */
#define COM_QUIT                0x01
#define COM_QUERY               0x03
#define COM_STATISTICS          0x09
#define COM_PING                0x0e
#define COM_REGISTER_SLAVE      0x15
#define COM_BINLOG_DUMP         0x12

/**
 * Macros to extract common fields
 */
#define INLINE_EXTRACT          1       /* Set to 0 for debug purposes */

#if INLINE_EXTRACT
#define EXTRACT16(x)            (*(uint8_t *)(x) | (*((uint8_t *)(x) + 1) << 8))
#define EXTRACT24(x)            (*(uint8_t *)(x) |              \
                                 (*((uint8_t *)(x) + 1) << 8) | \
                                 (*((uint8_t *)(x) + 2) << 16))
#define EXTRACT32(x)            (*(uint8_t *)(x) |                      \
                                 (*((uint8_t *)(x) + 1) << 8) |         \
                                 (*((uint8_t *)(x) + 2) << 16) |        \
                                 (*((uint8_t *)(x) + 3) << 24))
#else
#define EXTRACT16(x)            extract_field((x), 16)
#define EXTRACT24(x)            extract_field((x), 24)
#define EXTRACT32(x)            extract_field((x), 32)
#endif

#define MASTER_BYTES_BEFORE_EVENT 5
#define MASTER_BYTES_BEFORE_EVENT_SEMI_SYNC MASTER_BYTES_BEFORE_EVENT + 2
/* Semi-Sync indicator in network packet (byte 6) */
#define BLR_MASTER_SEMI_SYNC_INDICATOR  0xef
/* Semi-Sync flag ACK_REQ in network packet (byte 7) */
#define BLR_MASTER_SEMI_SYNC_ACK_REQ    0x01

/*
 * Externals within the router
 */
extern void blr_log_disabled_heartbeat(const ROUTER_INSTANCE *inst);
extern void blr_master_response(ROUTER_INSTANCE *, GWBUF *);
extern void blr_master_reconnect(ROUTER_INSTANCE *);
extern int blr_master_connected(ROUTER_INSTANCE *);

extern int blr_slave_request(ROUTER_INSTANCE *,
                             ROUTER_SLAVE *,
                             GWBUF *);
extern void blr_slave_rotate(ROUTER_INSTANCE *,
                             ROUTER_SLAVE *,
                             uint8_t *);
extern int blr_slave_catchup(ROUTER_INSTANCE *router,
                             ROUTER_SLAVE *slave,
                             bool large);
extern void blr_init_cache(ROUTER_INSTANCE *);

extern int  blr_file_init(ROUTER_INSTANCE *);
extern int  blr_write_binlog_record(ROUTER_INSTANCE *,
                                    REP_HEADER *,
                                    uint32_t pos,
                                    uint8_t *);
extern int  blr_file_rotate(ROUTER_INSTANCE *,
                            char *,
                            uint64_t);
extern int blr_file_read_master_config(ROUTER_INSTANCE *router);
extern int blr_file_write_master_config(ROUTER_INSTANCE *router, char *error);
extern void blr_file_flush(ROUTER_INSTANCE *);
extern BLFILE *blr_open_binlog(ROUTER_INSTANCE *,
                               const char *,
                               const MARIADB_GTID_INFO *);
extern GWBUF *blr_read_binlog(ROUTER_INSTANCE *,
                              BLFILE *,
                              unsigned long,
                              REP_HEADER *,
                              char *,
                              const SLAVE_ENCRYPTION_CTX *);
extern void blr_close_binlog(ROUTER_INSTANCE *, BLFILE *);
extern unsigned long blr_file_size(BLFILE *);
extern int blr_statistics(ROUTER_INSTANCE *, ROUTER_SLAVE *, GWBUF *);
extern int blr_ping(ROUTER_INSTANCE *, ROUTER_SLAVE *, GWBUF *);
extern int blr_send_custom_error(DCB *,
                                 int,
                                 int,
                                 const char *,
                                 const char *,
                                 unsigned int);
extern int blr_file_next_exists(ROUTER_INSTANCE *,
                                ROUTER_SLAVE *,
                                char *next_file);
uint32_t extract_field(uint8_t *src, int bits);
void blr_cache_read_master_data(ROUTER_INSTANCE *router);
int blr_read_events_all_events(ROUTER_INSTANCE *, BINLOG_FILE_FIX *, int);
int blr_save_dbusers(const ROUTER_INSTANCE *router);
const char *blr_get_event_description(ROUTER_INSTANCE *router, uint8_t event);
void blr_file_append(ROUTER_INSTANCE *router, char *file);
void blr_cache_response(ROUTER_INSTANCE *router, char *response, GWBUF *buf);
const char *blr_last_event_description(ROUTER_INSTANCE *router);
void blr_free_ssl_data(ROUTER_INSTANCE *inst);

extern bool blr_send_event(blr_thread_role_t role,
                           const char* binlog_name,
                           uint32_t binlog_pos,
                           ROUTER_SLAVE *slave,
                           REP_HEADER *hdr,
                           uint8_t *buf);

extern const char *blr_get_encryption_algorithm(int);
extern int blr_check_encryption_algorithm(const char *);
extern const char *blr_encryption_algorithm_list(void);
extern bool blr_get_encryption_key(ROUTER_INSTANCE *);
extern void blr_set_checksum(ROUTER_INSTANCE *instance, GWBUF *buf);
extern const char *blr_skip_leading_sql_comments(const char *);
extern bool blr_fetch_mariadb_gtid(ROUTER_SLAVE *,
                                   const char *,
                                   MARIADB_GTID_INFO *);
extern bool blr_start_master_in_main(ROUTER_INSTANCE* data, int32_t delay = 0);
extern bool blr_binlog_file_exists(ROUTER_INSTANCE *router,
                                   const MARIADB_GTID_INFO *info_file);

// Functions used by blr_handle_one_event
int blr_rotate_event(ROUTER_INSTANCE *router, uint8_t *ptr, REP_HEADER *hdr);
int blr_send_semisync_ack(ROUTER_INSTANCE *router, uint64_t pos);
bool blr_handle_fake_rotate(ROUTER_INSTANCE *router, REP_HEADER *hdr, uint8_t *ptr);
void blr_handle_fake_gtid_list(ROUTER_INSTANCE *router, REP_HEADER *hdr, uint8_t *ptr);
void blr_master_close(ROUTER_INSTANCE *);
void blr_notify_all_slaves(ROUTER_INSTANCE *router);
bool blr_save_mariadb_gtid(ROUTER_INSTANCE *inst);

/**
 * Handler for binlog events
 *
 * This function is called for each event replicated from the master.
 *
 * @param instance Router instance
 * @param hdr      Event header
 * @param ptr      Event data
 * @param len      Buffer length
 * @param semisync 1 if semisync is enabled (TODO: change this)
 *
 * @return True if event was successfully processed, false if an error occurred
 *         and the processing should be stopped.
 */
bool blr_handle_one_event(MXS_ROUTER* instance, REP_HEADER& hdr, uint8_t* ptr, uint32_t len,
                          int semi_sync_send_ack);

#endif
