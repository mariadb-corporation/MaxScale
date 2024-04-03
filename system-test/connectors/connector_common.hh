#pragma once

#include <maxtest/testconnections.hh>
#include <fstream>
#include <iostream>
#include <maxbase/string.hh>

static inline bool clone_repo(TestConnections& test, const std::string& repo,
                              const std::string& branch, const std::string& repo_dir)
{
    if (access(repo_dir.c_str(), F_OK) != 0 && errno == ENOENT)
    {
        test.run_shell_command(
            "git clone --depth=1 --branch=" + branch + " " + repo,
            "Cloning repository");
    }

    return test.ok();
}

static inline int run_maven_test(TestConnections& test_arg, int argc, char** argv,
                                 std::string repo, std::string branch, std::string repo_dir)
{
    auto maven_test_main = [&repo, &branch, &repo_dir](TestConnections& test){
        if (clone_repo(test, repo, branch, repo_dir))
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
                    "cd "s + repo_dir + " && TEST_MAXSCALE_TLS_PORT=4007 srv=maxscale mvn -Duser.timezone=UTC -B -q test",
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

    return test_arg.run_test(argc, argv, maven_test_main);
}

static inline int run_npm_test(TestConnections& test_arg, int argc, char** argv,
                               std::string repo, std::string branch, std::string repo_dir)
{
    auto npm_test_main = [&repo, &branch, &repo_dir](TestConnections& test){
        if (clone_repo(test, repo, branch, repo_dir))
        {
            std::ostringstream ss;
            ss << "cd " << repo_dir << " && npm i &&"
               << " TEST_DB_HOST=" << test.maxscale->ip()
               << " TEST_DB_PORT=4006"
               << " TEST_MAXSCALE_TLS_PORT=4007"
               << " TEST_DB_DATABASE=test"
               << " TEST_DB_USER=" << test.maxscale->user_name()
               << " TEST_DB_PASSWORD=" << test.maxscale->password()
               << " srv=maxscale"
               << " npm run test:base";

            test.run_shell_command(ss.str(), "Running test suite");
        }
    };

    int rc = system("command -v npm");

    if (!WIFEXITED(rc) || WEXITSTATUS(rc) != 0)
    {
        std::cout << "NPM is not installed, skipping test" << std::endl;
        return TestConnections::TEST_SKIPPED;
    }

    return test_arg.run_test(argc, argv, npm_test_main);
}
