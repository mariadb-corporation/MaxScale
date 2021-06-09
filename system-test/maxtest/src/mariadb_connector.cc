/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <stdarg.h>
#include <maxbase/format.hh>
#include <maxtest/mariadb_connector.hh>
#include <maxsql/mariadb.hh>
#include <maxtest/log.hh>

maxtest::MariaDB::MariaDB(TestLogger& log)
    : m_log(log)
{
    // The test connector tries to automatically reconnect if a query fails.
    auto& sett = connection_settings();
    sett.auto_reconnect = true;
    sett.multiquery = true;
}

bool maxtest::MariaDB::open(const std::string& host, int port, const std::string& db)
{
    auto ret = mxq::MariaDB::open(host, port, db);
    m_log.expect(ret, "%s", error());
    return ret;
}

bool maxtest::MariaDB::try_open(const std::string& host, int port, const std::string& db)
{
    auto ret = mxq::MariaDB::open(host, port, db);
    if (!ret)
    {
        m_log.log_msgf("%s", error());
    }
    return ret;
}

bool maxtest::MariaDB::cmd(const std::string& sql, Expect expect)
{
    // The test connector can do one retry in case connection was lost.
    auto ret = mxq::MariaDB::cmd(sql);
    if (!ret && mxq::mysql_is_net_error(errornum()))
    {
        ret = mxq::MariaDB::cmd(sql);
    }

    if (expect == Expect::OK)
    {
        m_log.expect(ret, "%s", error());
    }
    else if (expect == Expect::FAIL)
    {
        m_log.expect(!ret, "Query '%s' succeeded when failure was expected.", sql.c_str());
    }
    else
    {
        if (!ret)
        {
            // Report query error, but don't classify it as a test error.
            m_log.log_msgf("%s", error());
        }
    }
    return ret;
}

bool maxtest::MariaDB::try_cmd(const std::string& sql)
{
    return cmd(sql, Expect::ANY);
}

bool maxtest::MariaDB::cmd_f(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string sql = mxb::string_vprintf(format, args);
    va_end(args);
    return cmd(sql);
}

bool maxtest::MariaDB::try_cmd_f(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string sql = mxb::string_vprintf(format, args);
    va_end(args);
    return cmd(sql, Expect::ANY);
}

std::unique_ptr<mxq::QueryResult> maxtest::MariaDB::query(const std::string& query, Expect expect)
{
    auto ret = mxq::MariaDB::query(query);
    if (!ret && mxq::mysql_is_net_error(errornum()))
    {
        ret = mxq::MariaDB::query(query);
    }
    if (expect == Expect::OK)
    {
        m_log.expect(ret != nullptr, "%s", error());
    }
    else if (expect == Expect::FAIL)
    {
        m_log.expect(ret == nullptr, "Query '%s' succeeded when failure was expected.", query.c_str());
    }
    else
    {
        if (!ret)
        {
            // Report query error, but don't classify it as a test error.
            m_log.log_msgf("%s", error());
        }
    }
    return ret;
}

std::unique_ptr<mxq::QueryResult> maxtest::MariaDB::try_query(const std::string& query)
{
    return this->query(query, Expect::ANY);
}
