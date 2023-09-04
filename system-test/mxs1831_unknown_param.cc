/**
 * MXS-1831: No error on invalid monitor parameter alteration
 *
 * https://jira.mariadb.org/browse/MXS-1831
 */

#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto rv = test.maxctrl("alter monitor MySQL-Monitor not_a_parameter not_a_value");
    test.expect(rv.rc != 0, "Altering unknown parameter should cause an error: %s", rv.output.c_str());
    rv = test.maxctrl("alter monitor MySQL-Monitor auto_rejoin on_sunday_afternoons");
    test.expect(rv.rc != 0, "Invalid parameter value should cause an error: %s", rv.output.c_str());

    return test.global_result;
}
