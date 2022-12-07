#pragma once

#include <maxtest/testconnections.hh>
#include <fstream>

static inline int run_maven_test(TestConnections& test, int argc, char** argv,
                                 std::string repo, std::string branch, std::string repo_dir)
{
    auto maven_test_main = [&repo, &branch, &repo_dir](TestConnections& test){
        if (access(repo_dir.c_str(), F_OK) != 0 && errno == ENOENT)
        {
            test.run_shell_command(
                "git clone --depth=1 --branch=" + branch + " " + repo,
                "Cloning repository");
        }

        if (test.ok())
        {
            std::ofstream of("./" + repo_dir + "/src/test/resources/conf.properties");
            of << "DB_HOST=" << test.maxscale->ip() << "\n"
               << "DB_PORT=4006\n"
               << "DB_DATABASE=test\n"
               << "DB_USER=" << test.maxscale->user_name() << "\n"
               << "DB_PASSWORD=" << test.maxscale->password() << "\n"
               << "DB_OTHER=\n";
            of.close();

            test.expect(!!of, "Failed to write test configuration file: %d, %s",
                        errno, mxb_strerror(errno));

            if (test.ok())
            {
                test.run_shell_command(
                    "cd "s + repo_dir + " && srv=maxscale mvn -Duser.timezone=UTC -B -q test",
                    "Running test suite");
            }
        }
    };

    int rc = system("command -v mvn");

    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
    {
        std::cout << "Maven is not installed, skipping test" << std::endl;
        return TestConnections::TEST_SKIPPED;
    }

    return test.run_test(argc, argv, maven_test_main);
}
