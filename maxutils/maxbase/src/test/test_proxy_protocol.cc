/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/proxy_protocol.hh>
#include <arpa/inet.h>

#define BIN_SIG "\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A"

namespace
{
int test_networks_parse_and_match();
int test_header_preparse();
}
int main()
{
    int rval = test_networks_parse_and_match();
    rval += test_header_preparse();
    return rval;
}

namespace
{
int test_networks_parse_and_match()
{
    struct Ip
    {
        int         family {0};
        std::string ip;
        bool        matches {false};
    };

    struct Test
    {
        std::string     networks_setting;
        bool            parses {false};
        std::vector<Ip> ips;
    };

    std::vector<Test> tests = {{"*", true,
                                {{AF_INET, "127.0.0.1", true},
                                 {AF_UNIX, "", true},
                                 {AF_INET, "192.168.0.1", true},
                                 {AF_INET6, "2001:0db8:85a3:0000:0000:8a2e:0370:7334", true}},
                                },
                               {"127.0.0.1", true,
                                {{AF_INET, "127.0.0.1", true},
                                 {AF_INET, "192.168.0.1", false}}
                                },
                               {"qwertyasdf", false, {}},
                               {"1.2.3.4.5.6.7", false, {}},
                               {"localhost", true,
                                {{AF_UNIX, "", true},
                                 {AF_INET, "192.168.0.5", false}}
                               },
                               {"192.168.0.1/17", true,
                                {{AF_INET, "192.168.127.4", true},
                                 {AF_INET, "192.168.128.4", false}}
                               },
                               {"127.0.0.1 ,192.168.0.1", true,
                                {{AF_INET, "127.0.0.1", true},
                                 {AF_INET, "192.168.0.1", true},
                                 {AF_INET, "192.168.0.2", false}}
                               }
    };

    int fails = 0;
    for (const auto& test : tests)
    {
        auto parse_res = mxb::proxy_protocol::parse_networks_from_string(test.networks_setting);
        bool parsed = parse_res.errmsg.empty();
        if (parsed == test.parses)
        {
            for (const auto& test_ip : test.ips)
            {
                sockaddr_storage sa;
                sa.ss_family = test_ip.family;
                if (test_ip.family == AF_INET)
                {
                    auto* dst = (sockaddr_in*)(&sa);
                    auto* dst_addr = &dst->sin_addr;
                    if (inet_pton(AF_INET, test_ip.ip.c_str(), dst_addr) != 1)
                    {
                        printf("inet_pton() failed for '%s'.\n", test_ip.ip.c_str());
                        fails++;
                        break;
                    }
                }
                else if (test_ip.family == AF_INET6)
                {
                    auto* dst = (sockaddr_in6*)(&sa);
                    auto* dst_addr = &dst->sin6_addr;
                    if (inet_pton(AF_INET6, test_ip.ip.c_str(), dst_addr) != 1)
                    {
                        printf("inet_pton() failed for '%s'.\n", test_ip.ip.c_str());
                        fails++;
                        break;
                    }
                }

                bool proxy_allowed = mxb::proxy_protocol::is_proxy_protocol_allowed(sa, parse_res.subnets);
                if (test_ip.matches != proxy_allowed)
                {
                    if (proxy_allowed)
                    {
                        printf("Test address '%s' matched networks '%s' when it should not have.\n",
                               test_ip.ip.c_str(), test.networks_setting.c_str());
                        fails++;
                    }
                    else
                    {
                        printf("Test address '%s' did not match networks '%s' when it should have.\n",
                               test_ip.ip.c_str(), test.networks_setting.c_str());
                        fails++;
                    }
                }
            }
        }
        else if (parsed)
        {
            printf("Parsing of '%s' succeeded when failure was expected.\n", test.networks_setting.c_str());
            fails++;
        }
        else
        {
            printf("Parsing of '%s' failed when success was expected. Error: %s\n",
                   test.networks_setting.c_str(), parse_res.errmsg.c_str());
            fails++;
        }
    }
    return fails;
}

int test_header_preparse()
{
    using Type = mxb::proxy_protocol::PreParseResult::Type;
    struct Test
    {
        uint8_t header_data[256];
        int     datalen {0};
        Type    result_type {Type::ERROR};
        int     length {-1};
    };

    std::vector<Test> tests =
        {
            Test {"ABC", 3, Type::NEED_MORE,                -1},
            Test {"ABCDE", 5, Type::ERROR,                  -1},
            Test {"PROXY UNKNOWN", 13, Type::NEED_MORE,     -1},
            Test {"PROXY UNKNOWN\r\n BLAH", 20, Type::TEXT, 15},
            Test {BIN_SIG "\x11\x22\x00\x02\x01\x01", 18, Type::BINARY, 18},
            Test {BIN_SIG "\xFF\xFF\x00\x03", 16, Type::NEED_MORE, 12 + 4 + 3},
            Test {BIN_SIG "\xFF\xFF\x00\x03\0x00\0x00\0x00\0x01", 20, Type::BINARY, 12 + 4 + 3},
            Test {"\x0D\x0A\x0D\x0A\x00", 5, Type::NEED_MORE, -1}
        };

    int fails = 0;
    for (const auto& test : tests)
    {
        auto parse_res = mxb::proxy_protocol::pre_parse_header(test.header_data, test.datalen);
        if (parse_res.type == test.result_type)
        {
            if (parse_res.len != test.length)
            {
                fails++;
                printf("Wrong pre-parse length result for '%s'. Got %i, expected %i.\n",
                       test.header_data, parse_res.len, test.length);
            }
        }
        else
        {
            fails++;
            printf("Wrong pre-parse result for '%s'. Got %i, expected %i.\n",
                   test.header_data, parse_res.type, test.result_type);
        }
    }
    return fails;
}
}
