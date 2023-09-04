/**
 * Double-close on bad session command result
 */

#include <maxtest/testconnections.hh>

void run_test(TestConnections& test)
{
    Connection conn = test.maxscale->rwsplit();
    conn.connect();

    for (int i = 0; i <= 300 && test.global_result == 0; i++)
    {
        if (conn.query("SET @a = 1")
            && conn.query("USE test")
            && conn.query("SET SQL_MODE=''")
            && conn.query("USE test")
            && conn.query("SELECT @@last_insert_id")
            && conn.query("SELECT 1")
            && conn.query("USE test")
            && conn.query("SELECT 1")
            && conn.query("SET @a = 123")
            && conn.query("BEGIN")
            && conn.query("SELECT @a")
            && conn.query("COMMIT")
            && conn.query("SET @a = 321")
            && conn.query("SELECT @a")
            && conn.query("SET @a = 456")
            && conn.query("START TRANSACTION READ ONLY")
            && conn.query("SELECT @a")
            && conn.query("COMMIT")
            && conn.query("PREPARE ps FROM 'SELECT 1'")
            && conn.query("EXECUTE ps")
            && conn.query("DEALLOCATE PREPARE ps"))
        {
            conn.reset_connection();
        }
        else
        {
            break;
        }
    }
}

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    std::vector<std::thread> threads;

    for (int i = 0; i < 5; i++)
    {
        threads.emplace_back(run_test, std::ref(test));
    }

    for (int i = 0; i < 5; i++)
    {
        test.repl->stop_node(1 + i % 3);
        test.repl->start_node(1 + i % 3);
        sleep(1);
    }

    for (auto& a : threads)
    {
        a.join();
    }

    return test.global_result;
}
