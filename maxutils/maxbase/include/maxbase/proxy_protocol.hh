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
#pragma once

#include <maxscale/ccdefs.hh>
#include <string>

struct sockaddr_storage;

namespace maxbase
{
namespace proxy_protocol
{
struct HeaderV1Res
{
    char        header[108];
    int         len {0};
    std::string errmsg;
};
HeaderV1Res
generate_proxy_header_v1(const sockaddr_storage* client_addr, const sockaddr_storage* server_addr);

bool packet_hdr_maybe_proxy(const uint8_t* header);

// Proxy protocol parsing and subnetwork matching code adapted from MariaDB Server.

// Subnetwork address in CIDR format, e.g. 192.168.1.0/24 or 2001:db8::/32.
struct Subnet
{
    char           addr[16] {}; /**< Binary representation of the address, big endian */
    unsigned short family {0};  /**< Address family, AF_INET or AF_INET6 */
    unsigned short bits {0};    /**< subnetwork size */
};
using SubnetArray = std::vector<Subnet>;
bool is_proxy_protocol_allowed(const sockaddr_storage& addr, const SubnetArray& allowed_subnets);

struct SubnetParseResult
{
    SubnetArray subnets;
    std::string errmsg;
};
SubnetParseResult parse_networks_from_string(const std::string& networks_str);
}
}
