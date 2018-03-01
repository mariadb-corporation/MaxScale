#pragma once
#ifndef _BLR_H
#define _BLR_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file blr.h - The binlog router header file
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 02/04/14     Mark Riddoch            Initial implementation
 * 25/05/15     Massimiliano Pinto      Added BLRM_SLAVE_STOPPED state
 * 05/06/15     Massimiliano Pinto      Addition of m_errno, m_errmsg fields
 * 08/06/15     Massimiliano Pinto      Modification of MYSQL_ERROR_CODE and MYSQL_ERROR_MSG
 * 11/05/15     Massimiliano Pinto      Added mariadb10_compat to master and slave structs
 * 12/06/15     Massimiliano Pinto      Added mariadb10 new events
 * 23/06/15     Massimiliano Pinto      Addition of MASTER_SERVER_CFG struct
 * 24/06/15     Massimiliano Pinto      Added BLRM_UNCONFIGURED state
 * 05/08/15     Massimiliano Pinto      Initial implementation of transaction safety
 * 23/10/15     Markus Makela           Added current_safe_event
 * 26/04/16     Massimiliano Pinto      Added MariaDB 10.0 and 10.1 GTID event flags detection
 * 11/07/16     Massimiliano Pinto      Added SSL backend support
 * 22/07/16     Massimiliano Pinto      Added Semi-Sync replication support
 * 24/08/16     Massimiliano Pinto      Added slave notification state CS_WAIT_DATA.
 *                                      State CS_UPTODATE removed.
 * 01/09/2016   Massimiliano Pinto      Added support for ANNOTATE_ROWS_EVENT in COM_BINLOG_DUMP
 * 16/09/2016   Massimiliano Pinto      Addition of MARIADB10_START_ENCRYPTION_EVENT 0xa4
 * 19/09/2016   Massimiliano Pinto      Added encrypt_binlog=0|1 option
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "binlogrouter"

#include <maxscale/cdefs.h>

#include <stdint.h>
#include <openssl/aes.h>
#include <pthread.h>
#include <zlib.h>

#include <maxscale/dcb.h>
#include <maxscale/buffer.h>
#include <maxscale/thread.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/secrets.h>

MXS_BEGIN_DECLS

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

/* Binlog Encryption */
#define BINLOG_ENC_ALGO_NAME_LEN        13
#define BINLOG_FLAG_ENCRYPT              1
#define BINLOG_FLAG_DECRYPT              0
#define BINLOG_AES_MAX_KEY_LEN          32
#define BINLOG_MAX_CRYPTO_SCHEME         2
#define BINLOG_SYSTEM_DATA_CRYPTO_SCHEME 1
#define BINLOG_MAX_KEYFILE_LINE_LEN     130

/* Event detail routine */
#define BLR_REPORT_CHECKSUM_FORMAT "CRC32 0x"
#define BLR_REPORT_REP_HEADER 0x02

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

/**
 * How often to call the binlog status function (seconds)
 */
#define BLR_STATS_FREQ          60
#define BLR_NSTATS_MINUTES      30

/**
 * High and Low water marks for the slave dcb. These values can be overriden
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
 * BLR_MAX_BACKOFF              Maximum number of increments to backoff to
 */
#define BLR_MASTER_BACKOFF_TIME 10
#define BLR_MAX_BACKOFF         60

/* max size for error message returned to client */
#define BINLOG_ERROR_MSG_LEN    700

/* network latency extra wait tme for heartbeat check */
#define BLR_NET_LATENCY_WAIT_TIME       1

/* default heartbeat interval in seconds */
#define BLR_HEARTBEAT_DEFAULT_INTERVAL  "300"

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
    BLR_EVENT_DONE, /*< The complete event was received */
};

/* Master Server configuration struct */
typedef struct master_server_config
{
    char *host;
    unsigned short port;
    char logfile[BINLOG_FNAMELEN + 1];
    uint64_t pos;
    uint64_t safe_pos;
    char *user;
    char *password;
    char *filestem;
    /* SSL options */
    char *ssl_key;
    char *ssl_cert;
    char *ssl_ca;
    int ssl_enabled;
    char *ssl_version;
} MASTER_SERVER_CFG;

/* Config struct for CHANGE MASTER TO options */
typedef struct change_master_options
{
    char *host;
    char *port;
    char *binlog_file;
    char *binlog_pos;
    char *user;
    char *password;
    /* SSL options */
    char *ssl_key;
    char *ssl_cert;
    char *ssl_ca;
    char *ssl_enabled;
    char *ssl_version;
} CHANGE_MASTER_OPTIONS;

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
    char            binlogname[BINLOG_FNAMELEN + 1]; /*< Name of the binlog file */
    int             fd;                             /*< Actual file descriptor */
    int             refcnt;                         /*< Reference count for file */
    BLCACHE         *cache;                         /*< Record cache for this file */
    SPINLOCK        lock;                           /*< The file lock */
    struct blfile   *next;                          /*< Next file in list */
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
 * The client session structure used within this router. This represents
 * the slaves that are replicating binlogs from MaxScale.
 */
typedef struct router_slave
{
#if defined(SS_DEBUG)
    skygw_chk_t     rses_chk_top;
#endif
    DCB             *dcb;           /*< The slave server DCB */
    int             state;          /*< The state of this slave */
    uint32_t        binlog_pos;     /*< Binlog position for this slave */
    char            binlogfile[BINLOG_FNAMELEN + 1];
    /*< Current binlog file for this slave */
    char            *uuid;          /*< Slave UUID */
#ifdef BLFILE_IN_SLAVE
    BLFILE          *file;          /*< Currently open binlog file */
#endif
    int             serverid;       /*< Server-id of the slave */
    char            *hostname;      /*< Hostname of the slave, if known */
    char            *user;          /*< Username if given */
    char            *passwd;        /*< Password if given */
    short           port;           /*< MySQL port */
    int             nocrc;          /*< Disable CRC */
    int             overrun;
    uint32_t        rank;           /*< Replication rank */
    uint8_t         seqno;          /*< Replication dump sequence no */
    uint32_t        lastEventTimestamp;/*< Last event timestamp sent */
    SPINLOCK        catch_lock;     /*< Event catchup lock */
    unsigned int    cstate;         /*< Catch up state */
    bool            mariadb10_compat;/*< MariaDB 10.0 compatibility */
    SPINLOCK        rses_lock;      /*< Protects rses_deleted */
    pthread_t       pthread;
    struct router_instance
        *router;        /*< Pointer to the owning router */
    struct router_slave *next;
    SLAVE_STATS     stats;          /*< Slave statistics */
    time_t          connect_time;   /*< Connect time of slave */
    char            *warning_msg;   /*< Warning message */
    int             heartbeat;      /*< Heartbeat in seconds */
    uint8_t         lastEventReceived; /*< Last event received */
    time_t          lastReply;      /*< Last event sent */
    // lsi: Last Sent Information
    blr_thread_role_t lsi_sender_role; /*< Master or slave code sent */
    THREAD            lsi_sender_tid;  /*< Who sent */
    char              lsi_binlog_name[BINLOG_FNAMELEN + 1]; /*< Which binlog file */
    uint32_t          lsi_binlog_pos; /*< What position */
    void              *encryption_ctx;      /*< Encryption context */
    bool              annotate_rows;  /*< MariaDB 10 Slave requests ANNOTATE_ROWS */
#if defined(SS_DEBUG)
    skygw_chk_t     rses_chk_tail;
#endif
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
    GWBUF           *server_id;     /*< Master server id */
    GWBUF           *heartbeat;     /*< Heartbeat period */
    GWBUF           *chksum1;       /*< Binlog checksum 1st response */
    GWBUF           *chksum2;       /*< Binlog checksum 2nd response */
    GWBUF           *gtid_mode;     /*< GTID Mode response */
    GWBUF           *uuid;          /*< Master UUID */
    GWBUF           *setslaveuuid;  /*< Set Slave UUID */
    GWBUF           *setnames;      /*< Set NAMES latin1 */
    GWBUF           *utf8;          /*< Set NAMES utf8 */
    GWBUF           *select1;       /*< select 1 */
    GWBUF           *selectver;     /*< select version() */
    GWBUF           *selectvercom;  /*< select @@version_comment */
    GWBUF           *selecthostname;/*< select @@hostname */
    GWBUF           *map;           /*< select @@max_allowed_packet */
    GWBUF           *mariadb10;     /*< set @mariadb_slave_capability */
    uint8_t         *fde_event;     /*< Format Description Event */
    int             fde_len;        /*< Length of fde_event */
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

/**
 * The per instance data for the router.
 */
typedef struct router_instance
{
    SERVICE                 *service;       /*< Pointer to the service using this router */
    ROUTER_SLAVE            *slaves;        /*< Link list of all the slave connections  */
    SPINLOCK                lock;           /*< Spinlock for the instance data */
    char                    *uuid;          /*< UUID for the router to use w/master */
    int                     masterid;       /*< Set ID of the master, sent to slaves */
    int                     serverid;       /*< ID for the router to use w/master */
    int                     initbinlog;     /*< Initial binlog file number */
    char                    *user;          /*< User name to use with master */
    char                    *password;      /*< Password to use with master */
    char                    *fileroot;      /*< Root of binlog filename */
    bool                    master_chksum;  /*< Does the master provide checksums */
    bool                    mariadb10_compat; /*< MariaDB 10.0 compatibility */
    char                    *master_uuid;   /*< Set UUID of the master, sent to slaves */
    DCB                     *master;        /*< DCB for master connection */
    DCB                     *client;        /*< DCB for dummy client */
    MXS_SESSION             *session;       /*< Fake session for master connection */
    unsigned int            master_state;   /*< State of the master FSM */
    uint8_t                 lastEventReceived; /*< Last even received */
    uint32_t                lastEventTimestamp; /*< Timestamp from last event */
    MASTER_RESPONSES        saved_master;   /*< Saved master responses */
    char                    *binlogdir;     /*< The directory with the binlog files */
    SPINLOCK                binlog_lock;    /*< Lock to control update of the binlog position */
    int                     trx_safe;       /*< Detect and handle partial transactions */
    int                     pending_transaction; /*< Pending transaction */
    enum blr_event_state    master_event_state; /*< Packet read state */
    REP_HEADER              stored_header; /*< Relication header of the event the master is sending */
    GWBUF                  *stored_event; /*< Buffer where partial events are stored */
    uint64_t                last_safe_pos; /* last committed transaction */
    char                    binlog_name[BINLOG_FNAMELEN + 1];
    /*< Name of the current binlog file */
    uint64_t                binlog_position;
    /*< last committed transaction position */
    uint64_t                current_pos;
    /*< Current binlog position */
    int                     binlog_fd;      /*< File descriptor of the binlog
                                             *  file being written
                                             */
    uint64_t          last_written; /*< Position of the last write operation */
    uint64_t          last_event_pos;       /*< Position of last event written */
    uint64_t          current_safe_event;
    /*< Position of the latest safe event being sent to slaves */
    char              prevbinlog[BINLOG_FNAMELEN + 1];
    int               rotating;     /*< Rotation in progress flag */
    BLFILE            *files;       /*< Files used by the slaves */
    SPINLOCK          fileslock;    /*< Lock for the files queue above */
    unsigned int      low_water;    /*< Low water mark for client DCB */
    unsigned int      high_water;   /*< High water mark for client DCB */
    unsigned int      short_burst;  /*< Short burst for slave catchup */
    unsigned int      long_burst;   /*< Long burst for slave catchup */
    unsigned long     burst_size;   /*< Maximum size of burst to send */
    unsigned long     heartbeat;    /*< Configured heartbeat value */
    ROUTER_STATS      stats;        /*< Statistics for this router */
    int               active_logs;
    int               reconnect_pending;
    int               retry_backoff;
    time_t            connect_time;
    int               handling_threads;
    unsigned long     m_errno;      /*< master response mysql errno */
    char              *m_errmsg;    /*< master response mysql error message */
    char              *set_master_version; /*< Send custom Version to slaves */
    char              *set_master_hostname; /*< Send custom Hostname to slaves */
    bool              set_master_uuid; /*< Send custom Master UUID to slaves */
    bool              set_master_server_id; /*< Send custom Master server_id to slaves */
    int               send_slave_heartbeat; /*< Enable sending heartbeat to slaves */
    bool              ssl_enabled;          /*< Use SSL connection to master */
    int               ssl_cert_verification_depth; /*< The maximum length of the certificate
                                                    * authority chain that will be accepted.
                                                    */
    char              *ssl_key;             /*< config Certificate Key for Master SSL connection */
    char              *ssl_ca;              /*< config CA Certificate for Master SSL connection */
    char              *ssl_cert;            /*< config Certificate for Master SSL connection */
    char              *ssl_version;         /*< config TLS Version for Master SSL connection */
    bool              request_semi_sync;    /*< Request Semi-Sync replication to master */
    int               master_semi_sync;     /*< Semi-Sync replication status of master server */
    BINLOG_ENCRYPTION_SETUP encryption;     /*< Binlog encryption setup */
    void              *encryption_ctx;      /*< Encryption context */
    char              *set_slave_hostname;  /*< Send custom Hostname to Master */
    struct router_instance  *next;
} ROUTER_INSTANCE;

/**
 * Binlog encryption context of slave binlog file
 */

typedef struct slave_encryption_ctx
{
    uint8_t  binlog_crypto_scheme;   /**< Encryption scheme */
    uint32_t binlog_key_version;     /**< Encryption key version */
    uint8_t  nonce[AES_BLOCK_SIZE];  /**< nonce (random bytes) of current binlog.
                                      * These bytes + the binlog event current pos
                                      * form the encrryption IV for the event */
    char     *log_file;              /**< The log file the client has requested */
    uint32_t first_enc_event_pos;    /**< The position of first encrypted event
                                      * It's the first event afte Start_encryption_event
                                      * Which is after FDE */
} SLAVE_ENCRYPTION_CTX;

/**
 * Binlog encryption context of binlog file
 */

typedef struct binlog_encryption_ctx
{
    uint8_t  binlog_crypto_scheme;   /**< Encryption scheme */
    uint32_t binlog_key_version;     /**< Encryption key version */
    uint8_t  nonce[AES_BLOCK_SIZE];  /**< nonce (random bytes) of current binlog.
                                      * These bytes + the binlog event current pos
                                      * form the encrryption IV for the event */
    char     *binlog_file;           /**< Current binlog file being encrypted */
} BINLOG_ENCRYPTION_CTX;

/**
 * Defines and offsets for binlog encryption
 *
 * BLRM_FDE_EVENT_TYPES_OFFSET is the offset in FDE event content that points to
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
#define BLRM_UNCONFIGURED       0x0000
#define BLRM_UNCONNECTED        0x0001
#define BLRM_CONNECTING         0x0002
#define BLRM_AUTHENTICATED      0x0003
#define BLRM_TIMESTAMP          0x0004
#define BLRM_SERVERID           0x0005
#define BLRM_HBPERIOD           0x0006
#define BLRM_CHKSUM1            0x0007
#define BLRM_CHKSUM2            0x0008
#define BLRM_MARIADB10          0x0009
#define BLRM_GTIDMODE           0x000A
#define BLRM_MUUID              0x000B
#define BLRM_SUUID              0x000C
#define BLRM_LATIN1             0x000D
#define BLRM_UTF8               0x000E
#define BLRM_SELECT1            0x000F
#define BLRM_SELECTVER          0x0010
#define BLRM_SELECTVERCOM       0x0011
#define BLRM_SELECTHOSTNAME     0x0012
#define BLRM_MAP                0x0013
#define BLRM_REGISTER           0x0014
#define BLRM_CHECK_SEMISYNC     0x0015
#define BLRM_REQUEST_SEMISYNC   0x0016
#define BLRM_REQUEST_BINLOGDUMP 0x0017
#define BLRM_BINLOGDUMP         0x0018
#define BLRM_SLAVE_STOPPED      0x0019

#define BLRM_MAXSTATE           0x0019

static char *blrm_states[] =
{
    "Unconfigured", "Unconnected", "Connecting", "Authenticated", "Timestamp retrieval",
    "Server ID retrieval", "HeartBeat Period setup", "binlog checksum config",
    "binlog checksum rerieval", "Set MariaDB slave capability", "GTID Mode retrieval",
    "Master UUID retrieval", "Set Slave UUID", "Set Names latin1", "Set Names utf8", "select 1",
    "select version()", "select @@version_comment", "select @@hostname",
    "select @@max_allowed_packet", "Register slave", "Semi-Sync Support retrivial",
    "Request Semi-Sync Replication", "Request Binlog Dump", "Binlog Dump", "Slave stopped"
};

#define BLRS_CREATED            0x0000
#define BLRS_UNREGISTERED       0x0001
#define BLRS_REGISTERED         0x0002
#define BLRS_DUMPING            0x0003
#define BLRS_ERRORED            0x0004

#define BLRS_MAXSTATE           0x0004

static char *blrs_states[] =
{
    "Created", "Unregistered", "Registered", "Sending binlogs", "Errored"
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
#define COM_QUIT                                0x01
#define COM_QUERY                               0x03
#define COM_STATISTICS                          0x09
#define COM_PING                                0x0e
#define COM_REGISTER_SLAVE                      0x15
#define COM_BINLOG_DUMP                         0x12

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
extern int  blr_write_binlog_record(ROUTER_INSTANCE *, REP_HEADER *, uint32_t pos, uint8_t *);
extern int  blr_file_rotate(ROUTER_INSTANCE *, char *, uint64_t);
extern void blr_file_flush(ROUTER_INSTANCE *);
extern BLFILE *blr_open_binlog(ROUTER_INSTANCE *, char *);
extern GWBUF *blr_read_binlog(ROUTER_INSTANCE *, BLFILE *, unsigned long, REP_HEADER *, char *,
                              const SLAVE_ENCRYPTION_CTX *);
extern void blr_close_binlog(ROUTER_INSTANCE *, BLFILE *);
extern unsigned long blr_file_size(BLFILE *);
extern int blr_statistics(ROUTER_INSTANCE *, ROUTER_SLAVE *, GWBUF *);
extern int blr_ping(ROUTER_INSTANCE *, ROUTER_SLAVE *, GWBUF *);
extern int blr_send_custom_error(DCB *, int, int, char *, char *, unsigned int);
extern int blr_file_next_exists(ROUTER_INSTANCE *, ROUTER_SLAVE *);
uint32_t extract_field(uint8_t *src, int bits);
void blr_cache_read_master_data(ROUTER_INSTANCE *router);
int blr_read_events_all_events(ROUTER_INSTANCE *router, int fix, int debug);
int blr_save_dbusers(const ROUTER_INSTANCE *router);
char    *blr_get_event_description(ROUTER_INSTANCE *router, uint8_t event);
void blr_file_append(ROUTER_INSTANCE *router, char *file);
void blr_cache_response(ROUTER_INSTANCE *router, char *response, GWBUF *buf);
char * blr_last_event_description(ROUTER_INSTANCE *router);
void blr_free_ssl_data(ROUTER_INSTANCE *inst);

extern bool blr_send_event(blr_thread_role_t role,
                           const char* binlog_name,
                           uint32_t binlog_pos,
                           ROUTER_SLAVE *slave,
                           REP_HEADER *hdr,
                           uint8_t *buf);

extern const char *blr_get_encryption_algorithm(int);
extern int blr_check_encryption_algorithm(char *);
extern const char *blr_encryption_algorithm_list(void);
extern bool blr_get_encryption_key(ROUTER_INSTANCE *);
extern void blr_set_checksum(ROUTER_INSTANCE *instance, GWBUF *buf);

MXS_END_DECLS

#endif
