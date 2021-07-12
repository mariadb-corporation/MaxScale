#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    return test.run_test_script("masking_user.sh", "masking_user");
}
