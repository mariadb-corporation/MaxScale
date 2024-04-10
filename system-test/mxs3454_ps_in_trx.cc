/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Failed to connect: %s", conn.error());
    test.expect(conn.query("CREATE TABLE test.t1 (id INT)"), "Failed to create table: %s", conn.error());
    test.expect(conn.query("START TRANSACTION"), "Failed to start transaction: %s", conn.error());

    MYSQL_STMT* stmt = conn.stmt();
    uint64_t buffer;
    char err;
    char isnull = false;
    MYSQL_BIND param;
    param.buffer = &buffer;
    param.buffer_type = MYSQL_TYPE_LONG;
    param.is_null = &isnull;
    param.is_unsigned = false;
    param.error = &err;

    std::string query = "DELETE FROM test.t1 WHERE id = ?";

    test.expect(mysql_stmt_prepare(stmt, query.c_str(), query.length()) == 0,
                "Failed to prepare: %s", mysql_stmt_error(stmt));

    test.expect(mysql_stmt_bind_param(stmt, &param) == 0,
                "Failed to bind: %s", mysql_stmt_error(stmt));

    test.expect(mysql_stmt_execute(stmt) == 0,
                "Failed to execute: %s", mysql_stmt_error(stmt));

    mysql_stmt_close(stmt);

    test.expect(conn.query("COMMIT"), "Failed to commit transaction: %s", conn.error());
    test.expect(conn.query("DROP TABLE test.t1"), "Failed to drop table: %s", conn.error());

    return test.global_result;
}
