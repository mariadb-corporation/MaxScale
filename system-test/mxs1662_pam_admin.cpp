/**
 * MXS-1662: PAM authenticator for admin users
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    // TODO: Store login information
    test.check_maxctrl("-u admin -p mariadb show maxscale");

    return test.global_result;
}
