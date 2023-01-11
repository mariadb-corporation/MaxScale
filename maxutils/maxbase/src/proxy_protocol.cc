/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <maxbase/proxy_protocol.hh>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <maxbase/format.hh>

namespace
{
struct AddressResult
{
    bool        success {false};
    char        addr[INET6_ADDRSTRLEN] {};
    in_port_t   port {0};
    std::string error_msg;
};
AddressResult get_ip_string_and_port(const sockaddr_storage* sa);

const uint8_t PROXY_TEXT_SIG[] = "PROXY";
const uint8_t PROXY_BIN_SIG[] = "\x0D\x0A\x0D\x0A\x00\x0D\x0A\x51\x55\x49\x54\x0A";
}

namespace maxbase
{
namespace proxy_protocol
{
HeaderV1Res generate_proxy_header_v1(const sockaddr_storage* client_addr, const sockaddr_storage* server_addr)
{
    auto client_res = get_ip_string_and_port(client_addr);
    auto server_res = get_ip_string_and_port(server_addr);

    HeaderV1Res rval;
    if (client_res.success && server_res.success)
    {
        const auto cli_addr_fam = client_addr->ss_family;
        const auto srv_addr_fam = server_addr->ss_family;
        // The proxy header must contain the client address & port + server address & port. Both should have
        // the same address family. Since the two are separate connections, it's possible one is IPv4 and
        // the other IPv6. In this case, convert any IPv4-addresses to IPv6-format.
        int ret = -1;
        const int maxlen = 108;     // 108 is the worst-case length defined in the protocol documentation.
        char proxy_header[maxlen];
        if ((cli_addr_fam == AF_INET || cli_addr_fam == AF_INET6)
            && (srv_addr_fam == AF_INET || srv_addr_fam == AF_INET6))
        {
            if (cli_addr_fam == srv_addr_fam)
            {
                auto family_str = (cli_addr_fam == AF_INET) ? "TCP4" : "TCP6";
                ret = snprintf(proxy_header, maxlen, "PROXY %s %s %s %d %d\r\n",
                               family_str, client_res.addr, server_res.addr, client_res.port,
                               server_res.port);
            }
            else if (cli_addr_fam == AF_INET)
            {
                // Connection to server is already IPv6.
                ret = snprintf(proxy_header, maxlen, "PROXY TCP6 ::ffff:%s %s %d %d\r\n",
                               client_res.addr, server_res.addr, client_res.port, server_res.port);
            }
            else
            {
                // Connection to client is already IPv6.
                ret = snprintf(proxy_header, maxlen, "PROXY TCP6 %s ::ffff:%s %d %d\r\n",
                               client_res.addr, server_res.addr, client_res.port, server_res.port);
            }
        }
        else
        {
            ret = snprintf(proxy_header, maxlen, "PROXY UNKNOWN\r\n");
        }

        if (ret < 0 || ret >= maxlen)
        {
            rval.errmsg = mxb::string_printf("Could not form proxy protocol header, snprintf returned %i.",
                                             ret);
        }
        else
        {
            memcpy(rval.header, proxy_header, ret + 1);
            rval.len = ret;
        }
    }
    else if (!client_res.success)
    {
        rval.errmsg = mxb::string_printf("Could not convert network address of source to string form. %s",
                                         client_res.error_msg.c_str());
    }
    else
    {
        rval.errmsg = mxb::string_printf("Could not convert network address of server to string form. "
                                         "%s", server_res.error_msg.c_str());
    }
    return rval;
}

bool packet_hdr_maybe_proxy(const uint8_t* header)
{
    return memcmp(header, PROXY_TEXT_SIG, 4) == 0 || memcmp(header, PROXY_BIN_SIG, 4) == 0;
}

bool is_proxy_protocol_allowed(const sockaddr_storage* addr)
{
    // TODO
    return false;
}
}
}

namespace
{
/* Read IP and port from socket address structure, return IP as string and port
 * as host byte order integer.
 *
 * @param sa A sockaddr_storage containing either an IPv4 or v6 address
 * @return Result structure
 */
AddressResult get_ip_string_and_port(const sockaddr_storage* sa)
{
    AddressResult rval;

    const char errmsg_fmt[] = "inet_ntop() failed. Error %i: %s";
    switch (sa->ss_family)
    {
    case AF_INET:
        {
            const auto* sock_info = (const sockaddr_in*)sa;
            const in_addr* addr = &(sock_info->sin_addr);
            if (inet_ntop(AF_INET, addr, rval.addr, sizeof(rval.addr)))
            {
                rval.port = ntohs(sock_info->sin_port);
                rval.success = true;
            }
            else
            {
                rval.error_msg = mxb::string_printf(errmsg_fmt, errno, mxb_strerror(errno));
            }
        }
        break;

    case AF_INET6:
        {
            const auto* sock_info = (const sockaddr_in6*)sa;
            const in6_addr* addr = &(sock_info->sin6_addr);
            if (inet_ntop(AF_INET6, addr, rval.addr, sizeof(rval.addr)))
            {
                rval.port = ntohs(sock_info->sin6_port);
                rval.success = true;
            }
            else
            {
                rval.error_msg = mxb::string_printf(errmsg_fmt, errno, mxb_strerror(errno));
            }
        }
        break;

    default:
        {
            rval.error_msg = mxb::string_printf("Unrecognized socket address family %i.", (int)sa->ss_family);
        }
    }

    return rval;
}
}
