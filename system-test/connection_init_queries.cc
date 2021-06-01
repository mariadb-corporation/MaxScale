#include <iostream>
#include <string>
#include <maxbase/format.hh>
#include <maxtest/testconnections.hh>

using std::string;
using std::cout;

int main(int argc, char** argv)
{
    // Before starting MaxScale, need to write the connection initialization file on the MaxScale machine.
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);

    const string init_var1 = "@var1";
    const string extected_res1 = "result_one";

    const string init_var2 = "@var2";
    const string extected_res2 = "result_two";

    const string init_var3 = "@var3";
    const string extected_res3 = "result_three";

    string file_contents;
    auto add_line = [&file_contents](const string& var_name, const string& var_value) {
            file_contents += mxb::string_printf("SET %s='%s';\n", var_name.c_str(), var_value.c_str());
        };
    add_line(init_var1, extected_res1);
    add_line(init_var2, extected_res2);
    add_line(init_var3, extected_res3);

    string filepath = "/tmp/init_file.txt";
    string create_file_cmd = "printf \"" + file_contents + "\" > " + filepath;
    string delete_file_cmd = "rm -f " + filepath;

    test.maxscales->ssh_node_f(0, true, "%s", create_file_cmd.c_str());
    test.maxscales->start_and_check_started();
    test.maxscales->wait_for_monitor();
    auto conn = test.maxscales->open_rwsplit_connection();

    auto check_variable_value = [conn](const string& var_name, const string& expected_value) {
            string query = "select " + var_name + ";";
            bool rval = false;
            char read_value[100];
            if (find_field(conn, query.c_str(), var_name.c_str(), read_value) == 0)
            {
                if (read_value == expected_value)
                {
                    rval = true;
                }
                else
                {
                    string msg = mxb::string_printf("Value of %s is wrong. Expected '%s', got '%s'.\n",
                                                    var_name.c_str(), expected_value.c_str(), read_value);
                    cout << msg;
                }
            }
            else
            {
                cout << "Could not read value of " << var_name << ".\n";
            }
            return rval;
        };

    const char msg[] = "Init variable set/get failed.";
    test.expect(check_variable_value(init_var1, extected_res1), msg);
    test.expect(check_variable_value(init_var2, extected_res2), msg);
    test.expect(check_variable_value(init_var3, extected_res3), msg);

    test.maxscales->ssh_node_f(0, true, "%s", delete_file_cmd.c_str());
    test.log_includes("Super user '.*' logged in to service");
    return test.global_result;
}
