/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/ini.hh>
#include <string>

using std::move;
using mxb::ini::array_result::Configuration;
using mxb::ini::array_result::ConfigSection;

namespace
{
int compare_configs(const Configuration& found, const Configuration& expected);
}

int main(int argc, char* argv[])
{
    const std::string test_text =
        R"(
#qwerty
headerless_key1  =   headerless_value1
headerless_key2=   headerless_value2

[section1]
s1k1=s1v1
s1k2             =s1v2
[section two    ]
s2k1=s2v1

#asdf

[SeCt10n_three]
k1=part1
 part2
#zxcv
    part3
k1=part4

[section1]
k1=v1
k2 = v2
k1 =    v3
  v3continued=v3continued
k3=v4
)";
    auto res = mxb::ini::parse_config_text(test_text);

    int rval = 0;
    if (res.success)
    {
        Configuration expected;
        ConfigSection s_none;
        s_none.key_values.emplace_back("headerless_key1", "headerless_value1");
        s_none.key_values.emplace_back("headerless_key2", "headerless_value2");
        expected.push_back(move(s_none));

        ConfigSection s1;
        s1.header = "section1";
        s1.key_values.emplace_back("s1k1", "s1v1");
        s1.key_values.emplace_back("s1k2", "s1v2");
        expected.push_back(move(s1));

        ConfigSection s2;
        s2.header = "section two    ";
        s2.key_values.emplace_back("s2k1", "s2v1");
        expected.push_back(move(s2));

        ConfigSection s3;
        s3.header = "SeCt10n_three";
        s3.key_values.emplace_back("k1", "part1part2part3part4");
        expected.push_back(move(s3));

        ConfigSection s4;
        s4.header = "section1";
        s4.key_values.emplace_back("k1", "v1");
        s4.key_values.emplace_back("k2", "v2");
        s4.key_values.emplace_back("k1", "v3v3continued=v3continued");
        s4.key_values.emplace_back("k3", "v4");
        expected.push_back(move(s4));

        rval = compare_configs(res.sections, expected);
    }
    else if (res.err_lineno > 0)
    {
        printf("Example config parsing failed. Error at line %i.\n", res.err_lineno);
        rval++;
    }
    else
    {
        printf("Parser memory allocation failed.\n");
        rval++;
    }
    return rval;
}

namespace
{
int compare_configs(const Configuration& found, const Configuration& expected)
{
    int rval = 0;
    if (found.size() == expected.size())
    {
        for (size_t i = 0; i < expected.size(); i++)
        {
            const auto& sec_found = found[i];
            const auto& sec_expected = expected[i];
            if (sec_found.header != sec_expected.header)
            {
                printf("Headers differ. Found '%s' on line %i, expected '%s'.\n",
                       sec_found.header.c_str(), sec_found.lineno, sec_expected.header.c_str());
                rval++;
            }

            const auto& key_values_found = sec_found.key_values;
            const auto& key_values_expected = sec_expected.key_values;
            if (key_values_found.size() == key_values_expected.size())
            {
                for (size_t j = 0; j < key_values_expected.size(); j++)
                {
                    const auto& kv_found = key_values_found[j];
                    const auto& kv_expected = key_values_expected[j];
                    if (kv_found.name != kv_expected.name || kv_found.value != kv_expected.value)
                    {
                        printf("Key-value in section '%s' differs. "
                               "Found '%s' and '%s' on line %i, expected '%s' and '%s'.\n",
                               sec_found.header.c_str(),
                               kv_found.name.c_str(), kv_found.value.c_str(), kv_found.lineno,
                               kv_expected.name.c_str(), kv_expected.value.c_str());
                        rval++;
                    }
                }
            }
            else
            {
                printf("Found %zu key-values in section '%s' (starting at line %i), expected %zu.\n",
                       key_values_found.size(), sec_found.header.c_str(), sec_found.lineno,
                       key_values_expected.size());
                rval++;
            }
        }
    }
    else
    {
        printf("Found %zu sections, expected %zu.\n", found.size(), expected.size());
        rval++;
    }
    return rval;
}
}
