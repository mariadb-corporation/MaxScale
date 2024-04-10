/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/proxy_protocol.hh>
#include <arpa/inet.h>
#include <sys/un.h>

#define BIN_SIG "\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A"

using std::string;

namespace
{
int test_networks_parse_and_match();
int test_header_preparse();
int test_gen_vs_parse();
int check_parse_res(const mxb::proxy_protocol::HdrParseResult& parsed, const sockaddr_storage& orig,
                    const string& orig_str);

const char PTON_FAIL[] = "inet_pton() failed for '%s'.\n";
}
int main()
{
    int rval = test_networks_parse_and_match();
    rval += test_header_preparse();
    rval += test_gen_vs_parse();
    return rval;
}

namespace
{
int test_networks_parse_and_match()
{
    struct Ip
    {
        int    family {0};
        string ip;
        bool   matches {false};
    };

    struct Test
    {
        string          networks_setting;
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
            Test {"ABC", 3, Type::INCOMPLETE,               -1},
            Test {"ABCDE", 5, Type::ERROR,                  -1},
            Test {"PROXY UNKNOWN", 13, Type::INCOMPLETE,    -1},
            Test {"PROXY UNKNOWN\r\n BLAH", 20, Type::TEXT, 15},
            Test {BIN_SIG "\x11\x22\x00\x02\x01\x01", 18, Type::BINARY, 18},
            Test {BIN_SIG "\xFF\xFF\x00\x03", 16, Type::INCOMPLETE, 12 + 4 + 3},
            Test {BIN_SIG "\xFF\xFF\x00\x03\0x00\0x00\0x00\0x01", 20, Type::BINARY, 12 + 4 + 3},
            Test {"\x0D\x0A\x0D\x0A\x00", 5, Type::INCOMPLETE, -1}
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

int test_gen_vs_parse()
{
    struct Test
    {
        int     client_family;
        string  client_address;
        int     client_port;

        int     server_family;
        string  server_address;
        int     server_port;
    };
    std::vector<Test> tests = {
        {AF_INET, "127.0.0.1", 1234, AF_INET, "192.168.0.1", 4321},
        {AF_INET, "111.0.2.3", 1111, AF_INET6, "2001:0db8:85a3:0000:0000:8a2e:0370:7334", 22},
        {AF_INET6, "2001:0db8:85a3:0000:0000:8a2e:0370:7334", 332, AF_UNIX, "abc", 0},
        {AF_UNIX, "some_socket", 0, AF_INET, "192.168.0.1", 4321}
    };

    int fails = 0;
    auto addr_helper = [&fails](int family, const string& addr_str, int port, sockaddr_storage& out) {
        if (family == AF_INET)
        {
            auto* dst = (sockaddr_in*)&out;
            auto* dst_addr = &dst->sin_addr;
            if (inet_pton(family, addr_str.c_str(), dst_addr) == 1)
            {
                dst->sin_family = family;
                dst->sin_port = htons(port);
            }
            else
            {
                printf(PTON_FAIL, addr_str.c_str());
                fails++;
            }
        }
        else if (family == AF_INET6)
        {
            auto* dst = (sockaddr_in6*)&out;
            auto* dst_addr = &dst->sin6_addr;
            if (inet_pton(family, addr_str.c_str(), dst_addr) == 1)
            {
                dst->sin6_family = family;
                dst->sin6_port = htons(port);
            }
            else
            {
                printf(PTON_FAIL, addr_str.c_str());
                fails++;
            }
        }
        else if (family == AF_UNIX)
        {
            auto* dst = (sockaddr_un*)&out;
            strcpy(dst->sun_path, addr_str.c_str());
            dst->sun_family = family;
        }
    };

    for (const auto& test : tests)
    {
        sockaddr_storage client {};
        sockaddr_storage server {};
        addr_helper(test.client_family, test.client_address, test.client_port, client);
        addr_helper(test.server_family, test.server_address, test.server_port, server);

        if (client.ss_family && server.ss_family)
        {
            // Text header does not support unix socket clients.
            if (client.ss_family != AF_UNIX)
            {
                auto header = mxb::proxy_protocol::gen_text_header(client, server);
                if (header.len > 0)
                {
                    auto parsed = mxb::proxy_protocol::parse_text_header(header.header, header.len);
                    if (parsed.success && parsed.is_proxy)
                    {
                        fails += check_parse_res(parsed, client, test.client_address);
                    }
                    else
                    {
                        printf("Parsing of text header '%s' failed.", header.header);
                        fails++;
                    }
                }
                else
                {
                    printf("Header generation from '%s' and '%s' failed: %s", test.client_address.c_str(),
                           test.server_address.c_str(), header.errmsg.c_str());
                    fails++;
                }
            }

            // Binary header generation and parsing should work with all address family combinations.
            auto binheader = mxb::proxy_protocol::gen_binary_header(client, server);
            auto parsed = mxb::proxy_protocol::parse_binary_header(binheader.header);
            if (parsed.success && parsed.is_proxy)
            {
                fails += check_parse_res(parsed, client, test.client_address);
            }
            else
            {
                printf("Parsing of binary header '%s' failed.", binheader.header);
                fails++;
            }
        }
        else
        {
            if (!client.ss_family)
            {
                printf("Failed to convert client address '%s'.\n", test.client_address.c_str());
                fails++;
            }
            if (!server.ss_family)
            {
                printf("Failed to convert server address '%s'.\n", test.server_address.c_str());
                fails++;
            }
        }
    }
    return fails;
}

int check_parse_res(const mxb::proxy_protocol::HdrParseResult& parsed, const sockaddr_storage& orig,
                    const string& orig_str)
{
    int rval = 0;
    if (parsed.peer_addr.ss_family == orig.ss_family)
    {
        const char addr_mismatch[] = "Binary parsed peer address (%s) does not match "
                                     "original binary address (%s).\n";
        const char port_mismatch[] = "Parsed peer address port %i does not match original "
                                     "address port %i.\n";
        if (orig.ss_family == AF_INET)
        {
            auto peer_addr = (const sockaddr_in*)&parsed.peer_addr;
            auto orig_addr = (const sockaddr_in*)&orig;
            if (memcmp(&peer_addr->sin_addr, &orig_addr->sin_addr, sizeof(peer_addr->sin_addr)))
            {
                printf(addr_mismatch, parsed.peer_addr_str.c_str(), orig_str.c_str());
                rval++;
            }

            if (peer_addr->sin_port != orig_addr->sin_port)
            {
                printf(port_mismatch, ntohs(peer_addr->sin_port), ntohs(orig_addr->sin_port));
                rval++;
            }
        }
        else if (orig.ss_family == AF_INET6)
        {
            auto peer_addr = (const sockaddr_in6*)&parsed.peer_addr;
            auto orig_addr = (const sockaddr_in6*)&orig;
            if (memcmp(&peer_addr->sin6_addr, &orig_addr->sin6_addr, sizeof(peer_addr->sin6_addr)))
            {
                printf(addr_mismatch, parsed.peer_addr_str.c_str(), orig_str.c_str());
                rval++;
            }

            if (peer_addr->sin6_port != orig_addr->sin6_port)
            {
                printf(port_mismatch, ntohs(peer_addr->sin6_port),
                       ntohs(orig_addr->sin6_port));
                rval++;
            }
        }
        else if (orig.ss_family == AF_UNIX)
        {
            auto peer_addr = (const sockaddr_un*)&parsed.peer_addr;
            auto orig_addr = (const sockaddr_un*)&orig;
            if (memcmp(&peer_addr->sun_path, &orig_addr->sun_path, sizeof(peer_addr->sun_path)))
            {
                printf(addr_mismatch, peer_addr->sun_path, orig_str.c_str());
                rval++;
            }
        }
        else
        {
            mxb_assert(!true);
        }
    }
    else
    {
        printf("Parsed peer address family %i does not match original family %i.\n",
               parsed.peer_addr.ss_family, orig.ss_family);
        rval++;
    }
    return rval;
}
}
