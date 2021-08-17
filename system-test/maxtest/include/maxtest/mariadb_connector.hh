/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxtest/ccdefs.hh>
#include <maxsql/mariadb_connector.hh>

namespace maxtest
{
class TestLogger;
class ScopedUser;

/**
 * Connection helper class for tests. Reports errors to the system test log.
 */
class MariaDB : public mxq::MariaDB
{
public:
    explicit MariaDB(TestLogger& log);

    bool open(const std::string& host, int port, const std::string& db = "");
    bool try_open(const std::string& host, int port, const std::string& db = "");
    enum class Expect
    {
        OK, FAIL, ANY
    };

    bool cmd(const std::string& sql, Expect expect = Expect::OK);
    bool try_cmd(const std::string& sql);
    bool cmd_f(const char* format, ...) mxb_attribute((format (printf, 2, 3)));
    bool try_cmd_f(const char* format, ...) mxb_attribute((format (printf, 2, 3)));

    std::unique_ptr<mxq::QueryResult> query(const std::string& query, Expect expect = Expect::OK);
    std::unique_ptr<mxq::QueryResult> try_query(const std::string& query);

    /**
     * Create a user that is automatically deleted when the object goes out of scope. Depends on the
     * generating connection object, so be careful when moving or manually destroying the user object.
     *
     * @param user Username
     * @param host Host
     * @param pw Password
     * @return User object
     */
    ScopedUser create_user(const std::string& user, const std::string& host,
                           const std::string& pw);

private:
    TestLogger& m_log;
};

/**
 * Helper class for managing user accounts in tests. When the object goes out of scope, the user is deleted
 * from backend. The object is dependent on the connection that created it. Should not be generated manually.
 */
class ScopedUser final
{
public:
    ScopedUser& operator=(ScopedUser&& rhs);

    ScopedUser() = default;
    ScopedUser(std::string user_host, maxtest::MariaDB* conn);
    ScopedUser(ScopedUser&& rhs);
    ~ScopedUser();

    void grant(const std::string& grant);
    void grant_f(const char* grant_fmt, ...);

private:
    std::string   m_user_host;      /**< user@host */
    mxt::MariaDB* m_conn {nullptr}; /**< Connection managing this user */
};
}
