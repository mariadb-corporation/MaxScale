/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/host.hh>

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{

struct test_error : public std::runtime_error
{
    test_error(const std::string& msg)
        : std::runtime_error(msg.c_str())
    {
    }
};

void eval(const maxbase::Host& host, maxbase::Host::Type expected)
{
    std::cout << host.org_input() << " => \n"
              << "  as string    " << host << "\n"
              << "  host.type()     " << to_string(host.type()) << "\n"
              << "  host.is_valid() " << host.is_valid() << "\n"
              << "  host.address()  " << host.address() << "\n"
              << "  host.port()     " << host.port() << "\n\n"
    ;
    if (host.type() != expected)
    {
        std::ostringstream os;
        os << "Failed to create a Host with original input '" << host.org_input()
           << "'. Expected type " << to_string(expected) << " got " << to_string(host.type());
        throw test_error(os.str());
    }

    if (host.is_valid())
    {
        std::ostringstream os_host;
        os_host << host;
        std::istringstream is(os_host.str());
        maxbase::Host host2;
        is >> host2;

        if (host != host2)
        {
            std::ostringstream os;
            os << "Failed to stream " << host << " with original input '" << host.org_input()
               << "' out (" << os_host.str() << ") and back in from a stream";
            throw test_error(os.str());
        }
    }
}

void test(const std::string& str, maxbase::Host::Type expected)
{
    maxbase::Host host {maxbase::Host::from_string(str)};
    eval(host, expected);
}
}

int main()
try
{
    std::cout << "\nParsing Constructor!!!!!\n";

    std::cout << "The following should be VALID!!!!!\n";
    test("/tmp/socket", maxbase::Host::Type::UnixDomainSocket);
    test("[/home/socket]", maxbase::Host::Type::UnixDomainSocket);
    test("127.0.0.1:4001", maxbase::Host::Type::IPV4);
    test("[127.0.0.1]:4001", maxbase::Host::Type::IPV4);
    test("[fe80::37f8:99a2:558a:9f5d]:4001", maxbase::Host::Type::IPV6);
    test("[::]:4001", maxbase::Host::Type::IPV6);
    test("google.com:80", maxbase::Host::Type::HostName);
    test("hello-world.fi:3333", maxbase::Host::Type::HostName);

    std::cout << "The following should be INVALID!!!!!\n";
    test("/tmp/socket/", maxbase::Host::Type::Invalid);
    test("[/home/socket]:1234", maxbase::Host::Type::Invalid);
    test("[127.0.0.1]:42B", maxbase::Host::Type::Invalid);
    test("[127.0.0.1]:", maxbase::Host::Type::Invalid);
    test("[127.0.0.1:", maxbase::Host::Type::Invalid);
    test("[127.0.0.1]", maxbase::Host::Type::Invalid);
    test("127.0.0.1", maxbase::Host::Type::Invalid);
    test("_hello_world.fi:3333", maxbase::Host::Type::Invalid);
    test("-hello_world.fi:3333", maxbase::Host::Type::Invalid);
    test("hello--world.fi:3333", maxbase::Host::Type::Invalid);

    std::cout << "\nRegular Constructor!!!!!\n";

    std::cout << "The following should be VALID!!!!!\n";
    eval({"/tmp/socket", maxbase::Host::InvalidPort}, maxbase::Host::Type::UnixDomainSocket);
    eval({"google.com", 80}, maxbase::Host::Type::HostName);
    eval({"123.345.678.901", 4444}, maxbase::Host::Type::IPV4);
    eval({"::", 5555}, maxbase::Host::Type::IPV6);
    eval({"ABCD:ABCD:ABCD:ABCD:ABCD:ABCD:123.123.123.123", 5555}, maxbase::Host::Type::IPV6);

    std::cout << "The following should be INVALID!!!!!\n";
    eval({"/tmp/socket", 52}, maxbase::Host::Type::Invalid);
    eval({"127.0.0.1", 999999}, maxbase::Host::Type::Invalid);
    eval({"127.0.0.1", -42}, maxbase::Host::Type::Invalid);
    eval({"Hello::World!", 42}, maxbase::Host::Type::Invalid);
    eval({"yle .fi", 666}, maxbase::Host::Type::Invalid);

    return EXIT_SUCCESS;
}
catch (test_error& err)
{
    std::cerr << err.what() << std::endl;
    return EXIT_FAILURE;
}
