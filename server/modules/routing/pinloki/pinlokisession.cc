/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pinlokisession.hh"
#include "rpl_event.hh"

#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/resultset.hh>
#include <maxbase/string.hh>

using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::steady_clock;

namespace
{

// Some common constants usually queried by various client libraries and monitoring solutions. The values were
// extracted from MariaDB 10.5.10 with minor modifications, namely @@license and @@sql_mode.
const std::map<std::string, std::string> constant_variables =
{
    {"@@session.auto_increment_increment", "1"                 },
    {"@@character_set_client",             "utf8"              },
    {"@@character_set_connection",         "utf8"              },
    {"@@character_set_results",            "utf8"              },
    {"@@character_set_server",             "utf8mb4"           },
    {"@@collation_server",                 "utf8mb4_general_ci"},
    {"@@collation_connection",             "utf8_general_ci"   },
    {"@@init_connect",                     ""                  },
    {"@@interactive_timeout",              "28800"             },
    {"@@license",                          "BSL"               },
    {"@@lower_case_table_names",           "0"                 },
    {"@@max_allowed_packet",               "16777216"          },
    {"@@net_write_timeout",                "60"                },
    {"@@performance_schema",               "0"                 },
    {"@@query_cache_size",                 "1048576"           },
    {"@@query_cache_type",                 "OFF"               },
    {"@@sql_mode",                         ""                  },
    {"@@system_time_zone",                 "UTC"               },
    {"@@time_zone",                        "SYSTEM"            },
    {"@@tx_isolation",                     "REPEATABLE-READ"   },
    {"@@wait_timeout",                     "28800"             },
};

GWBUF create_resultset(const std::vector<std::string>& columns, const std::vector<std::string>& row)
{
    auto rset = ResultSet::create(columns);

    if (!row.empty())
    {
        rset->add_row(row);
    }

    return rset->as_buffer();
}

GWBUF create_slave_running_error()
{
    return mariadb::create_error_packet(
        1, 1198, "HY000",
        "This operation cannot be performed as you have a running replica; run STOP SLAVE first");
}

GWBUF create_select_master_error()
{
    return mariadb::create_error_packet(
        1, 1198, "HY000",
        "Manual master configuration is not possible when `select_master=true` is used.");
}

GWBUF create_change_master_error(const std::string& err)
{
    return mariadb::create_error_packet(1, 1198, "HY000", err.c_str());
}
}

namespace pinloki
{
PinlokiSession::PinlokiSession(MXS_SESSION* pSession, Pinloki* router)
    : mxs::RouterSession(pSession)
    , m_router(router)
{
    pSession->client_dcb->add_callback(DCB::Reason::HIGH_WATER, high_water_mark_reached, this);
    pSession->client_dcb->add_callback(DCB::Reason::LOW_WATER, low_water_mark_reached, this);
}

PinlokiSession::~PinlokiSession()
{
    if (m_mgw_dcid)
    {
        m_pSession->cancel_dcall(m_mgw_dcid);
    }
}

bool PinlokiSession::routeQuery(GWBUF&& buf)
{
    int rval = 0;
    auto cmd = mariadb::get_command(buf);
    GWBUF response;

    switch (cmd)
    {
    case MXS_COM_REGISTER_SLAVE:
        // Register slave (maybe grab the slave's server_id if we need it)
        MXB_INFO("COM_REGISTER_SLAVE");
        response = mariadb::create_ok_packet();
        break;

    case MXS_COM_XPAND_REPL:
        response = mariadb::create_error_packet(1, 1236, "HY000",
                                                "XPand replication is not supported.");
        rval = 1;
        break;

    case MXS_COM_BINLOG_DUMP:
        // Start dumping binlogs
        MXB_INFO("COM_BINLOG_DUMP");
        try
        {
            pinloki::SendCallback send_cb = [this](const mxq::RplEvent& event) {
                return send_event(event);
            };
            pinloki::WorkerCallback worker_cb = [this]() -> mxb::Worker& {
                return *m_pSession->worker();
            };
            pinloki::AbortCallback abort_cb = [this](){
                this->m_pSession->kill();
            };

            m_reader = std::make_unique<Reader>(
                send_cb, worker_cb, abort_cb, m_router->inventory()->config(),
                m_gtid_list, std::chrono::seconds(m_heartbeat_period));

            // The call to start() can end up sending events to the client. To make sure we don't call
            // clientReply inside routeQuery, do the actual start inside of a lcall. If the session is closed
            // right after routeQuery returns, we might end up starting and stopping the reader but it'll
            // still function as expected.
            m_pSession->worker()->lcall([this](){
                m_reader->start();
            });

            rval = 1;
        }
        catch (const GtidNotFoundError& err)
        {
            MXB_SINFO("Could not find GTID: " << err.what());
            response = mariadb::create_error_packet(1, 1236, "HY000", err.what());
            rval = 1;
        }
        catch (const BinlogReadError& err)
        {
            MXB_ERROR("Binlog read error: %s", err.what());
        }
        catch (const mxq::EncryptionError& err)
        {
            MXB_ERROR("Encryption error: %s", err.what());
        }
        break;

    case MXS_COM_QUERY:
        try
        {
            const auto& sql = get_sql_string(buf);
            MXB_DEBUG("COM_QUERY: %s", sql.c_str());
            parser::parse(sql, this);
            rval = 1;
        }
        catch (const BinlogWriteError& err)
        {
            MXB_ERROR("Binlog write error: %s", err.what());
        }
        break;

    case COM_QUIT:
        rval = 1;
        break;

    case MXS_COM_PING:
        response = mariadb::create_ok_packet();
        break;

    default:
        MXB_ERROR("Unrecognized command %i", cmd);
    }

    if (response)
    {
        set_response(std::move(response));
        rval = 1;
    }

    return rval;
}

bool PinlokiSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert_message(!true, "This should not happen");
    return 0;
}

GWBUF PinlokiSession::make_buffer(Prefix prefix, const uint8_t* ptr, size_t size)
{
    GWBUF buffer(MYSQL_HEADER_LEN + size + prefix);

    mariadb::set_byte3(buffer.data(), size + prefix);
    buffer.data()[3] = m_seq++;
    if (prefix == PREFIX_OK)
    {
        buffer.data()[MYSQL_HEADER_LEN] = 0;
    }

    if (size)
    {
        memcpy(buffer.data() + MYSQL_HEADER_LEN + prefix, ptr, size);
    }

    return buffer;
}

void PinlokiSession::send_event(const maxsql::RplEvent& event)
{
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(event.pBuffer());
    long size = event.buffer_size();
    Prefix prefix = PREFIX_OK;

    while (size > 0)
    {
        size_t payload_len = std::min(size, GW_MYSQL_MAX_PACKET_LEN - prefix);
        send(make_buffer(prefix, ptr, payload_len));

        if (size == GW_MYSQL_MAX_PACKET_LEN - prefix)
        {
            send(make_buffer(PREFIX_NONE, nullptr, 0));
        }

        prefix = PREFIX_NONE;
        ptr += payload_len;
        size -= payload_len;
    }
}

void PinlokiSession::send(GWBUF&& buffer)
{
    const mxs::ReplyRoute down;
    const mxs::Reply reply;
    mxs::RouterSession::clientReply(std::move(buffer), down, reply);
}

int PinlokiSession::high_water_mark_reached(DCB* dcb, DCB::Reason reason, void* userdata)
{
    PinlokiSession* pSession = static_cast<PinlokiSession*>(userdata);
    pSession->m_reader->set_in_high_water(true);
    return 0;
}

int PinlokiSession::low_water_mark_reached(DCB* dcb, DCB::Reason reason, void* userdata)
{
    PinlokiSession* pSession = static_cast<PinlokiSession*>(userdata);
    pSession->m_reader->set_in_high_water(false);

    auto callback = [pSession, ref = pSession->m_reader->get_ref()]() {
        if (auto r = ref.lock())
        {
            pSession->m_reader->send_events();
        }
    };

    pSession->m_pSession->worker()->execute(callback, mxs::RoutingWorker::EXECUTE_QUEUED);

    return 0;
}

void PinlokiSession::select(const std::vector<std::string>& fields, const std::vector<std::string>& aliases)
{
    static const std::set<std::string> gtid_pos_sel_var =
    {
        "@@gtid_slave_pos",
        "@@global.gtid_slave_pos",
        "@@gtid_current_pos",
        "@@global.gtid_current_pos",
        "@@gtid_binlog_pos",
        "@@global.gtid_binlog_pos"
    };

    static const std::set<std::string> version_vars =
    {
        "version()",
        "@@version",
        "@@global.version"
    };

    static const std::set<std::string> server_id_vars =
    {
        "@@server_id",
        "@@global.server_id"
    };

    auto values = fields;

    for (auto& a : values)
    {
        auto val = mxb::lower_case_copy(a);
        if (val == "unix_timestamp()")
        {
            a = std::to_string(time(nullptr));
        }
        else if (version_vars.count(val))
        {
            a = m_pSession->service->version_string();
        }
        else if (val == "@@version_comment")
        {
            a = "pinloki";      // Helps detect when something is replicating from pinloki.
        }
        else if (val == "@@read_only")
        {
            a = "1";    // Always in read-only mode
        }
        else if (val == "@@global.gtid_domain_id")
        {
            // Note: The slave that requests this doesn't use it for anything. It's only used
            //       to check whether the variable exists. Return the default domain 0.
            a = "0";
        }
        else if (val == "@master_binlog_checksum")
        {
            // TODO: Store the master's response to this (Connector-C doesn't
            //       seem to work without replication checksums).
            a = "CRC32";
        }
        else if (server_id_vars.count(val))
        {
            a = std::to_string(m_router->config().server_id());
        }
        else if (gtid_pos_sel_var.count(val))
        {
            a = m_router->gtid_io_pos().to_string();
        }
        else
        {
            auto it = constant_variables.find(val);

            if (it != constant_variables.end())
            {
                a = it->second;
            }
        }
    }

    set_response(create_resultset(aliases, values));
}

void PinlokiSession::set(const std::string& key, const std::string& value)
{
    GWBUF buf;

    if (key == "@slave_connect_state")
    {
        auto gtid_list = mxq::GtidList::from_string(value);

        if (!gtid_list.is_valid())
        {
            const char* const msg = "Replica trying to connect with "
                                    "invalid GTID (@@slave_connect_state)";
            MXB_WARNING(msg);
            buf = mariadb::create_error_packet(1, 1941, "HY000", msg);
        }
        else
        {
            m_gtid_list = std::move(gtid_list);
            buf = mariadb::create_ok_packet();
        }
    }
    else if (key == "@master_heartbeat_period")
    {
        m_heartbeat_period = strtol(value.c_str(), nullptr, 10) / 1000000000;
        buf = mariadb::create_ok_packet();
    }
    else if (key == "gtid_slave_pos")
    {
        mxq::GtidList gtid_list = mxq::GtidList::from_string(value);

        if (!gtid_list.is_valid())
        {
            buf = mariadb::create_error_packet(1, 1941, "HY000",
                                               "Could not parse GTID");
        }
        else if (m_router->is_slave_running())
        {
            buf = mariadb::create_error_packet(
                1, 1198, "HY000",
                "This operation cannot be performed as you have a running replica;"
                " run STOP SLAVE first");
        }
        else
        {
            m_router->set_gtid_slave_pos(gtid_list);
            buf = mariadb::create_ok_packet();
        }
    }
    else
    {
        MXB_SINFO("Ignore set " << key << " = " << value);
        buf = mariadb::create_ok_packet();
    }

    set_response(std::move(buf));
}

void PinlokiSession::change_master_to(const parser::ChangeMasterValues& values)
{
    GWBUF buf;

    if (m_router->is_slave_running())
    {
        buf = create_slave_running_error();
    }
    else
    {
        auto err_str = m_router->change_master(values);
        if (err_str.empty())
        {
            buf = mariadb::create_ok_packet();
        }
        else
        {
            buf = create_change_master_error(err_str);
        }
    }

    set_response(std::move(buf));
}

void PinlokiSession::start_slave()
{
    GWBUF buf;

    std::string err_str = m_router->start_slave();

    if (err_str.empty())
    {
        buf = mariadb::create_ok_packet();
    }
    else
    {
        // Slave not configured
        buf = mariadb::create_error_packet(
            1, 1200, "HY000",
            err_str.c_str());
    }

    set_response(std::move(buf));
}

void PinlokiSession::stop_slave()
{
    if (m_router->is_slave_running())
    {
        m_router->stop_slave();
    }

    set_response(mariadb::create_ok_packet());
}

void PinlokiSession::reset_slave()
{
    GWBUF buf;

    if (m_router->is_slave_running())
    {
        buf = create_slave_running_error();
    }
    else if (m_router->config().select_master())
    {
        buf = create_select_master_error();
    }
    else
    {
        m_router->reset_slave();
        buf = mariadb::create_ok_packet();
    }

    set_response(std::move(buf));
}

void PinlokiSession::show_slave_status(bool all)
{
    set_response(m_router->show_slave_status(all));
}

void PinlokiSession::show_master_status()
{
    auto files = m_router->inventory()->file_names();
    auto rset = ResultSet::create({"File", "Position", "Binlog_Do_DB", "Binlog_Ignore_DB"});

    if (!files.empty())
    {
        auto a = get_file_name_and_size(files.back());
        rset->add_row({a.first, a.second, "", ""});
    }

    set_response(rset->as_buffer());
}

void PinlokiSession::show_binlogs()
{
    auto rset = ResultSet::create({"Log_name", "File_size"});

    for (const auto& file : m_router->inventory()->file_names())
    {
        auto a = get_file_name_and_size(file);
        rset->add_row({a.first, a.second});
    }

    set_response(rset->as_buffer());
}

void PinlokiSession::show_variables(const std::string& like)
{
    static const std::set<std::string> gtid_pos_var = {
        "gtid_slave_pos", "gtid_current_pos", "gtid_binlog_pos"
    };

    std::vector<std::string> values;

    auto val = mxb::lower_case_copy(like);

    if (val == "server_id")
    {
        values = {like, std::to_string(m_router->config().server_id())};
    }
    else if (gtid_pos_var.count(val))
    {
        values = {like, m_router->gtid_io_pos().to_string()};
    }

    set_response(create_resultset({"Variable_name", "Value"}, values));
}

GWBUF PinlokiSession::check_mgw_status(const std::string& header, const mxq::GtidList& target,
                                       std::chrono::steady_clock::time_point start, int timeout)
{
    GWBUF result;

    if (m_router->gtid_io_pos().is_included(target))
    {
        result = create_resultset({header}, {"0"});
    }
    else if (duration_cast<seconds>(steady_clock::now() - start).count() > timeout)
    {
        result = create_resultset({header}, {"-1"});
    }

    return result;
}

void PinlokiSession::master_gtid_wait(const std::string& gtid, int timeout)
{
    mxb_assert(m_mgw_dcid == 0);
    auto header = "master_gtid_wait('" + gtid + "', " + std::to_string(timeout) + ")";
    auto target = mxq::GtidList::from_string(gtid);
    auto start = steady_clock::now();

    auto cb = [this, start, target, timeout, header]() {
        bool again = true;

        if (GWBUF buf = check_mgw_status(header, target, start, timeout))
        {
            send(std::move(buf));
            m_mgw_dcid = 0;
            again = false;
        }

        return again;
    };

    if (target.is_valid())
    {
        if (GWBUF buf = check_mgw_status(header, target, start, timeout))
        {
            set_response(std::move(buf));
        }
        else
        {
            m_mgw_dcid = m_pSession->dcall(1000ms, cb);
        }
    }
    else
    {
        set_response(create_resultset({header}, {"-1"}));
    }
}

void PinlokiSession::purge_logs(const std::string& up_to)
{
    switch (purge_binlogs(m_router->inventory()->config(), up_to))
    {
    case PurgeResult::Ok:
        set_response(mariadb::create_ok_packet());
        break;

    case PurgeResult::PartialPurge:
        MXB_SINFO("Could not purge all requested binlogs");
        set_response(mariadb::create_ok_packet());
        break;

    case PurgeResult::UpToFileNotFound:
        set_response(mariadb::create_error_packet(
            1, 1373, "HY000",
            MAKE_STR("Target log " << up_to << " not found in binlog index").c_str()));
    }
}

void PinlokiSession::error(const std::string& err)
{
    mxb_assert(!err.empty());
    set_response(mariadb::create_error_packet(1, 1064, "42000", err.c_str()));
}
}
