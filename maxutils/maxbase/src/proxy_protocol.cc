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

void get_normalized_ip(const sockaddr_storage& src, sockaddr_storage* dst);
bool addr_matches_subnet(const sockaddr_storage& addr, const mxb::proxy_protocol::Subnet& subnet);
int  compare_bits(const void* s1, const void* s2, size_t n_bits);
bool parse_subnet(char* addr_str, mxb::proxy_protocol::Subnet* subnet_out);
bool normalize_subnet(mxb::proxy_protocol::Subnet* subnet);

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

bool is_proxy_protocol_allowed(const sockaddr_storage& addr, const SubnetArray& allowed_subnets)
{
    if (allowed_subnets.empty())
    {
        return false;
    }

    sockaddr_storage normalized_addr;

    // Non-TCP addresses (unix domain socket) are treated as the localhost address.
    switch (addr.ss_family)
    {
    case AF_UNSPEC:
    case AF_UNIX:
        normalized_addr.ss_family = AF_UNIX;
        break;

    case AF_INET:
    case AF_INET6:
        get_normalized_ip(addr, &normalized_addr);
        break;

    default:
        mxb_assert(!true);
    }

    bool rval = false;
    for (const auto& subnet : allowed_subnets)
    {
        if (addr_matches_subnet(normalized_addr, subnet))
        {
            rval = true;
            break;
        }
    }

    return rval;
}

SubnetParseResult parse_networks_from_string(const std::string& networks_str)
{
    SubnetParseResult rval;
    // Handle some special cases.
    if (networks_str.empty())
    {
        return rval;
    }
    else if (networks_str == "*")   // Config string should not have any spaces
    {
        rval.subnets.resize(3);
        rval.subnets[0].family = AF_INET;
        rval.subnets[1].family = AF_INET6;
        rval.subnets[2].family = AF_UNIX;
        return rval;
    }

    char token[256];
    size_t i = 0;
    size_t len = networks_str.length();

    while (i < len)
    {
        char c = networks_str[i];
        if (c == ',' || c == ' ')
        {
            i++;
            continue;
        }

        size_t j = 0;
        while (i < len && c != ',' && c != ' ' && j < sizeof(token) - 1)
        {
            token[j++] = networks_str[i++];
            if (i < len)
            {
                c = networks_str[i];
            }
        }

        token[j++] = '\0';
        if (j == sizeof(token))
        {
            // Max length reached. It's possible the token is not completely read yet, so print error.
            rval.errmsg = mxb::string_printf("Subnet definition starting with '%s' is too long.", token);
            break;
        }

        Subnet subnet;
        if (parse_subnet(token, &subnet))
        {
            rval.subnets.push_back(subnet);
        }
        else
        {
            rval.errmsg = mxb::string_printf("Parse error near '%s'.", token);
            break;
        }
    }

    if (!rval.errmsg.empty())
    {
        rval.subnets.clear();
    }
    return rval;
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

void get_normalized_ip(const sockaddr_storage& src, sockaddr_storage* dst)
{
    switch (src.ss_family)
    {
    case AF_INET:
        memcpy(dst, &src, sizeof(sockaddr_in));
        break;

    case AF_INET6:
        {
            auto* src_addr6 = (const sockaddr_in6*)&src;
            const in6_addr* src_ip6 = &(src_addr6->sin6_addr);
            const uint32_t* src_ip6_int32 = (uint32_t*)src_ip6->s6_addr;

            if (IN6_IS_ADDR_V4MAPPED(src_ip6) || IN6_IS_ADDR_V4COMPAT(src_ip6))
            {
                /*
                 * This is an IPv4-mapped or IPv4-compatible IPv6 address. It should be converted to the IPv4
                 * form.
                 */
                auto* dst_ip4 = (sockaddr_in*)dst;
                memset(dst_ip4, 0, sizeof(sockaddr_in));
                dst_ip4->sin_family = AF_INET;
                dst_ip4->sin_port = src_addr6->sin6_port;

                /*
                 * In an IPv4 mapped or compatible address, the last 32 bits represent the IPv4 address. The
                 * byte orders for IPv6 and IPv4 addresses are the same, so a simple copy is possible.
                 */
                dst_ip4->sin_addr.s_addr = src_ip6_int32[3];
            }
            else
            {
                /* This is a "native" IPv6 address. */
                memcpy(dst, &src, sizeof(sockaddr_in6));
            }

            break;
        }
    }
}

bool addr_matches_subnet(const sockaddr_storage& addr, const mxb::proxy_protocol::Subnet& subnet)
{
    mxb_assert(subnet.family == AF_UNIX || subnet.family == AF_INET || subnet.family == AF_INET6);

    bool rval = false;
    if (addr.ss_family == subnet.family)
    {
        if (subnet.family == AF_UNIX)
        {
            rval = true;    // Localhost pipe
        }
        else
        {
            // Get a pointer to the address area.
            void* addr_ptr = (subnet.family == AF_INET) ? (void*)&((sockaddr_in*)&addr)->sin_addr :
                (void*)&((sockaddr_in6*)&addr)->sin6_addr;

            return compare_bits(addr_ptr, subnet.addr, subnet.bits) == 0;
        }
    }
    return rval;
}

/**
 *  Compare memory areas, similar to memcmp(). The size parameter is the bit count, not byte count.
 */
int compare_bits(const void* s1, const void* s2, size_t n_bits)
{
    size_t n_bytes = n_bits / 8;
    if (n_bytes > 0)
    {
        int res = memcmp(s1, s2, n_bytes);
        if (res != 0)
        {
            return res;
        }
    }

    int res = 0;
    size_t bits_remaining = n_bits % 8;
    if (bits_remaining > 0)
    {
        // Compare remaining bits of the last partial byte.
        size_t shift = 8 - bits_remaining;
        uint8_t s1_bits = (((uint8_t*)s1)[n_bytes]) >> shift;
        uint8_t s2_bits = (((uint8_t*)s2)[n_bytes]) >> shift;
        res = (s1_bits > s2_bits) ? 1 : (s1_bits < s2_bits ? -1 : 0);
    }
    return res;
}

bool parse_subnet(char* addr_str, mxb::proxy_protocol::Subnet* subnet_out)
{
    int max_mask_bits = 128;
    if (strchr(addr_str, ':'))
    {
        subnet_out->family = AF_INET6;
    }
    else if (strchr(addr_str, '.'))
    {
        subnet_out->family = AF_INET;
        max_mask_bits = 32;
    }
    else if (strcmp(addr_str, "localhost") == 0)
    {
        subnet_out->family = AF_UNIX;
        subnet_out->bits = 0;
        return true;
    }

    char* pmask = strchr(addr_str, '/');
    if (!pmask)
    {
        subnet_out->bits = max_mask_bits;
    }
    else
    {
        // Parse the number after '/'.
        *pmask++ = 0;
        int b = 0;

        do
        {
            if (*pmask < '0' || *pmask > '9')
            {
                return false;
            }
            b = 10 * b + *pmask - '0';
            if (b > max_mask_bits)
            {
                return false;
            }
            pmask++;
        }
        while (*pmask);

        subnet_out->bits = (unsigned short)b;
    }

    if (inet_pton(subnet_out->family, addr_str, subnet_out->addr) == 1
        && normalize_subnet(subnet_out))
    {
        return true;
    }
    return false;
}

bool normalize_subnet(mxb::proxy_protocol::Subnet* subnet)
{
    auto* addr = (unsigned char*)subnet->addr;
    if (subnet->family == AF_INET6)
    {
        const auto* src_ip6 = (in6_addr*)addr;
        if (IN6_IS_ADDR_V4MAPPED(src_ip6) || IN6_IS_ADDR_V4COMPAT(src_ip6))
        {
            /* Copy the actual IPv4 address (4 last bytes) */
            if (subnet->bits < 96)
            {
                return false;
            }
            subnet->family = AF_INET;
            memcpy(addr, addr + 12, 4);
            subnet->bits -= 96;
        }
    }
    return true;
}
}
