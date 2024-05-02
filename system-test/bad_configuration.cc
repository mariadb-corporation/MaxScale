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
#include <dirent.h>

using std::string;

namespace
{
int cnf_filter(const dirent* entry)
{
    return strstr(entry->d_name, ".cnf") ? 1 : 0;
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
            test.test_config(config_file_path, false);
            free(namelist[i]);
        }
        free(namelist);
    }
    else
    {
        test.add_failure("scandir failed. Error '%s'.", mxb_strerror(errno));
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
