/**
 * MXS-1831: No error on invalid monitor parameter alteration
 *
 * https://jira.mariadb.org/browse/MXS-1831
 */

#include "testconnections.h"

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    int rc = test.maxscales->ssh_node_f(0,
                                        true,
                                        "maxctrl alter monitor MySQL-Monitor not_a_parameter not_a_value|grep Error");
    test.assert(rc == 0, "Altering unknown parameter should cause an error");
    rc = test.maxscales->ssh_node_f(0,
                                    true,
                                    "maxctrl alter monitor MySQL-Monitor ignore_external_masters on_sunday_afternoons|grep Error");
    test.assert(rc == 0, "Invalid parameter value should cause an error");

    return test.global_result;
}
