/**
 * @file Simple dummy configuration program for non-C++ tests
 * - Configure Maxscale (prepare maxscale.cnf and copy it to Maxscale machine)
 * - check backends
 * - try to restore broken backends
 */


#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using std::string;

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        return 1;
    }

    TestConnections test(argc, argv);
    test.write_node_env_vars();

    sleep(3);
    setenv("src_dir", test_dir, 1);
    const char* test_name = argv[1];
    const char* test_script = argv[2];
    string script_cmd = mxb::string_printf("%s %s", test_script, test_name);
    int script_res = system(script_cmd.c_str());
    test.expect(script_res == 0, "Test %s FAILED!", test_name);
    return test.global_result;
}
