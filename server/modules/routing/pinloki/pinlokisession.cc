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

namespace
{
GWBUF* create_resultset(const std::initializer_list<std::string>& columns,
                        const std::initializer_list<std::string>& row)
{
    auto rset = ResultSet::create(columns);
    rset->add_row(row);
    return rset->as_buffer().release();
}
}

namespace pinloki
{
PinlokiSession::PinlokiSession(MXS_SESSION* pSession)
    : mxs::RouterSession(pSession)
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
        // Start dumping binlogs (not yet implemented)
        MXS_INFO("COM_BINLOG_DUMP");
        rval = 1;
        break;

    case MXS_COM_QUERY:
        {
            auto sql = mxs::extract_sql(buf.get());
            mxb::lower_case(sql);
            MXS_INFO("COM_QUERY: %s", sql.c_str());

            // TODO: Implement a proper SQL parser instead of comparing to hard-coded queries
            if (sql == "select unix_timestamp()")
            {
                response = create_resultset({"UNIX_TIMESTAMP()"}, {std::to_string(time(nullptr))});
            }
            else if (sql == "show variables like 'server_id'")
            {
                response = create_resultset({"Variable_name", "Value"}, {"server_id", "1"});
            }
            else if (sql == "select @master_binlog_checksum")
            {
                response = create_resultset({"@master_binlog_checksum"}, {"CRC32"});
            }
            else if (sql == "select 1")
            {
                response = create_resultset({"1"}, {"1"});
            }
            else if (sql == "select @@global.gtid_domain_id")
            {
                response = create_resultset({"@@GLOBAL.gtid_domain_id"}, {"0"});
            }
            else if (sql.substr(0, 4) == "set ")
            {
                response = modutil_create_ok();
            }
            else
            {
                mxb_assert(!true);
            }
        }
        break;
    }

    if (response)
    {
        const mxs::ReplyRoute down;
        const mxs::Reply reply;
        mxs::RouterSession::clientReply(response, down, reply);
        rval = 1;
    }
    else
    {
        mxb_assert(rval == 1);
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
    mxs::Buffer buffer(5 + event.data().size());

    // Wrap the events in a protocol packet with a command byte of 0x0
    mariadb::set_byte3(buffer.data(), event.data().size() + 1);
    buffer.data()[3] = m_seq++;
    buffer.data()[4] = 0x0;
    mempcpy(buffer.data() + 5, event.data().data(), event.data().size());

    const mxs::ReplyRoute down;
    const mxs::Reply reply;
    mxs::RouterSession::clientReply(buffer.release(), down, reply);

    // TODO: Stop sending events when the network buffer gets full
    return true;
}
}
