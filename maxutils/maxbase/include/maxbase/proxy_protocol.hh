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
#include <arpa/inet.h>
#include <sys/socket.h>

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

struct PreParseResult
{
    enum Type {ERROR, NEED_MORE, TEXT, BINARY};
    Type type {Type::ERROR};
    int  len {-1};
};

/**
 * Detects the type and length of the proxy protocol header.
 *
 * @param data Header data
 * @param datalen Data length. At least four bytes should be available to get some results.
 * @return Result structure. Type set to NEED_MORE if entire header was not available in the data.
 */
PreParseResult pre_parse_header(const uint8_t* data, size_t datalen);

struct HeaderResult
{
    bool             success {false};   /**< Was header successfully parsed? */
    bool             is_proxy {false};  /**< True if the header contains a peer address */
    sockaddr_storage peer_addr;         /**< Peer address and port */
    std::string      peer_addr_str;     /**< Peer address in string form */
};
HeaderResult parse_text_header(const char* header, int header_len);

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
