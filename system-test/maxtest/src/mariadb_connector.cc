/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
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

using std::string;

maxtest::MariaDB::MariaDB(TestLogger& log)
    : m_log(log)
{
    // The test connector tries to automatically reconnect if a query fails.
    auto& sett = connection_settings();
    sett.auto_reconnect = true;
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

mxt::ScopedUser
maxtest::MariaDB::create_user(const std::string& user, const std::string& host, const std::string& pw)
{
    mxt::ScopedUser rval;
    if (is_open())
    {
        auto user_host = mxb::string_printf("'%s'@'%s'", user.c_str(), host.c_str());
        if (cmd_f("create or replace user %s identified by '%s';", user_host.c_str(), pw.c_str()))
        {
            rval = ScopedUser(user_host, this);
        }
    }
    return rval;
}

maxtest::ScopedUser
maxtest::MariaDB::create_user_xpand(const string& user, const string& host, const string& pw)
{
    mxt::ScopedUser rval;
    if (is_open())
    {
        auto user_host = mxb::string_printf("'%s'@'%s'", user.c_str(), host.c_str());
        try_cmd_f("drop user %s;", user_host.c_str());
        if (cmd_f("create user %s identified by '%s';", user_host.c_str(), pw.c_str()))
        {
            rval = ScopedUser(user_host, this);
        }
    }
    return rval;
}

mxt::ScopedTable
maxtest::MariaDB::create_table(const std::string& name, const std::string& col_defs)
{
    mxt::ScopedTable rval;
    if (is_open())
    {
        if (cmd_f("create or replace table %s (%s);", name.c_str(), col_defs.c_str()))
        {
            rval = ScopedTable(name, this);
        }
    }
    return rval;
}

std::string maxtest::MariaDB::simple_query(const string& q)
{
    string rval;
    auto res = query(q);
    if (res)
    {
        if (res->next_row() && res->get_col_count() > 0)
        {
            rval = res->get_string(0);
        }
        else
        {
            m_log.add_failure("Query '%s' did not return any results.", q.c_str());
        }
    }
    return rval;
}

maxtest::ScopedUser::ScopedUser(std::string user_host, maxtest::MariaDB* conn)
    : m_user_host(std::move(user_host))
    , m_conn(conn)
{
}

maxtest::ScopedUser::~ScopedUser()
{
    if (m_conn)
    {
        m_conn->cmd_f("drop user %s;", m_user_host.c_str());
    }
}

void maxtest::ScopedUser::grant(const std::string& grant)
{
    if (m_conn)
    {
        m_conn->cmd_f("grant %s to %s;", grant.c_str(), m_user_host.c_str());
    }
}

void maxtest::ScopedUser::grant_f(const char* grant_fmt, ...)
{
    va_list args;
    va_start(args, grant_fmt);
    string grant_str = mxb::string_vprintf(grant_fmt, args);
    va_end(args);
    grant(grant_str);
}

maxtest::ScopedUser& maxtest::ScopedUser::operator=(maxtest::ScopedUser&& rhs)
{
    m_conn = rhs.m_conn;
    rhs.m_conn = nullptr;
    m_user_host = std::move(rhs.m_user_host);
    return *this;
}

maxtest::ScopedUser::ScopedUser(maxtest::ScopedUser&& rhs)
{
    *this = std::move(rhs);
}

maxtest::ScopedTable::ScopedTable(std::string name, maxtest::MariaDB* conn)
    : m_name(std::move(name))
    , m_conn(conn)
{
}

maxtest::ScopedTable::~ScopedTable()
{
    if (m_conn)
    {
        m_conn->cmd_f("drop table %s;", m_name.c_str());
    }
}

maxtest::ScopedTable& maxtest::ScopedTable::operator=(maxtest::ScopedTable&& rhs)
{
    m_conn = rhs.m_conn;
    rhs.m_conn = nullptr;
    m_name = std::move(rhs.m_name);
    return *this;
}

maxtest::ScopedTable::ScopedTable(maxtest::ScopedTable&& rhs)
    : m_name(std::move(rhs.m_name))
    , m_conn(rhs.m_conn)
{
    rhs.m_conn = nullptr;
}
