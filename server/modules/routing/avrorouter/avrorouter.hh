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
#include <string>
#include <vector>
#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/service.h>
#include <maxscale/spinlock.h>
#include <maxscale/mysql_binlog.h>
#include <maxscale/users.h>
#include <cdc.h>
#include <maxscale/pcre2.h>
#include <maxavro.h>
#include <binlog_common.h>
#include <maxscale/protocol/mysql.h>
#include <blr_constants.h>

#include "rpl_events.hh"

MXS_BEGIN_DECLS

/** Name of the file where the binlog to Avro conversion progress is stored */
#define AVRO_PROGRESS_FILE "avro-conversion.ini"

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
};

static const MXS_ENUM_VALUE codec_values[] =
{
    {"null", MXS_AVRO_CODEC_NULL},
    {"deflate",  MXS_AVRO_CODEC_DEFLATE},
// Not yet implemented
//    {"snappy", MXS_AVRO_CODEC_SNAPPY},
    {NULL}
};

typedef std::tr1::unordered_map<std::string, STableCreateEvent> CreatedTables;
typedef std::tr1::unordered_map<std::string, STableMapEvent>    MappedTables;
typedef std::tr1::unordered_map<uint64_t, STableMapEvent>       ActiveMaps;

class Avro: public MXS_ROUTER
{
    Avro(const Avro&) = delete;
    Avro& operator=(const Avro&) = delete;

public:
    static Avro* create(SERVICE* service);

    SERVICE*                 service; /*< Pointer to the service using this router */
    std::string              filestem; /*< Root of binlog filename */
    std::string              binlogdir; /*< The directory where the binlog files are stored */
    std::string              avrodir; /*< The directory with the AVRO files */
    std::string              binlog_name; /*< Name of the current binlog file */
    uint64_t                 current_pos; /*< Current binlog position */
    int                      binlog_fd; /*< File descriptor of the binlog file being read */
    pcre2_code*              create_table_re;
    pcre2_code*              alter_table_re;
    uint8_t                  event_types;
    uint8_t                  event_type_hdr_lens[MAX_EVENT_TYPE_END];
    uint8_t                  binlog_checksum;
    gtid_pos_t               gtid;
    ActiveMaps               active_maps;
    MappedTables             table_maps;
    CreatedTables            created_tables;
    uint64_t                 trx_count; /*< Transactions processed */
    uint64_t                 trx_target; /*< Minimum about of transactions that will trigger
                                          * a flush of all tables */
    uint64_t                 row_count; /*< Row events processed */
    uint64_t                 row_target; /*< Minimum about of row events that will trigger
                                          * a flush of all tables */
    uint32_t                 task_handle; /**< Delayed task handle */
    RowEventHandler*         event_hander;

    struct
    {
        int n_clients; /*< Number client sessions created */
    } stats; /*< Statistics for this router */

private:
    Avro(SERVICE* service, MXS_CONFIG_PARAMETER* params, SERVICE* source);
    void read_source_service_options(SERVICE* source);
};

class AvroSession: public MXS_ROUTER_SESSION
{
    AvroSession(const AvroSession&) = delete;
    AvroSession& operator=(const AvroSession&) = delete;
public:

    static AvroSession* create(Avro* router, MXS_SESSION* session);
    ~AvroSession();

    DCB*                  dcb;  /*< The client DCB */
    int                   state; /*< The state of this client */
    enum avro_data_format format; /*< Stream JSON or Avro data */
    std::string           uuid; /*< Client UUID */
    SPINLOCK              catch_lock; /*< Event catchup lock */
    Avro*                 router; /*< Pointer to the owning router */
    MAXAVRO_FILE*         file_handle; /*< Current open file handle */
    uint64_t              last_sent_pos; /*< The last record we sent */
    time_t                connect_time; /*< Connect time of slave */
    std::string           avro_binfile;
    bool                  requested_gtid; /*< If the client requested */
    gtid_pos_t            gtid; /*< Current/requested GTID */
    gtid_pos_t            gtid_start; /*< First sent GTID */

    /**
     * Process a client request
     *
     * @param Buffer The incoming request packet
     *
     * @return 1 on success, 0 on error
     */
    int routeQuery(GWBUF* buffer);

    /**
     * Handler for the EPOLLOUT event
     */
    void client_callback();

private:
    AvroSession(Avro* instance, MXS_SESSION* session);

    int do_registration(GWBUF *data);
    void process_command(GWBUF *queue);
    void send_gtid_info(gtid_pos_t *gtid_pos);
    void set_current_gtid(json_t *row);
    bool stream_json();
    bool stream_binary();
    bool seek_to_gtid();
    bool stream_data();
    void rotate_avro_file(std::string fullname);
};

void read_table_info(uint8_t *ptr, uint8_t post_header_len, uint64_t *table_id, char* dest, size_t len);
TableMapEvent *table_map_alloc(uint8_t *ptr, uint8_t hdr_len, TableCreateEvent* create);
TableCreateEvent* table_create_alloc(char* ident, const char* sql, int len);
TableCreateEvent* table_create_copy(Avro *router, const char* sql, size_t len, const char* db);
bool table_create_save(TableCreateEvent *create, const char *filename);
bool table_create_alter(TableCreateEvent *create, const char *sql, const char *end);
TableCreateEvent* table_create_from_schema(const char* file, const char* db, const char* table,
                                           int version);
void read_table_identifier(const char* db, const char *sql, const char *end, char *dest, int size);
int avro_client_handle_request(Avro *, AvroSession *, GWBUF *);
void avro_client_rotate(Avro *router, AvroSession *client, uint8_t *ptr);
bool avro_open_binlog(const char *binlogdir, const char *file, int *fd);
void avro_close_binlog(int fd);
avro_binlog_end_t avro_read_all_events(Avro *router);
char* json_new_schema_from_table(const STableMapEvent& map, const STableCreateEvent& create);
bool handle_table_map_event(Avro *router, REP_HEADER *hdr, uint8_t *ptr);
bool handle_row_event(Avro *router, REP_HEADER *hdr, uint8_t *ptr);
void handle_one_event(Avro* router, uint8_t* ptr, REP_HEADER& hdr, uint64_t& pos);
REP_HEADER construct_header(uint8_t* ptr);
bool avro_save_conversion_state(Avro *router);
bool avro_load_conversion_state(Avro *router);
void avro_load_metadata_from_schemas(Avro *router);
void notify_all_clients(Avro *router);

MXS_END_DECLS
