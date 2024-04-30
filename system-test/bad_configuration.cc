/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Bad configuration test
 */

#include <maxtest/testconnections.hh>
#include <maxbase/format.hh>

using std::string;

namespace
{
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

void test_main(TestConnections& test)
{
    for (int i = 0; bad_configs[i]; i++)
    {
        test.tprintf("Testing %s.", bad_configs[i]);
        string config_file_path = mxb::string_printf("%s/bad_configurations/%s.cnf",
                                                     mxt::SOURCE_DIR, bad_configs[i]);
        test.test_config(config_file_path, false);
    }

    // Finally, test some good configurations to ensure test validity.
    string config_file_path = mxb::string_printf("%s/cnf/maxscale.cnf.template.minimal", mxt::SOURCE_DIR);
    test.test_config(config_file_path, true);
    config_file_path = mxb::string_printf("%s/cnf/maxscale.cnf.template.replication", mxt::SOURCE_DIR);
    test.test_config(config_file_path, true);
}
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    return TestConnections().run_test(argc, argv, test_main);
}
