/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxbase/host.hh>
#include <unordered_set>

int main()
{
    using std::string;
    using std::unordered_set;

    struct Test
    {
        string host;
        string ip;
        string result;
    };

    Test tests[] = {
        {"localhost", "127.0.0.1", "127.0.0.1"},
    };
    /*
     * Here are some additional test case examples. They may not work
     * as is.
     *
     * Test tests[] = {
     *      {"yle.fi", "13.32.43.102", "13.32.43.102"},
     *      {"mariadb.com", "35.235.124.140", "35.235.124.140"},
     *      {"reddit.com", "151.101.1.140", "151.101.1.140"},
     *      {"max-tst-02.mariadb.com", "94.23.248.118", "94.23.248.118"},
     *      {"wikipedia.org", "2620:0:862:ed1a::1", "2620:0:862:ed1a::1"},
     *      {"one.one.one.one", "2606:4700:4700::1111", "2606:4700:4700::1111"},
     * };
     */

    bool ok = true;
    for (auto& item : tests)
    {
        string error;
        auto& host            = item.host;
        auto& ip              = item.ip;
        auto& expected_result = item.result;
        for (auto& subitem : {host, ip})
        {
            unordered_set<string> lookup_result;
            bool lookup_ok = mxb::name_lookup(subitem, &lookup_result, &error);
            if (!lookup_ok)
            {
                printf("Lookup of '%s' failed: %s\n", subitem.c_str(), error.c_str());
                ok = false;
            }
            else if (lookup_result.count(expected_result) == 0)
            {
                string results_conc;
                string sep;
                for (auto result_elem : lookup_result)
                {
                    results_conc += sep + result_elem;
                    sep = ", ";
                }
                printf("Lookup of '%s' gave incorrect results. Expected '%s', got '%s'.\n",
                    subitem.c_str(),
                    expected_result.c_str(),
                    results_conc.c_str());
                ok = false;
            }
        }
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
