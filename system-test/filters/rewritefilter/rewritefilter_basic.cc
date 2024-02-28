/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */


/** This is a very simple test, just making sure that traffic
 *  goes through it and it makes a simple rewrite. Unit
 *  tests do more specific template testing.
 */

#include <memory>
#include <sstream>
#include <iostream>

#include <maxtest/testconnections.hh>
#include <maxbase/exception.hh>
#include <maxbase/string.hh>

DEFINE_EXCEPTION(RewriteError);
DEFINE_EXCEPTION(DatabaseError);

const int ROW_ID = 42;

class CreateTable
{
public:
    CreateTable(CreateTable&&) = delete;

    CreateTable(MYSQL* conn)
        : m_conn(conn)
    {
        if (execute_query_silent(conn, "drop table if exists test.rewrite;"
                                       "create table test.rewrite(id int, str_id varchar(10),"
                                       "primary key(id))") != 0)
        {
            MXB_THROW(DatabaseError, "Create table failed - could not start test");
        }
    }

    ~CreateTable()
    {
        mysql_query(m_conn, "drop table test.rewrite");
    }
private:
    MYSQL* m_conn;
};

void insert_rows(MYSQL* conn)
{
    std::string insert = MAKE_STR("insert into test.rewrite values(" << ROW_ID << ", '" << ROW_ID << "')");

    if (execute_query_silent(conn, insert.c_str()) != 0)
    {
        MXB_THROW(DatabaseError, "Insert failed - could not start test");
    }
}

void test_rewrites(MYSQL* conn)
{
    /** This query should be rewritten to:
     *  select id, str_id from test.rewrite where id=ROW_ID # ==42
     */
    auto sql = MAKE_STR("select id from test.rewrite where id=" << ROW_ID);

    if (mysql_query(conn, sql.c_str()) != 0)
    {
        MXB_THROWCode(DatabaseError, mysql_errno(conn), "Query failed: " << sql);
    }

    std::unique_ptr<MYSQL_RES, void (*)(MYSQL_RES*)> result(mysql_store_result(conn),
                                                            mysql_free_result);
    if (!result)
    {
        MXB_THROW(RewriteError, "No resultset for " << sql);
    }

    MYSQL_ROW row = mysql_fetch_row(&*result);
    if (row)
    {
        if (row[0] != std::to_string(ROW_ID))
        {
            MXB_THROW(RewriteError, "Expected row[0] == " << ROW_ID);
        }
        else if (row[1] != std::to_string(ROW_ID))
        {
            MXB_THROW(RewriteError, "Expected row[1] == " << ROW_ID);
        }
    }
    else
    {
        MXB_THROW(RewriteError, "Row id = " << ROW_ID << " not in resultset.");
    }


    if ((row = mysql_fetch_row(&*result)))
    {
        MXB_THROW(RewriteError, "Extra row index = " << ROW_ID << " id = " << row[0] << " in resultset.");
    }
}

int main(int argc, char* argv[])
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test {argc, argv};

    /* Copy the rewrite template file to the maxscale node */
    auto rf_file = "rewrite.rf"s;
    std::string from = mxt::SOURCE_DIR + "/filters/rewritefilter/"s + rf_file;
    std::string to = test.maxscale->access_homedir() + rf_file;
    test.maxscale->copy_to_node(from.c_str(), to.c_str());
    test.maxscale->ssh_node(("chmod a+r "s + to).c_str(), true);

    test.repl->connect();
    test.maxscale->start();
    test.maxscale->connect_rwsplit("test");

    try
    {
        std::cout << "Create table" << std::endl;
        CreateTable create{test.maxscale->conn_rwsplit};

        std::cout << "Insert rows" << std::endl;
        test.reset_timeout();
        insert_rows(test.maxscale->conn_rwsplit);

        std::cout << "Test rewrites" << std::endl;
        test_rewrites(test.maxscale->conn_rwsplit);
    }
    catch (DatabaseError& ex)
    {
        test.add_result(1, "%s", ex.what());
    }

    catch (std::exception& ex)
    {
        test.add_result(1, "%s", ex.what());
    }
    catch (...)
    {
        test.add_result(1, "catch(...)");
        throw;
    }


    return test.global_result;
}
