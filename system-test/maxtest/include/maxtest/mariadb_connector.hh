/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxtest/ccdefs.hh>
#include <maxsql/mariadb_connector.hh>

class TestLogger;

namespace maxtest
{
/**
 * Connection helper class for tests. Reports errors to the system test log.
 */
class MariaDB : public mxq::MariaDB
{
public:
    explicit MariaDB(TestLogger& log);

    bool open(const std::string& host, int port, const std::string& db = "");

    bool cmd(const std::string& sql);
    bool cmd_f(const char* format, ...) mxb_attribute((format (printf, 2, 3)));

    std::unique_ptr<mxq::QueryResult> query(const std::string& query);

private:
    TestLogger& m_log;
};
}
