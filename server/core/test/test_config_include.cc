/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include <maxbase/log.hh>
#include <maxbase/string.hh>
#include <maxscale/maxscale_test.h>
#include "../internal/config.hh"

using namespace std;

struct TestCase
{
    const char*        zConfig;
    bool               should_succeed;
    map<string,string> result;
} test_cases[] =
{
    // Vanilla case.
    {
        R"(
[Included]
type=include
user=admin
password=mariadb

[Result]
type=monitor
module=mariadbmon
@include=Included
)",
        true,
        {
            { "type", "monitor" },
            { "module", "mariadbmon" },
            { "user", "admin" },
            { "password", "mariadb" }
        }
    },

    // An include section must not be able to include another include section.
    {
        R"(
[Included]
type=include
user=admin
password=mariadb

[Result]
type=include
@include=Base
)",
        false
    },

    // It must only be possible to include an include section.
    {
        R"(
[Included]
type=monitor
module=mariadbmon
user=admin
password=mariadb

[Result]
type=monitor
@include=Included
)",
        false
    }
};

const int nTest_cases = sizeof(test_cases)/sizeof(test_cases[0]);


int test(const TestCase& tc)
{
    int rv = 0;

    SniffResult sniff = sniff_configuration_text(tc.zConfig);

    if (sniff.success)
    {
        ConfigSectionMap config;

        bool loaded = config_load("test_config_include", sniff.config, config);

        if (loaded && !tc.should_succeed)
        {
            cerr << "error: Config loaded, even though it should have failed.\n"
                 << tc.zConfig << endl;
            rv = 1;
        }
        else if (!loaded && tc.should_succeed)
        {
            cerr << "error: Config not loaded, even though it should have succeeded.\n"
                 << tc.zConfig << endl;
            rv = 1;
        }
        else if (loaded && tc.should_succeed)
        {
            auto it = config.find("Result");
            mxb_assert(it != config.end());

            const ConfigSection& section = it->second;
            const mxs::ConfigParameters& parameters = section.m_parameters;

            for (const auto& kv : tc.result)
            {
                if (parameters.contains(kv.first))
                {
                    auto value = parameters.get_string(kv.first);

                    if (value != kv.second)
                    {
                        cerr << "error: Key '" << kv.first << "' found, but value was '"
                             << value << "' and not '" << kv.second << "'."
                             << endl;
                        rv = 1;
                    }
                }
                else
                {
                    cerr << "error: Expected key '" << kv.first << "' to be found, but it was not."
                         << endl;
                    rv = 1;
                }
            }
        }
    }
    else
    {
        cerr << "error: Sniffing failed." << endl;
        rv = 1;
    }

    return rv;
}

int main(int argc, char* argv[])
{
    int rv = 0;

    mxb::Log log;

    mxs::set_libdir(TEST_DIR "/server/modules/monitor/mariadbmon");

    mxs::Config::init(argc, argv);

    for (size_t i = 0; i < nTest_cases; ++i)
    {
        rv += test(test_cases[i]);
    }

    return rv;
};
