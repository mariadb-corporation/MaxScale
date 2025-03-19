#include <maxtest/testconnections.hh>

void execute_ps(TestConnections& test, Connection& c, const std::string& query)
{
    std::shared_ptr<MYSQL_STMT> stmt(c.stmt(), mysql_stmt_close);
    MXT_EXPECT(!!stmt);

    MXT_EXPECT_F(mysql_stmt_prepare(stmt.get(), query.c_str(), query.size()) == 0,
                 "Prepare failed: %s", mysql_stmt_error(stmt.get()));

    MXT_EXPECT_F(mysql_stmt_execute(stmt.get()) == 0,
                 "Execute failed: %s", mysql_stmt_error(stmt.get()));
}

void test_main(TestConnections& test)
{
    const auto databases = {"db1", "db2", "db3"};

    auto r1 = test.repl->get_connection(0);
    MXT_EXPECT(r1.connect());

    try
    {
        for (std::string db : databases)
        {
            MXT_EXPECT(r1.query("CREATE DATABASE " + db + "; "
                                + "CREATE TABLE " + db + ".t1(id INT); "
                                + "INSERT INTO " + db + ".t1 values (1)"));
        }

        auto c = test.maxscale->rwsplit();
        MXT_EXPECT(c.connect());

        for (std::string db : databases)
        {
            execute_ps(test, c, "SELECT * FROM " + db + ".t1");
            execute_ps(test, c, "SELECT * FROM " + db + ".t1 FOR UPDATE");
        }
    }
    catch (const std::runtime_error& err)
    {
    }

    for (std::string db : databases)
    {
        r1.query("DROP DATABASE " + db);
    }
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
