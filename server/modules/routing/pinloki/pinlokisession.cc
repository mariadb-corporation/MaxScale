/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pinlokisession.hh"

#include <maxscale/modutil.hh>
#include <maxscale//protocol/mariadb/resultset.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
GWBUF* create_resultset(const std::vector<std::string>& columns, const std::vector<std::string>& row)
{
    auto rset = ResultSet::create(columns);

    if (!row.empty())
    {
        rset->add_row(row);
    }

    return rset->as_buffer().release();
}

GWBUF* create_slave_running_error()
{
    return modutil_create_mysql_err_msg(
        1, 0, 1198, "HY000",
        "This operation cannot be performed as you have a running slave; run STOP SLAVE first");
}

std::pair<std::string, std::string> get_file_name_and_size(const std::string& filepath)
{
    std::string file = filepath;
    std::string size = "0";

    if (!file.empty())
    {
        auto pos = file.find_last_of('/');

        if (pos != std::string::npos)
        {
            file = file.substr(pos + 1);
        }

        struct stat st;
        if (stat(filepath.c_str(), &st) == 0)
        {
            size = std::to_string(st.st_size);
        }
    }

    return {file, size};
}
}

namespace pinloki
{
PinlokiSession::PinlokiSession(MXS_SESSION* pSession, Pinloki* router)
    : mxs::RouterSession(pSession)
    , m_router(router)
{
}

void PinlokiSession::close()
{
}

int32_t PinlokiSession::routeQuery(GWBUF* pPacket)
{
    int rval = 0;
    GWBUF* response = nullptr;
    mxs::Buffer buf(pPacket);
    auto cmd = mxs_mysql_get_command(buf.get());

    switch (cmd)
    {
    case MXS_COM_REGISTER_SLAVE:
        // Register slave (maybe grab the slave's server_id if we need it)
        MXS_INFO("COM_REGISTER_SLAVE");
        response = modutil_create_ok();
        break;

    case MXS_COM_BINLOG_DUMP:
        // Start dumping binlogs
        MXS_INFO("COM_BINLOG_DUMP");
        try
        {
            pinloki::Callback cb = [this](const mxq::RplEvent& event) {
                    return send_event(event);
                };

            m_reader = std::make_unique<Reader>(
                cb, m_router->inventory(),
                mxs::RoutingWorker::get_current(),
                m_gtid, std::chrono::seconds(m_heartbeat_period));
            rval = 1;
        }
        catch (const GtidNotFoundError& err)
        {
            send(modutil_create_mysql_err_msg(1, 0, 1236, "HY000", err.what()));
            rval = 1;
        }
        catch (const BinlogReadError& err)
        {
            MXS_ERROR("%s", err.what());
        }
        break;

    case MXS_COM_QUERY:
        {
            auto sql = mxs::extract_sql(buf.get());
            MXS_INFO("COM_QUERY: %s", sql.c_str());
            parser::parse(sql, this);
            rval = 1;
        }
        break;

    case COM_QUIT:
        rval = 1;
        break;
    }

    if (response)
    {
        const mxs::ReplyRoute down;
        const mxs::Reply reply;
        mxs::RouterSession::clientReply(response, down, reply);
        rval = 1;
    }

    return rval;
}

void PinlokiSession::clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb_assert_message(!true, "This should not happen");
}

bool PinlokiSession::handleError(mxs::ErrorType type, GWBUF* pMessage,
                                 mxs::Endpoint* pProblem, const mxs::Reply& pReply)
{
    mxb_assert_message(!true, "This should not happen");
    return false;
}

bool PinlokiSession::send_event(const maxsql::RplEvent& event)
{
    // Not the prettiest way of detecting a full network buffer but it should work
    bool can_write = m_pSession->client_dcb->writeq() == nullptr;

    if (can_write)
    {
        mxs::Buffer buffer(5 + event.data().size());

        // Wrap the events in a protocol packet with a command byte of 0x0
        mariadb::set_byte3(buffer.data(), event.data().size() + 1);
        buffer.data()[3] = m_seq++;
        buffer.data()[4] = 0x0;
        mempcpy(buffer.data() + 5, event.data().data(), event.data().size());

        send(buffer.release());
    }
    else
    {
        MXS_DEBUG("Buffer full, %u bytes buffered", gwbuf_length(m_pSession->client_dcb->writeq()));
    }

    return can_write;
}

void PinlokiSession::send(GWBUF* buffer)
{
    const mxs::ReplyRoute down;
    const mxs::Reply reply;
    mxs::RouterSession::clientReply(buffer, down, reply);
}

void PinlokiSession::select(const std::vector<std::string>& fields)
{
    auto values = fields;

    for (auto& a : values)
    {
        auto val = mxb::lower_case_copy(a);
        if (val == "unix_timestamp()")
        {
            a = std::to_string(time(nullptr));
        }
        else if (val == "@@global.gtid_domain_id")
        {
            // TODO: Get this from either the master or the configuration
            val = "1";
        }
        else if (val == "@master_binlog_checksum")
        {
            // TODO: Store the master's response to this
            val = "CRC32";
        }
    }

    send(create_resultset(fields, values));
}

void PinlokiSession::set(const std::string& key, const std::string& value)
{
    if (key == "@slave_connect_state")
    {
        m_gtid = mxq::Gtid::from_string(value);
    }
    else if (key == "@master_heartbeat_period")
    {
        m_heartbeat_period = strtol(value.c_str(), nullptr, 10) / 1000000000;
    }
    else if (key == "gtid_slave_pos")
    {
        m_router->set_gtid(mxq::GtidList::from_string(value));
    }

    send(modutil_create_ok());
}

void PinlokiSession::change_master_to(const parser::ChangeMasterValues& values)
{
    GWBUF* buf = nullptr;

    if (m_router->is_slave_running())
    {
        buf = create_slave_running_error();
    }
    else
    {
        m_router->change_master(values);
        buf = modutil_create_ok();
    }

    send(buf);
}

void PinlokiSession::start_slave()
{
    GWBUF* buf = nullptr;

    if (m_router->start_slave())
    {
        buf = modutil_create_ok();
    }
    else
    {
        // Slave not configured
        buf = modutil_create_mysql_err_msg(
            1, 0, 1200, "HY000",
            "Misconfigured slave: MASTER_HOST was not set; Fix in config file or with CHANGE MASTER TO");
    }


    send(buf);
}

void PinlokiSession::stop_slave()
{
    m_router->stop_slave();
    send(modutil_create_ok());
}

void PinlokiSession::reset_slave()
{
    GWBUF* buf = nullptr;

    if (m_router->is_slave_running())
    {
        buf = create_slave_running_error();
    }
    else
    {
        m_router->reset_slave();
        buf = modutil_create_ok();
    }

    send(buf);
}

void PinlokiSession::show_slave_status()
{
    send(m_router->show_slave_status());
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

    send(rset->as_buffer().release());
}

void PinlokiSession::show_binlogs()
{
    auto rset = ResultSet::create({"Log_name", "File_size"});

    for (const auto& file : m_router->inventory()->file_names())
    {
        auto a = get_file_name_and_size(file);
        rset->add_row({a.first, a.second});
    }

    send(rset->as_buffer().release());
}

void PinlokiSession::show_variables(const std::string& like)
{
    std::vector<std::string> values;

    if (strcasestr(like.c_str(), "server_id") == 0)
    {
        values = {like, std::to_string(m_router->config().server_id())};
    }

    send(create_resultset({"Variable_name", "Value"}, values));
}

void PinlokiSession::purge_logs(const std::string& up_to)
{
    auto files = m_router->inventory()->file_names();

    auto it = std::find(files.begin(), files.end(), up_to);

    if (it != files.end())
    {
        for (auto start = files.begin(); start != it; start++)
        {
            m_router->inventory()->remove(*start);
            remove(start->c_str());
        }
    }

    send(modutil_create_ok());
}

void PinlokiSession::error(const std::string& err)
{
    send(modutil_create_mysql_err_msg(1, 0, 1064, "42000", err.c_str()));
}
}
