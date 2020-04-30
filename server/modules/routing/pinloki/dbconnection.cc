/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "dbconnection.hh"
#include <maxbase/log.hh>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <unistd.h>

namespace maxsql
{

namespace
{
bool is_connection_lost(uint mariadb_err)
{
    return mariadb_err == 2006 || mariadb_err == 2013;
}
}

Connection::Connection(const ConnectionDetails& details)
    : m_details(details)
{
    _connect();
}

Connection::~Connection()
{
    if (m_rpl)
    {
        mariadb_rpl_close(m_rpl);
    }
    mysql_close(m_conn);
}

void Connection::start_replication(unsigned int server_id, maxsql::Gtid gtid)
{
    std::ostringstream gtid_start_pos;
    gtid_start_pos << "SET @slave_connect_state='" << (gtid.is_valid() ? gtid.to_string() : "") << '\'';

    // The heartbeat period is in nanoseconds
    auto hb = "SET @master_heartbeat_period=" + std::to_string((m_details.timeout.count() * 1000000000) / 2);

    // TODO use config
    std::vector<std::string> queries =
    {
        hb,
        "SET @master_binlog_checksum = @@global.binlog_checksum",
        "SET @mariadb_slave_capability=4",
        gtid_start_pos.str(),
        "SET @slave_gtid_strict_mode=1",
        "SET @slave_gtid_ignore_duplicates=1",
        "SET NAMES latin1"
    };

    for (const auto& sql : queries)
    {
        query(sql);
    }

    if (!(m_rpl = mariadb_rpl_init(m_conn)))
    {   // TODO this should be of a more fatal kind
        MXB_THROWCode(DatabaseError, mysql_errno(m_conn),
                      "mariadb_rpl_init failed " << m_details.host << " : mysql_error "
                                                 << mysql_error(m_conn));
    }

    mariadb_rpl_optionsv(m_rpl, MARIADB_RPL_SERVER_ID, server_id);
    mariadb_rpl_optionsv(m_rpl, MARIADB_RPL_START, 4);
    mariadb_rpl_optionsv(m_rpl, MARIADB_RPL_FLAGS, MARIADB_RPL_BINLOG_SEND_ANNOTATE_ROWS);

    if (mariadb_rpl_open(m_rpl))
    {
        MXB_THROWCode(DatabaseError, mysql_errno(m_conn),
                      "mariadb_rpl_open failed " << m_details.host << " : mysql_error "
                                                 << mysql_error(m_conn));
    }
}

MariaRplEvent Connection::get_rpl_msg()
{
    auto ptr = mariadb_rpl_fetch(m_rpl, nullptr);
    if (!ptr)
    {
        std::cerr << "OK " << mariadb_error_str() << '\n';
        throw std::runtime_error("dammit");
    }

    return MariaRplEvent {ptr, m_rpl};
}


uint Connection::mariadb_error()
{
    return mysql_errno(m_conn);
}

std::string Connection::mariadb_error_str()
{
    return mysql_error(m_conn);
}

uint Connection::ping()
{
    mysql_ping(m_conn);

    return mariadb_error();
}

void Connection::begin_trx()
{
    if (m_nesting_level++ == 0)
    {
        mysql_autocommit(m_conn, false);
        if (mysql_ping(m_conn))
        {
            MXB_THROWCode(DatabaseError, mysql_errno(m_conn),
                          "begin_tran failed " << m_details.host << " : mysql_error "
                                               << mysql_error(m_conn));
        }
    }
}

void Connection::commit_trx()
{
    if (--m_nesting_level == 0)
    {
        if (mysql_autocommit(m_conn, true))
        {
            MXB_THROWCode(DatabaseError, mysql_errno(m_conn),
                          "commit failed " << m_details.host << " : mysql_error "
                                           << mysql_error(m_conn));
        }
    }
}

void Connection::rollback_trx()
{
    if (mysql_rollback(m_conn))
    {
        MXB_THROWCode(DatabaseError, mysql_errno(m_conn),
                      "rollback failed " << m_details.host << " : mysql_error "
                                         << mysql_error(m_conn));
    }

    mysql_autocommit(m_conn, true);
    m_nesting_level = 0;
}

int Connection::nesting_level()
{
    return m_nesting_level;
}

void Connection::_connect()
{
    if (m_conn != nullptr)
    {
        MXB_THROW(DatabaseError, "connect(), already connected");
    }

    // LOGI("Trying " << _details.host);

    m_conn = mysql_init(nullptr);

    if (!m_conn)
    {
        MXB_THROW(DatabaseError, "mysql_init failed.");
    }

    unsigned int timeout = m_details.timeout.count();
    mysql_optionsv(m_conn, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_optionsv(m_conn, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    mysql_optionsv(m_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    // mysql_options(m_conn, MYSQL_OPT_NONBLOCK, 0);

    if (mysql_real_connect(m_conn,
                           m_details.host.address().c_str(),
                           m_details.user.c_str(),
                           m_details.password.c_str(),
                           m_details.database.c_str(),
                           (uint) m_details.host.port(),
                           nullptr, m_details.flags) == nullptr)
    {
        MXB_THROWCode(DatabaseError, mysql_errno(m_conn),
                      "Could not connect to " << m_details.host << " : mysql_error "
                                              << mysql_error(m_conn));
    }

    // LOGI("Connection ok: " << _details.host);
}

void Connection::query(const std::string& sql)
{
    mysql_real_query(m_conn, sql.c_str(), sql.size());
    auto err_code = mysql_errno(m_conn);

    if (err_code && !is_connection_lost(err_code))
    {
        MXB_THROWCode(DatabaseError, mysql_errno(m_conn),
                      "mysql_real_query: '" << sql << "' failed " << m_details.host.address()
                                            << ':' << m_details.host.port()
                                            << " : mysql_error " << mysql_error(m_conn));
    }
}

int Connection::affected_rows() const
{
    return mysql_affected_rows(m_conn);
}

void Connection::discard_result()
{
    // TODO. There must be a fast way, mariadb_cancel?
    auto res = result_set();
    for (auto ite = res.begin(); ite != res.end(); ++ite)
    {
    }
}

maxbase::Host Connection::host()
{
    return m_details.host;
}

ResultSet Connection::result_set()
{
    return ResultSet(m_conn);
}
}
