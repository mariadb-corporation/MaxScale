/**
 * Bad configuration test
 */


#include <iostream>
#include <unistd.h>
#include <maxtest/testconnections.hh>

using std::string;

const char* bad_configs[] =
{
    "bug359",
    "bug495",
    "bug526",
    "bug479",
    "bug493",
    "bad_ssl",
    "mxs710_bad_socket",
    "mxs711_two_ports",
    "mxs720_line_with_no_equal",
    "mxs720_wierd_line",
    "mxs799",
    "mxs1731_empty_param",
// passwd is still supported
//    "old_passwd",
    "no_use_of_reserved_names",
    "no_spaces_in_section_names",
    "no_unknown_auth_options",
    "mxs4211_unknown_nested_parameter",
    NULL
};

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    TestConnections test(argc, argv);
    int rval = 0;

    for (int i = 0; bad_configs[i]; i++)
    {
        string config_file_path = string(mxt::SOURCE_DIR) + "/cnf/maxscale.cnf.template." + bad_configs[i];
        printf("Testing %s...\n", config_file_path.c_str());
        test.expect(!test.test_bad_config(config_file_path), "Bad config not detected");
    }

    return test.global_result;
}
