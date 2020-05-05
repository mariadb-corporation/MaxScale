#include <testconnections.h>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.check_maxscale_alive();

    return test.global_result;
}
