/**
 * @file Simple dummy configuration program for non-C++ tests
 * - Configure Maxscale (prepare maxscale.cnf and copy it to Maxscale machine)
 * - check backends
 * - try to restore broken backends
 */


#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>

using namespace std;

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        return 1;
    }

    std::string sys =
        std::string(argv[2]) +
        std::string(" ") +
        std::string(argv[1]);

    TestConnections test(argc, argv);
    sleep(3);
    setenv("src_dir", test_dir, 1);

    test.add_result(system(sys.c_str()), "Test %s FAILED!", argv[1]);

    return test.global_result;
}
