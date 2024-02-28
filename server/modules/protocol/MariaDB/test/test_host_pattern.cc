/*
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

#include <string>
#include "../user_data.hh"

using std::string;

namespace
{
struct AddrTest
{
    string client_addr;
    bool   match {false};
};
struct PatternTest
{
    string                host_pattern;
    std::vector<AddrTest> test_cases;
};

int test(UserDatabase& db, const PatternTest& pattern)
{
    const string uname = "test_user";

    db.clear();
    mariadb::UserEntry new_entry;
    new_entry.username = uname;
    new_entry.host_pattern = pattern.host_pattern;
    db.add_entry(std::move(new_entry));

    int rval = 0;

    for (const auto& test : pattern.test_cases)
    {
        bool matched = db.find_entry(uname, test.client_addr, {}).entry != nullptr;
        if (matched != test.match)
        {
            rval++;
            if (matched)
            {
                MXB_ERROR("Client address %s matched host pattern %s when it should not have.",
                          test.client_addr.c_str(), pattern.host_pattern.c_str());
            }
            else
            {
                MXB_ERROR("Client address %s did not match host pattern %s when it should have.",
                          test.client_addr.c_str(), pattern.host_pattern.c_str());
            }
        }
    }

    return rval;
}
}

int main(int argc, char** argv)
{
    mxb::Log log(MXB_LOG_TARGET_STDOUT);

    PatternTest tests[] =
    {
        {"0.0.0.0/0.0.0.0",
         {{"0.0.0.0", true},
          {"0.0.0.1", true}}},
        {"0.0.0.1/0.0.0.0",
         {{"0.0.0.1", false}}},
        {"127.0.0.0/255.255.255.0",
         {{"127.0.0.8", true},
          {"127.0.5.8", false},
          {"128.0.0.8"}}},
        {"1.2.12.254/3.18.12.255",
         {{"5.34.252.254", true}}},
        {"111.222.210.42/239.223.218.58",
         {{"111.222.210.42", true},
          {"127.254.214.170", true},
          {"239.254.214.170", false}}}
    };

    UserDatabase db;
    int result = 0;
    for (auto& pattern : tests)
    {
        result += test(db, pattern);
    }
    return result;
}
