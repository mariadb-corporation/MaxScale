/**
 * Runs the MariaDB Connector/R2DBC test suite against MaxScale
 */
#include <maxtest/testconnections.hh>
#include <fstream>

void test_main(TestConnections& test)
{
    if (access("mariadb-connector-r2dbc", F_OK) != 0 && errno == ENOENT)
    {
        test.run_shell_command(
            "git clone --depth=1 --branch=develop https://github.com/mariadb-corporation/mariadb-connector-r2dbc",
            "Cloning R2DBC repository");
    }

    if (test.ok())
    {
        std::ofstream of("./mariadb-connector-r2dbc/src/test/resources/conf.properties");
        of << "DB_HOST=" << test.maxscale->ip() << "\n"
           << "DB_PORT=4006\n"
           << "DB_DATABASE=test\n"
           << "DB_USER=" << test.maxscale->user_name() << "\n"
           << "DB_PASSWORD=" << test.maxscale->password() << "\n"
           << "DB_OTHER=\n";
        of.close();

        test.expect(!!of, "Failed to write R2DBC test configuration file: %d, %s",
                    errno, mxb_strerror(errno));

        if (test.ok())
        {
            // Test test appears to take close to 300 seconds to complete. Increase the timeout to make sure
            // it has enough time to complete but not too much to make sure it returns within a reasonable
            // time if it hangs.
            test.reset_timeout(500);
            test.run_shell_command("cd mariadb-connector-r2dbc && srv=maxscale mvn -B test",
                                   "Running Connector/R2DBC test suite");
        }
    }
}

int main(int argc, char** argv)
{
    int rc = system("command -v mvn");

    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
    {
        std::cout << "Maven is not installed, skipping test" << std::endl;
        return TestConnections::TEST_SKIPPED;
    }

    return TestConnections().run_test(argc, argv, test_main);
}
