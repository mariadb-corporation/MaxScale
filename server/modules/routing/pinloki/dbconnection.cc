/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "dbconnection.hh"
#include <maxscale/log.hh>
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
    connect();
}

Connection::~Connection()
{
    if (m_rpl)
    {
        mariadb_rpl_close(m_rpl);
    }
    mysql_close(m_conn);
}

void Connection::start_replication(unsigned int server_id, maxsql::GtidList gtid)
{
    std::string gtid_str = gtid.is_valid() ? gtid.to_string() : "";

    // The heartbeat period is in nanoseconds. We need frequent updates to keep get_rpl_msg responsive.
    auto hb = "SET @master_heartbeat_period=1000000000";

    // TODO use config
    std::vector<std::string> queries =
    {
        hb,
        "SET @master_binlog_checksum = @@global.binlog_checksum",
        "SET @mariadb_slave_capability=4",
        "SET @slave_connect_state='" + gtid_str + "'",
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
        throw std::runtime_error("Failed to fetch binlog event from master: " + mariadb_error_str());
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

void Connection::connect()
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

    if (m_details.ssl)
    {
        uint8_t yes = 1;
        mysql_optionsv(m_conn, MYSQL_OPT_SSL_ENFORCE, &yes);

        if (!m_details.ssl_key.empty())
        {
            mysql_optionsv(m_conn, MYSQL_OPT_SSL_KEY, m_details.ssl_key.c_str());
        }
        if (!m_details.ssl_cert.empty())
        {
            mysql_optionsv(m_conn, MYSQL_OPT_SSL_CERT, m_details.ssl_cert.c_str());
        }
        if (!m_details.ssl_ca.empty())
        {
            mysql_optionsv(m_conn, MYSQL_OPT_SSL_CA, m_details.ssl_ca.c_str());
        }
        if (!m_details.ssl_capath.empty())
        {
            mysql_optionsv(m_conn, MYSQL_OPT_SSL_CAPATH, m_details.ssl_capath.c_str());
        }
        if (!m_details.ssl_cipher.empty())
        {
            mysql_optionsv(m_conn, MYSQL_OPT_SSL_CIPHER, m_details.ssl_cipher.c_str());
        }
        if (!m_details.ssl_crl.empty())
        {
            mysql_optionsv(m_conn, MYSQL_OPT_SSL_CRL, m_details.ssl_crl.c_str());
        }
        if (!m_details.ssl_crlpath.empty())
        {
            mysql_optionsv(m_conn, MYSQL_OPT_SSL_CRLPATH, m_details.ssl_crlpath.c_str());
        }
        if (m_details.ssl_verify_server_cert)
        {
            mysql_optionsv(m_conn, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &yes);
        }
    }

    if (mysql_real_connect(m_conn,
                           m_details.host.address().c_str(),
                           m_details.user.c_str(),
                           m_details.password.c_str(),
                           m_details.database.c_str(),
                           (uint) m_details.host.port(),
                           nullptr, m_details.flags) == nullptr)
    {
        std::string error = mysql_error(m_conn);
        auto errnum = mysql_errno(m_conn);
        mysql_close(m_conn);
        m_conn = nullptr;
        MXB_THROWCode(DatabaseError, errnum,
                      "Could not connect to " << m_details.host << " : mysql_error " << error);
    }
    else if (m_details.ssl && !mysql_get_ssl_cipher(m_conn))
    {
        mysql_close(m_conn);
        m_conn = nullptr;
        MXB_THROW(DatabaseError, "Could not establish an encrypted connection");
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

maxbase::Host Connection::host() const
{
    return m_details.host;
}

ResultSet Connection::result_set()
{
    return ResultSet(m_conn);
}
}
