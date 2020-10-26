/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXS_MODULE_NAME "avrorouter"

#include <maxscale/ccdefs.hh>
#include <stdbool.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <mysql.h>
#include <maxbase/alloc.h>
#include <maxscale/dcb.hh>
#include <maxscale/service.hh>
#include <maxscale/mysql_binlog.h>
#include <maxscale/users.hh>
#include <maxscale/router.hh>
#include <maxscale/protocol/cdc/cdc.hh>
#include <maxavro.hh>
#include <blr_constants.hh>

#include "../replicator/replicator.hh"

/** Name of the file where the binlog to Avro conversion progress is stored */
#define AVRO_PROGRESS_FILE "avro-conversion.ini"

static const char* avro_client_states[] = {"Unregistered", "Registered", "Processing", "Errored"};
static const char* avro_client_client_mode[] = {"Catch-up", "Busy", "Wait_for_data"};
static const char* avro_client_ouput[] = {"Undefined", "JSON", "Avro"};

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
    {"null",    MXS_AVRO_CODEC_NULL   },
    {"deflate", MXS_AVRO_CODEC_DEFLATE},
// Not yet implemented
//    {"snappy", MXS_AVRO_CODEC_SNAPPY},
    {NULL}
};

class AvroConfig : public mxs::config::Configuration
{
public:
    AvroConfig(const std::string& name);

    std::string             filestem;
    std::string             binlogdir;
    std::string             avrodir;
    std::string             gtid;
    int64_t                 trx_target;
    int64_t                 row_target;
    int64_t                 server_id;
    int64_t                 start_index;
    int64_t                 block_size;
    mxs::config::RegexValue match;
    mxs::config::RegexValue exclude;
    mxs_avro_codec_type     codec;
    bool                    cooperative_replication;
};

class AvroSession;

class Avro : public mxs::Router
{
    Avro(const Avro&) = delete;
    Avro& operator=(const Avro&) = delete;

public:
    static Avro* create(SERVICE* service, mxs::ConfigParameters* params);

    mxs::RouterSession* newSession(MXS_SESSION* session, const mxs::Endpoints& endpoints);

    json_t* diagnostics() const;

    uint64_t getCapabilities() const
    {
        return RCAP_TYPE_NONE;
    }

    bool configure(mxs::ConfigParameters* params)
    {
        return false;
    }

    mxs::config::Configuration* getConfiguration()
    {
        return &m_config;
    }

    const AvroConfig& config() const
    {
        return m_config;
    }

    SERVICE*    service;    /*< Pointer to the service using this router */
    std::string binlog_name;/*< Name of the current binlog file */
    uint64_t    current_pos;/*< Current binlog position */
    int         binlog_fd;  /*< File descriptor of the binlog file being read */
    int64_t     trx_count;  /*< Transactions processed */
    int64_t     row_count;  /*< Row events processed */
    uint32_t    task_handle;/**< Delayed task handle */

    std::unique_ptr<Rpl> handler;

private:
    std::unique_ptr<cdc::Replicator> m_replicator;
    AvroConfig                       m_config;

    Avro(SERVICE* service, mxs::ConfigParameters* params, AvroConfig&& config);
};

class AvroSession : public mxs::RouterSession
{
    AvroSession(const AvroSession&) = delete;
    AvroSession& operator=(const AvroSession&) = delete;
public:

    static AvroSession* create(Avro* router, MXS_SESSION* session);
    virtual ~AvroSession();

    /**
     * Process a client request
     *
     * @param Buffer The incoming request packet
     *
     * @return 1 on success, 0 on error
     */
    int32_t routeQuery(GWBUF* buffer);

    int32_t clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
    {
        mxb_assert(!true);
        return 0;
    }

    bool handleError(mxs::ErrorType, GWBUF*, mxs::Endpoint*, const mxs::Reply&)
    {
        /** We should never end up here */
        mxb_assert(false);
        return false;
    }

    void queue_client_callback();

    static void notify_all_clients(SERVICE* router);

private:
    MXS_SESSION*         m_session {nullptr};   /**< Generic session */
    CDCClientConnection* m_client {nullptr};    /**< Client connection */

    int              m_state;           /*< The state of this client */
    avro_data_format m_format;          /*< Stream JSON or Avro data */
    std::string      m_uuid;            /*< Client UUID */
    Avro*            m_router;          /*< Pointer to the owning router */
    MAXAVRO_FILE*    m_file_handle;     /*< Current open file handle */
    uint64_t         m_last_sent_pos;   /*< The last record we sent */
    time_t           m_connect_time;    /*< Connect time of slave */
    std::string      m_avro_binfile;
    bool             m_requested_gtid;  /*< If the client requested */
    gtid_pos_t       m_gtid;            /*< Current/requested GTID */
    gtid_pos_t       m_gtid_start;      /*< First sent GTID */

    AvroSession(Avro* instance, MXS_SESSION* session);

    int  do_registration(GWBUF* data);
    void process_command(GWBUF* queue);
    void send_gtid_info(gtid_pos_t* gtid_pos);
    void set_current_gtid(json_t* row);
    bool stream_json();
    bool stream_binary();
    bool seek_to_gtid();
    bool stream_data();
    void rotate_avro_file(std::string fullname);
    void client_callback();
    int  send_row(json_t* row);
};

bool              avro_open_binlog(const char* binlogdir, const char* file, int* fd);
avro_binlog_end_t avro_read_all_events(Avro* router);
bool              avro_save_conversion_state(Avro* router);
bool              avro_load_conversion_state(Avro* router);
bool              conversion_task_ctl(Avro* inst, bool start);
