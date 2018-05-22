#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "avrorouter"

#include <maxscale/cdefs.h>
#include <stdbool.h>
#include <stdint.h>
#include <blr_constants.h>
#include <maxscale/dcb.h>
#include <maxscale/service.h>
#include <maxscale/spinlock.h>
#include <maxscale/mysql_binlog.h>
#include <maxscale/users.h>
#include <avro.h>
#include <cdc.h>
#include <maxscale/pcre2.h>
#include <maxavro.h>
#include <binlog_common.h>
#include <maxscale/sqlite3.h>
#include <maxscale/protocol/mysql.h>

MXS_BEGIN_DECLS

/**
 * How often to call the router status function (seconds)
 */
#define AVRO_NSTATS_MINUTES 30

/**
 * Avro block grouping defaults
 */
#define AVRO_DEFAULT_BLOCK_TRX_COUNT 1
#define AVRO_DEFAULT_BLOCK_ROW_COUNT 1000

#define MAX_MAPPED_TABLES 1024

#define GTID_TABLE_NAME        "gtid"
#define USED_TABLES_TABLE_NAME "used_tables"
#define MEMORY_DATABASE_NAME   "memory"
#define MEMORY_TABLE_NAME      MEMORY_DATABASE_NAME".mem_used_tables"
#define INDEX_TABLE_NAME       "indexing_progress"

/** Name of the file where the binlog to Avro conversion progress is stored */
#define AVRO_PROGRESS_FILE "avro-conversion.ini"

/** Buffer limits */
#define AVRO_SQL_BUFFER_SIZE 2048

/** Avro filename maxlen */
#ifdef NAME_MAX
#define AVRO_MAX_FILENAME_LEN NAME_MAX
#else
#define AVRO_MAX_FILENAME_LEN 255
#endif

static const char *avro_client_states[]      = { "Unregistered", "Registered", "Processing", "Errored" };
static const char *avro_client_client_mode[] = { "Catch-up", "Busy", "Wait_for_data" };

static const char *avro_domain         = "domain";
static const char *avro_server_id      = "server_id";
static const char *avro_sequence       = "sequence";
static const char *avro_event_number   = "event_number";
static const char *avro_event_type     = "event_type";
static const char *avro_timestamp      = "timestamp";
static const char *avro_client_ouput[] = { "Undefined", "JSON", "Avro" };

static inline bool is_reserved_word(const char* word)
{
    return strcasecmp(word, avro_domain)       == 0 ||
           strcasecmp(word, avro_server_id)    == 0 ||
           strcasecmp(word, avro_sequence)     == 0 ||
           strcasecmp(word, avro_event_number) == 0 ||
           strcasecmp(word, avro_event_type)   == 0 ||
           strcasecmp(word, avro_timestamp)    == 0;
}

static inline void fix_reserved_word(char *tok)
{
    if (is_reserved_word(tok))
    {
        strcat(tok, "_");
    }
}

/** How a binlog file is closed */
typedef enum avro_binlog_end
{
    AVRO_OK = 0,                /**< A newer binlog file exists with a rotate event to that file */
    AVRO_LAST_FILE,             /**< Last binlog which is closed */
    AVRO_OPEN_TRANSACTION,      /**< The binlog ends with an open transaction */
    AVRO_BINLOG_ERROR           /**< An error occurred while processing the binlog file */
} avro_binlog_end_t;

/** How many numbers each table version has (db.table.000001.avro) */
#define TABLE_MAP_VERSION_DIGITS 6

/** Maximum version number*/
#define TABLE_MAP_VERSION_MAX 999999

/** Maximum column name length */
#define TABLE_MAP_MAX_NAME_LEN 64

/** How many bytes each thread tries to send */
#define AVRO_DATA_BURST_SIZE (32 * 1024)

/** A CREATE TABLE abstraction */
typedef struct table_create
{
    uint64_t columns;
    char**   column_names;
    char**   column_types;
    int*     column_lengths;
    char*    table;
    char*    database;
    int      version;           /**< How many versions of this table have been used */
    bool     was_used;          /**< Has this schema been persisted to disk */
} TABLE_CREATE;

/** A representation of a table map event read from a binary log. A table map
 * maps a table to a unique ID which can be used to match row events to table map
 * events. The table map event tells us how the table is laid out and gives us
 * some meta information on the columns. */
typedef struct table_map
{
    uint64_t      id;
    uint64_t      columns;
    uint16_t      flags;
    uint8_t*      column_types;
    uint8_t*      null_bitmap;
    uint8_t*      column_metadata;
    size_t        column_metadata_size;
    TABLE_CREATE* table_create; /*< The definition of the table */
    int           version;
    char          version_string[TABLE_MAP_VERSION_DIGITS + 1];
    char*         table;
    char*         database;
} TABLE_MAP;

/**
 * The statistics for this AVRO router instance
 */
typedef struct
{
    int n_clients;              /*< Number client sessions created */
} AVRO_ROUTER_STATS;

typedef struct avro_table_t
{
    char*               filename; /*< Absolute filename */
    char*               json_schema; /*< JSON representation of the schema */
    avro_file_writer_t  avro_file; /*< Current Avro data file */
    avro_value_iface_t* avro_writer_iface; /*< Avro C API writer interface */
    avro_schema_t       avro_schema; /*< Native Avro schema of the table */
} AVRO_TABLE;

/** Data format used when streaming data to the clients */
enum avro_data_format
{
    AVRO_FORMAT_UNDEFINED,
    AVRO_FORMAT_JSON,
    AVRO_FORMAT_AVRO,
};

enum mxs_avro_codec_type
{
    MXS_AVRO_CODEC_NULL,
    MXS_AVRO_CODEC_DEFLATE,
    MXS_AVRO_CODEC_SNAPPY,      /**< Not yet implemented */
} ;

typedef struct gtid_pos
{
    uint32_t timestamp;         /*< GTID event timestamp */
    uint64_t domain;            /*< Replication domain */
    uint64_t server_id;         /*< Server ID */
    uint64_t seq;               /*< Sequence number */
    uint64_t event_num; /*< Subsequence number, increases monotonically. This
                         * is an internal representation of the position of
                         * an event inside a GTID event and it is used to
                         * rebuild GTID events in the correct order. */
} gtid_pos_t;

struct Avro
{
    SERVICE*                 service; /*< Pointer to the service using this router */
    int                      initbinlog; /*< Initial binlog file number */
    char*                    fileroot; /*< Root of binlog filename */
    unsigned int             state; /*< State of the AVRO router */
    uint8_t                  lastEventReceived; /*< Last even received */
    uint32_t                 lastEventTimestamp; /*< Timestamp from last event */
    char*                    binlogdir; /*< The directory where the binlog files are stored */
    char*                    avrodir; /*< The directory with the AVRO files */
    char                     binlog_name[BINLOG_FNAMELEN + 1]; /*< Name of the current binlog file */
    uint64_t                 binlog_position; /*< last committed transaction position */
    uint64_t                 current_pos; /*< Current binlog position */
    int                      binlog_fd; /*< File descriptor of the binlog file being read */
    pcre2_code*              create_table_re;
    pcre2_code*              alter_table_re;
    uint8_t                  event_types;
    uint8_t                  event_type_hdr_lens[MAX_EVENT_TYPE_END];
    uint8_t                  binlog_checksum;
    gtid_pos_t               gtid;
    TABLE_MAP*               active_maps[MAX_MAPPED_TABLES];
    HASHTABLE*               table_maps;
    HASHTABLE*               open_tables;
    HASHTABLE*               created_tables;
    sqlite3*                 sqlite_handle;
    char                     prevbinlog[BINLOG_FNAMELEN + 1];
    int                      rotating; /*< Rotation in progress flag */
    AVRO_ROUTER_STATS        stats; /*< Statistics for this router */
    int                      task_delay; /*< Delay in seconds until the next conversion takes place */
    uint64_t                 trx_count; /*< Transactions processed */
    uint64_t                 trx_target; /*< Minimum about of transactions that will trigger
                                          * a flush of all tables */
    uint64_t                 row_count; /*< Row events processed */
    uint64_t                 row_target; /*< Minimum about of row events that will trigger
                                          * a flush of all tables */
    uint64_t                 block_size; /**< Avro datablock size */
    enum mxs_avro_codec_type codec; /**< Avro codec type, defaults to `null` */
    uint32_t                 task_handle; /**< Delayed task handle */
};

struct AvroSession
{
    DCB*                  dcb;  /*< The client DCB */
    int                   state; /*< The state of this client */
    enum avro_data_format format; /*< Stream JSON or Avro data */
    char*                 uuid; /*< Client UUID */
    SPINLOCK              catch_lock; /*< Event catchup lock */
    SPINLOCK              file_lock; /*< Protects rses_deleted */
    Avro*                 router; /*< Pointer to the owning router */
    MAXAVRO_FILE*         file_handle; /*< Current open file handle */
    uint64_t              last_sent_pos; /*< The last record we sent */
    time_t                connect_time; /*< Connect time of slave */
    MAXAVRO_FILE          avro_file; /*< Avro file struct */
    char                  avro_binfile[AVRO_MAX_FILENAME_LEN + 1];
    bool                  requested_gtid; /*< If the client requested */
    gtid_pos_t            gtid; /*< Current/requested GTID */
    gtid_pos_t            gtid_start; /*< First sent GTID */
    unsigned int          cstate; /*< Catch up state */
    sqlite3*              sqlite_handle;
};

extern void read_table_info(uint8_t *ptr, uint8_t post_header_len, uint64_t *table_id,
                            char* dest, size_t len);
extern TABLE_MAP *table_map_alloc(uint8_t *ptr, uint8_t hdr_len, TABLE_CREATE* create);
extern void table_map_free(TABLE_MAP *map);
extern TABLE_CREATE* table_create_alloc(char* ident, const char* sql, int len);
extern TABLE_CREATE* table_create_copy(Avro *router, const char* sql, size_t len, const char* db);
extern void table_create_free(TABLE_CREATE* value);
extern bool table_create_save(TABLE_CREATE *create, const char *filename);
extern bool table_create_alter(TABLE_CREATE *create, const char *sql, const char *end);
extern void read_table_identifier(const char* db, const char *sql, const char *end, char *dest, int size);
extern int avro_client_handle_request(Avro *, AvroSession *, GWBUF *);
extern void avro_client_rotate(Avro *router, AvroSession *client, uint8_t *ptr);
extern bool avro_open_binlog(const char *binlogdir, const char *file, int *fd);
extern void avro_close_binlog(int fd);
extern avro_binlog_end_t avro_read_all_events(Avro *router);
extern AVRO_TABLE* avro_table_alloc(const char* filepath, const char* json_schema,
                                    const char *codec, size_t block_size);
extern void avro_table_free(AVRO_TABLE *table);
extern char* json_new_schema_from_table(TABLE_MAP *map);
extern void save_avro_schema(const char *path, const char* schema, TABLE_MAP *map);
extern bool handle_table_map_event(Avro *router, REP_HEADER *hdr, uint8_t *ptr);
extern bool handle_row_event(Avro *router, REP_HEADER *hdr, uint8_t *ptr);

enum avrorouter_file_op
{
    AVROROUTER_SYNC,
    AVROROUTER_FLUSH
};

/**
 * @brief Flush or sync all tables
 *
 * @param router Router instance
 * @param flush AVROROUTER_SYNC for sync only or AVROROUTER_FLUSH for full flush
 */
extern void avro_flush_all_tables(Avro *router, enum avrorouter_file_op flush);

#define AVRO_CLIENT_UNREGISTERED 0x0000
#define AVRO_CLIENT_REGISTERED   0x0001
#define AVRO_CLIENT_REQUEST_DATA 0x0002
#define AVRO_CLIENT_ERRORED      0x0003
#define AVRO_CLIENT_MAXSTATE     0x0003

/**
 * Client catch-up status
 */
#define AVRO_CS_BUSY             0x0001
#define AVRO_WAIT_DATA           0x0002

MXS_END_DECLS
