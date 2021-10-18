/**
 * MXS-1831: No error on invalid monitor parameter alteration
 *
 * https://jira.mariadb.org/browse/MXS-1831
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    int rc = test.maxscale->ssh_node_f(true,
                                       "maxctrl alter monitor MySQL-Monitor not_a_parameter not_a_value|grep Error");
    test.expect(rc == 0, "Altering unknown parameter should cause an error");
    rc = test.maxscale->ssh_node_f(true,
                                   "maxctrl alter monitor MySQL-Monitor auto_rejoin on_sunday_afternoons|grep Error");
    test.expect(rc == 0, "Invalid parameter value should cause an error");

    return test.global_result;
}
