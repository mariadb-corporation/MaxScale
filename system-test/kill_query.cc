/**
 * Test KILL QUERY functionality
 */

#include <maxtest/testconnections.hh>
#include <condition_variable>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    for (int i = 0; i < 10; i++)
    {
        auto a = test.maxscale->rwsplit();
        auto b = test.maxscale->rwsplit();
        test.expect(a.connect() && b.connect(), "Connections should work");
        std::mutex lock;
        std::condition_variable cv;

        auto id = a.thread_id();

        std::thread thr(
            [&]() {
            cv.notify_one();

            const char* query =
                "SET STATEMENT max_statement_time=31 FOR "
                "SELECT seq FROM seq_0_to_1000000000000000000";

            // The query takes 30 seconds to complete and the KILL is required to interrupt it before that.
            auto start = std::chrono::steady_clock::now();
            bool ok = a.query(query);
            auto end = std::chrono::steady_clock::now();

            if (ok)
            {
                test.expect(end - start < 30s, "Query should fail in less than 30 seconds");
            }
            else
            {
                const char* expected = "Query execution was interrupted";
                test.expect(strstr(a.error(), expected),
                            "Query should fail with '%s' but it failed with '%s'",
                            expected, a.error());
            }
        });

        // Wait for a few seconds to make sure the other thread has started executing
        // the query before killing it.
        std::unique_lock<std::mutex> guard(lock);
        cv.wait(guard);
        sleep(1);
        test.expect(b.query("KILL QUERY " + std::to_string(id)), "KILL QUERY failed: %s", b.error());

        test.reset_timeout();
        thr.join();
    }

    return test.global_result;
}
