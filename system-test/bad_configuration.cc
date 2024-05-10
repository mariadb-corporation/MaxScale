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
#include <maxbase/string.hh>
#include <dirent.h>

using std::string;

namespace
{
int cnf_filter(const dirent* entry)
{
    const char* filename = entry->d_name;
    auto fn_len = strlen(filename);
    const char suffix[] = ".cnf";
    const auto suffix_len = sizeof(suffix) - 1;
    if (fn_len > suffix_len)
    {
        return memcmp(filename + fn_len - suffix_len, suffix, suffix_len) == 0;
    }
    return false;
}

void test_main(TestConnections& test)
{
    dirent** namelist;
    string bad_configs_path = mxb::string_printf("%s/bad_configurations", mxt::SOURCE_DIR);

    int n = scandir(bad_configs_path.c_str(), &namelist, cnf_filter, alphasort);
    if (n >= 0)
    {
        test.expect(n > 10, "Too few files, found just %i.", n);

        for (int i = 0; i < n; i++)
        {
            const char* fname = namelist[i]->d_name;
            test.tprintf("Testing %s.", fname);
            string config_file_path = mxb::string_printf("%s/%s", bad_configs_path.c_str(), fname);
            test.test_config(config_file_path, 1);
            free(namelist[i]);
        }
        free(namelist);
    }
    else
    {
        test.add_failure("scandir failed. Error '%s'.", mxb_strerror(errno));
    }

    // Test some good configurations to ensure test validity.
    string config_file_path = mxb::string_printf("%s/cnf/maxscale.cnf.template.minimal", mxt::SOURCE_DIR);
    test.test_config(config_file_path, 0);
    config_file_path = mxb::string_printf("%s/cnf/maxscale.cnf.template.replication", mxt::SOURCE_DIR);
    test.test_config(config_file_path, 0);

    // Test a configuration that fails due to service not starting up. First check that the listener port
    // is already taken so that the test is valid.
    const int ssh_port = 22;
    test.tprintf("Checking that port %i is taken.", ssh_port);
    std::string cmd = mxb::string_printf("netstat -ln -A inet | grep -E ^tcp.*:%i", ssh_port);
    auto res = test.maxscale->vm_node().run_cmd_output_sudo(cmd);
    if (res.rc == 0)
    {
        test.tprintf("Command '%s' returned:\n%s", cmd.c_str(), res.output.c_str());
        if (res.output.empty())
        {
            test.add_failure("Port %i may not be in use, cannot continue test.", ssh_port);
        }
        else
        {
            config_file_path = mxb::string_printf("%s/listener_port_in_use.cnf_ret3",
                                                  bad_configs_path.c_str());
            test.test_config(config_file_path, 3);
        }
    }
    else
    {
        test.add_failure("lsof failed. Error %i: %s", res.rc, res.output.c_str());
    }
}
}

int main(int argc, char** argv)
{
    TestConnections::skip_maxscale_start(true);
    return TestConnections().run_test(argc, argv, test_main);
}
