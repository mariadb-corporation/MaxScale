/**
 * @file Simple dummy configuration program for non-C++ tests
 * - Configure Maxscale (prepare maxscale.cnf and copy it to Maxscale machine)
 * - check backends
 * - try to restore broken backends
 */

#include <iostream>
#include <stdlib.h>
#include <maxbase/format.hh>
#include <maxtest/testconnections.hh>

using namespace std;

namespace
{

void print_usage(const char* zProgram)
{
    cout << "usage: " << zProgram << "[<flags>] <name> <script>\n"
         << "\n"
         << "where\n"
         << "  <flags>   are flags that will be provided to the TestConnections contructor,\n"
         << "  <name>    is the name of the test, and\n"
         << "  <script>  is the program that will be invoked." << endl;
}

}

int main(int argc, char* argv[])
{
    const char* zScript = nullptr;
    const char* zName = nullptr;

    for (int i = 1; i < argc; ++i)
    {
        if (*argv[i] != '-')
        {
            zName = argv[i];

            if (i + 1 < argc)
            {
                zScript = argv[i + 1];
            }

            break;
        }
    }

    int rv = 1;

    if (zName && zScript)
    {
        TestConnections test(argc, argv);
        test.write_node_env_vars();
        auto test_dir = mxt::SOURCE_DIR;

        sleep(3);
        setenv("src_dir", test_dir, 1);

        string script_cmd = mxb::string_printf("%s/%s %s", test_dir, zScript, zName);
        int script_res = system(script_cmd.c_str());

        test.expect(script_res == 0, "Test %s FAILED!", argv[1]);

        rv = test.global_result;
    }
    else
    {
        print_usage(argv[0]);
    }

    return rv;
}
