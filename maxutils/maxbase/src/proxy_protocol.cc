/*
 * Copyright (c) 2023 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
#include <maxbase/proxy_protocol.hh>
#include <maxbase/format.hh>
#include <maxbase/string.hh>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/un.h>

namespace
{

bool addr_matches_subnet(const sockaddr_storage& addr, const mxb::proxy_protocol::Subnet& subnet);
int  compare_bits(const void* s1, const void* s2, size_t n_bits);
bool parse_subnet(char* addr_str, mxb::proxy_protocol::Subnet* subnet_out);
bool normalize_subnet(mxb::proxy_protocol::Subnet* subnet);

int      read_be_uint16(const uint8_t* ptr);
uint8_t* write_be_uint16(uint8_t* ptr, uint16_t val_host);

const char PROXY_TEXT_SIG[] = "PROXY";
const uint8_t PROXY_BIN_SIG[] = {0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A};
const int TEXT_HDR_MAX_LEN = 107;
}

namespace maxbase
{
namespace proxy_protocol
{
TextHdrRes gen_text_header(const sockaddr_storage& client_addr, const sockaddr_storage& server_addr)
{
    TextHdrRes rval;

    auto write_inet4_addr = [&rval](const sockaddr_storage& addr, char* addr_out, int* port_out) {
        bool ntop_success = false;
        const auto& addr4 = reinterpret_cast<const sockaddr_in&>(addr);
        if (inet_ntop(AF_INET, &addr4.sin_addr, addr_out, INET6_ADDRSTRLEN))
        {
            *port_out = ntohs(addr4.sin_port);
            ntop_success = true;
        }
        else
        {
            rval.errmsg = mxb::string_printf("inet_ntop(AF_INET, ...) failed. Error %i: %s",
                                             errno, mxb_strerror(errno));
        }
        return ntop_success;
    };

    auto write_inet6_addr = [&rval](const sockaddr_storage& addr, char* addr_out, int* port_out) {
        bool ntop_success = false;
        const auto& addr6 = reinterpret_cast<const sockaddr_in6&>(addr);
        if (inet_ntop(AF_INET6, &addr6.sin6_addr, addr_out, INET6_ADDRSTRLEN))
        {
            *port_out = ntohs(addr6.sin6_port);
            ntop_success = true;
        }
        else
        {
            rval.errmsg = mxb::string_printf("inet_ntop(AF_INET6, ...) failed. Error %i: %s",
                                             errno, mxb_strerror(errno));
        }
        return ntop_success;
    };

    char client_addr_str[INET6_ADDRSTRLEN];
    int client_port {0};
    char server_addr_str[INET6_ADDRSTRLEN];
    int server_port {0};

    /* Text-header generation gets tricky if server and client address families differ. In these cases,
     * replace the real server address with the client address. The server address is not an important
     * part of the proxy-header and e.g. MariaDB Server only cares that it looks like a valid address.
     * Client address cannot be a unix socket, in such cases a binary header should be used. */
    bool same_addr_families = (client_addr.ss_family == server_addr.ss_family);

    const char* eff_server_addr_str = nullptr;
    const char* family_str = nullptr;

    switch (client_addr.ss_family)
    {
    case AF_INET:
        family_str = "TCP4";
        if (write_inet4_addr(client_addr, client_addr_str, &client_port))
        {
            if (same_addr_families)
            {
                if (write_inet4_addr(server_addr, server_addr_str, &server_port))
                {
                    eff_server_addr_str = server_addr_str;
                }
            }
            else
            {
                eff_server_addr_str = client_addr_str;
                server_port = client_port;
            }
        }
        break;

    case AF_INET6:
        family_str = "TCP6";
        if (write_inet6_addr(client_addr, client_addr_str, &client_port))
        {
            if (same_addr_families)
            {
                if (write_inet6_addr(server_addr, server_addr_str, &server_port))
                {
                    eff_server_addr_str = server_addr_str;
                }
            }
            else
            {
                eff_server_addr_str = client_addr_str;
                server_port = client_port;
            }
        }
        break;

    case AF_UNIX:
        {
            const auto& addr_un = reinterpret_cast<const sockaddr_un&>(client_addr);
            rval.errmsg = mxb::string_printf(
                "Cannot send text-form proxy protocol header for client connected via unix socket '%s'.",
                addr_un.sun_path);
        }
        break;

    default:
        mxb_assert(!true);
        rval.errmsg = mxb::string_printf("Unrecognized socket address family %i.",
                                         (int)client_addr.ss_family);
        break;
    }

    if (eff_server_addr_str)
    {
        const size_t maxlen = sizeof(rval.header);
        int res = snprintf(rval.header, maxlen, "PROXY %s %s %s %d %d\r\n",
                           family_str, client_addr_str, eff_server_addr_str, client_port, server_port);
        if (res < 0 || res >= (int)maxlen)
        {
            rval.errmsg = mxb::string_printf("Could not form proxy protocol header, snprintf returned %i.",
                                             res);
        }
        else
        {
            rval.len = res;
        }
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

    sockaddr_storage normalized_addr{};

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

    char token[256] {};
    auto tokens = mxb::strtok<std::string_view>(networks_str, ", ");

    for (const auto& token_str : tokens)
    {
        if (token_str.length() < sizeof(token))
        {
            memcpy(token, token_str.data(), token_str.length());
            *(token + token_str.length()) = 0;

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
        else
        {
            rval.errmsg = mxb::string_printf("Subnet definition '%s' is too long.",
                                             std::string(token_str).c_str());
            break;
        }
    }

    if (!rval.errmsg.empty())
    {
        rval.subnets.clear();
    }
    return rval;
}

PreParseResult pre_parse_header(const uint8_t* data, size_t datalen)
{
    PreParseResult rval;
    size_t text_sig_len = sizeof(PROXY_TEXT_SIG) - 1;
    if (datalen >= text_sig_len)
    {
        /**
         * Text header starts with "PROXY" and ends in \n (cannot have \n in middle), max len 107
         * characters.
         */
        if (memcmp(data, PROXY_TEXT_SIG, text_sig_len) == 0)
        {
            auto* end_pos = static_cast<const uint8_t*>(memchr(data, '\n', datalen));
            if (end_pos)
            {
                auto header_len = end_pos + 1 - data;
                if (header_len <= TEXT_HDR_MAX_LEN)
                {
                    // Looks like got the entire header.
                    rval.type = PreParseResult::TEXT;
                    rval.len = header_len;
                }
            }
            else if (datalen < TEXT_HDR_MAX_LEN)
            {
                // Need more to determine length.
                rval.type = PreParseResult::INCOMPLETE;
            }
        }
        else
        {
            /**
             * Binary header starts with 12-byte signature, followed by two bytes of info and then a two
             * byte number which tells the remaining length of the header.
             */
            size_t bin_sig_bytes = std::min(datalen, sizeof(PROXY_BIN_SIG));
            if (memcmp(data, PROXY_BIN_SIG, bin_sig_bytes) == 0)
            {
                // Binary data.
                size_t len_offset = 14;
                if (datalen >= len_offset + 2)
                {
                    // The length is big-endian.
                    int remaining_len = read_be_uint16(data + len_offset);
                    size_t total_len = sizeof(PROXY_BIN_SIG) + 2 + 2 + remaining_len;
                    // Sanity check. Don't allow unreasonably long binary headers.
                    if (total_len < 10000)
                    {
                        // Even if we don't have the full header ready, return the length.
                        rval.len = total_len;
                        rval.type = (datalen >= total_len) ? PreParseResult::BINARY :
                            PreParseResult::INCOMPLETE;
                    }
                }
                else
                {
                    rval.type = PreParseResult::INCOMPLETE;
                }
            }
        }
    }
    else
    {
        rval.type = PreParseResult::INCOMPLETE;
    }

    return rval;
}

HdrParseResult parse_text_header(const char* header, int header_len)
{
    HdrParseResult rval;
    char address_family[TEXT_HDR_MAX_LEN + 1];
    char client_address[TEXT_HDR_MAX_LEN + 1];
    char server_address[TEXT_HDR_MAX_LEN + 1];
    int client_port {0};
    int server_port {0};

    // About to use sscanf. To prevent any possibility of reading past end, copy the string and add 0.
    char header_copy[header_len + 1];
    memcpy(header_copy, header, header_len);
    header_copy[header_len] = '\0';

    int ret = sscanf(header_copy, "PROXY %s %s %s %d %d",
                     address_family, client_address, server_address,
                     &client_port, &server_port);
    if (ret >= 1 && ret < 5)
    {
        // At least something was parsed. Anything after "UNKNOWN" should be ignored.
        if (strcmp(address_family, "UNKNOWN") == 0)
        {
            rval.success = true;
        }
    }
    else if (ret == 5)
    {
        if (client_port >= 0 && client_port <= 0xffff && server_port >= 0 && server_port <= 0xffff)
        {
            // Check again for "UNKNOWN".
            if (strcmp(address_family, "UNKNOWN") == 0)
            {
                rval.success = true;
            }
            else
            {
                bool client_addr_ok = false;

                if (strcmp(address_family, "TCP4") == 0)
                {
                    auto* addr = reinterpret_cast<sockaddr_in*>(&rval.peer_addr);
                    addr->sin_family = AF_INET;
                    addr->sin_port = htons(client_port);
                    if (inet_pton(AF_INET, client_address, &addr->sin_addr) == 1)
                    {
                        client_addr_ok = true;
                    }
                }
                else if (strcmp(address_family, "TCP6") == 0)
                {
                    auto* addr = reinterpret_cast<sockaddr_in6*>(&rval.peer_addr);
                    addr->sin6_family = AF_INET6;
                    addr->sin6_port = htons(client_port);
                    if (inet_pton(AF_INET6, client_address, &addr->sin6_addr) == 1)
                    {
                        client_addr_ok = true;
                    }
                }

                if (client_addr_ok)
                {
                    // Looks good. Finally, check that the server address is valid.
                    uint8_t dummy[16];
                    if (inet_pton(rval.peer_addr.ss_family, server_address, dummy) == 1)
                    {
                        rval.success = true;
                        rval.is_proxy = true;
                        rval.peer_addr_str = client_address;
                    }
                }
            }
        }
    }
    return rval;
}

HdrParseResult parse_binary_header(const uint8_t* header)
{
    // The header should be valid in the sense that it has the signature and length info, and the array
    // is long enough.
    HdrParseResult rval;
    size_t sig_len = 12;
    if (memcmp(header, PROXY_BIN_SIG, sig_len) == 0)
    {
        // Next byte is protocol version and command.
        uint8_t version = (header[sig_len] & 0xF0) >> 4;
        if (version == 2)
        {
            uint8_t command = (header[sig_len] & 0xF);
            if (command == 0)
            {
                // Connection created by the proxy itself. More bytes remain but they can be ignored.
                rval.success = true;
            }
            else if (command == 1)
            {
                // Real proxied connection. Next byte is transport protocol and address family. We only
                // expect a few combinations.
                uint8_t family = header[13];
                // Next two bytes are length.
                int remaining_len = read_be_uint16(header + 14);
                auto* src_ptr = header + 16;

                if (family == 0x11)
                {
                    // IPv4. Should have at least 12 bytes left.
                    if (remaining_len >= 12)
                    {
                        auto* addr = (sockaddr_in*)&rval.peer_addr;
                        addr->sin_family = AF_INET;

                        auto bytes = sizeof(addr->sin_addr);
                        memcpy(&addr->sin_addr, src_ptr, bytes);
                        src_ptr += 2 * bytes;                                       // Skip server address
                        memcpy(&addr->sin_port, src_ptr, sizeof(addr->sin_port));   // Written in big-endian.

                        char text_addr[INET_ADDRSTRLEN];
                        if (inet_ntop(AF_INET, &addr->sin_addr, text_addr, sizeof(text_addr)))
                        {
                            rval.success = true;
                            rval.is_proxy = true;
                            rval.peer_addr_str = text_addr;
                        }
                    }
                }
                else if (family == 0x21)
                {
                    // IPv6. Should have at least 36 bytes left.
                    if (remaining_len >= 36)
                    {
                        auto* addr = (sockaddr_in6*)&rval.peer_addr;
                        addr->sin6_family = AF_INET6;

                        auto bytes = sizeof(addr->sin6_addr);
                        memcpy(&addr->sin6_addr, src_ptr, bytes);
                        src_ptr += 2 * bytes;
                        memcpy(&addr->sin6_port, src_ptr, sizeof(addr->sin6_port));

                        char text_addr[INET6_ADDRSTRLEN];
                        if (inet_ntop(AF_INET6, &addr->sin6_addr, text_addr, sizeof(text_addr)))
                        {
                            rval.success = true;
                            rval.is_proxy = true;
                            rval.peer_addr_str = text_addr;
                        }
                    }
                }
                else if (family == 0x31)
                {
                    // Unix socket.
                    if (remaining_len >= 216)
                    {
                        auto* addr = (sockaddr_un*)&rval.peer_addr;
                        addr->sun_family = AF_UNIX;

                        auto bytes = sizeof(addr->sun_path);
                        memcpy(&addr->sun_path, src_ptr, bytes);

                        rval.success = true;
                        rval.is_proxy = true;
                    }
                }
            }
        }
    }
    return rval;
}

BinHdrRes gen_binary_header(const sockaddr_storage& client_addr, const sockaddr_storage& server_addr)
{
    // Generate a binary header as described in http://www.haproxy.org/download/1.8/doc/proxy-protocol.txt

    BinHdrRes rval;
    auto* ptr = rval.header;
    memcpy(ptr, PROXY_BIN_SIG, sizeof(PROXY_BIN_SIG));
    ptr += sizeof(PROXY_BIN_SIG);

    *ptr++ = 0x21;      // Protocol version and command (1 = proxy).

    // Only consider the client address family. MariaDB Server does not even read the server
    // address from a binary header.
    bool same_addr_families = (client_addr.ss_family == server_addr.ss_family);

    switch (client_addr.ss_family)
    {
    case AF_INET:
        {
            *ptr++ = 0x11;
            ptr = write_be_uint16(ptr, 12);
            auto* cli_addr4 = reinterpret_cast<const sockaddr_in*>(&client_addr);
            auto* srv_addr4 = reinterpret_cast<const sockaddr_in*>(&server_addr);

            size_t addr_size = sizeof(cli_addr4->sin_addr);
            memcpy(ptr, &cli_addr4->sin_addr, addr_size);
            ptr += addr_size;

            // Next is server address. If it's IP4, add it.
            if (same_addr_families)
            {
                memcpy(ptr, &srv_addr4->sin_addr, addr_size);
            }
            else
            {
                memset(ptr, 0, addr_size);      // Hopefully the receiver doesn't care.
            }
            ptr += addr_size;

            size_t port_size = sizeof(cli_addr4->sin_port);
            memcpy(ptr, &cli_addr4->sin_port, port_size);   // Big-endian value in both source and dest.
            ptr += port_size;

            if (same_addr_families)
            {
                memcpy(ptr, &srv_addr4->sin_port, port_size);
            }
            else
            {
                memset(ptr, 0, port_size);
            }
            ptr += port_size;
            mxb_assert(ptr - rval.header == 28);
        }
        break;

    case AF_INET6:
        {
            *ptr++ = 0x21;
            ptr = write_be_uint16(ptr, 36);
            auto* cli_addr6 = reinterpret_cast<const sockaddr_in6*>(&client_addr);
            auto* srv_addr6 = reinterpret_cast<const sockaddr_in6*>(&server_addr);

            size_t addr_size = sizeof(cli_addr6->sin6_addr);
            memcpy(ptr, &cli_addr6->sin6_addr, addr_size);
            ptr += addr_size;

            if (same_addr_families)
            {
                memcpy(ptr, &srv_addr6->sin6_addr, addr_size);
            }
            else
            {
                memset(ptr, 0, addr_size);
            }
            ptr += addr_size;

            size_t port_size = sizeof(cli_addr6->sin6_port);
            memcpy(ptr, &cli_addr6->sin6_port, port_size);
            ptr += port_size;

            if (same_addr_families)
            {
                memcpy(ptr, &srv_addr6->sin6_port, port_size);
            }
            else
            {
                memset(ptr, 0, port_size);
            }
            ptr += port_size;
            mxb_assert(ptr - rval.header == 52);
        }
        break;

    case AF_UNIX:
        {
            *ptr++ = 0x31;
            ptr = write_be_uint16(ptr, 216);
            auto* cli_addr_un = reinterpret_cast<const sockaddr_un*>(&client_addr);
            auto* srv_addr_un = reinterpret_cast<const sockaddr_un*>(&server_addr);

            size_t addr_size = sizeof(cli_addr_un->sun_path);
            memcpy(ptr, &cli_addr_un->sun_path, addr_size);
            ptr += addr_size;

            if (same_addr_families)
            {
                memcpy(ptr, &srv_addr_un->sun_path, addr_size);
            }
            else
            {
                memset(ptr, 0, addr_size);
            }
            ptr += addr_size;
            mxb_assert(ptr - rval.header == 232);
        }
        break;

    default:
        mxb_assert(!true);
        break;
    }

    rval.len = ptr - rval.header;
    return rval;
}
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

    default:
        memcpy(dst, &src, sizeof(src));
        break;
    }
}
}

namespace
{
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

    bool mask_ok = false;
    char* pmask = strchr(addr_str, '/');
    if (!pmask)
    {
        subnet_out->bits = max_mask_bits;
        mask_ok = true;
    }
    else
    {
        // Parse the number after '/'.
        *pmask++ = 0;   // So inet_pton() stops reading.
        if (isdigit(*pmask))
        {
            char* endptr = nullptr;
            long int n_bits = strtol(pmask, &endptr, 10);
            if (endptr && *endptr == '\0' && n_bits >= 0 && n_bits <= max_mask_bits)
            {
                subnet_out->bits = n_bits;
                mask_ok = true;
            }
        }
    }

    if (mask_ok && inet_pton(subnet_out->family, addr_str, subnet_out->addr) == 1
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

int read_be_uint16(const uint8_t* ptr)
{
    uint16_t value_be = 0;
    memcpy(&value_be, ptr, 2);
    return be16toh(value_be);
}

uint8_t* write_be_uint16(uint8_t* ptr, uint16_t val_host)
{
    uint16_t value_be = htobe16(val_host);
    memcpy(ptr, &value_be, 2);
    return ptr + 2;
}
}
