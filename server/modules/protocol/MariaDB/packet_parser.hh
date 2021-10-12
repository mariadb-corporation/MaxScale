/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

namespace packet_parser
{
using ClientInfo = MYSQL_session::ClientCapabilities;

class ByteVec : public std::vector<uint8_t>
{
public:
    using std::vector<uint8_t>::push_back;

    /**
     * Add a null-terminated string.
     *
     * @param str The string
     */
    void push_back(const std::string& str);
};

/** Authentication token parsing depends on packet type. */
enum class AuthPacketType
{
    HANDSHAKE_RESPONSE,
    COM_CHANGE_USER
};

struct AuthParseResult
{
    bool    success {false};        /**< Was parsing successful */
    ByteVec auth_token;             /**< authentication token */
    bool    old_protocol {false};   /**< Is client using too old protocol version? */

    AuthParseResult() = default;
};

struct AttrParseResult
{
    bool    success {false};
    ByteVec attr_data;

    AttrParseResult() = default;
};

struct ClientCapsResult
{
    ClientInfo capabilities;
    uint16_t   collation {0};
};

struct ClientResponseResult
{
    bool success {false};

    std::string username;
    std::string db;
    std::string plugin;

    AuthParseResult token_res;
    AttrParseResult attr_res;

    ClientResponseResult() = default;
};

struct ChangeUserParseResult
{
    bool success {false};

    std::string username;
    std::string db;
    std::string plugin;
    uint16_t    charset {0};

    AuthParseResult token_res;
    AttrParseResult attr_res;

    ChangeUserParseResult() = default;
};

/**
 * Parse 32 bytes of client capabilities.
 *
 * @param data Data array. Should be at least 32 bytes.
 * @param old_info Old client capabilities info from SSLRequest packet.
 * @return Parsed client capabilities
 */
ClientCapsResult parse_client_capabilities(ByteVec& data, const ClientInfo& old_info);

/**
 * Parse username, database etc from client handshake response. Client capabilities should have
 * already been parsed.
 *
 * @param data Data array
 * @return Result object
 */
ClientResponseResult parse_client_response(ByteVec& data, uint32_t client_caps);

/**
 * Parse authentication token from array.
 *
 * @param data Data array
 * @param client_caps Client capabilities
 * @param packet_type Packet type
 * @return Result object
 */
AuthParseResult parse_auth_token(ByteVec& data, uint32_t client_caps, AuthPacketType packet_type);

/**
 * Parse connection attributes from array. The data is extracted as is, without breaking it to key-value
 * pairs.
 *
 * @param data Data array
 * @param client_caps Client capabilities
 * @return Result object
 */
AttrParseResult parse_attributes(ByteVec& data, uint32_t client_caps);

/**
 * Parse fields from a COM_CHANGE_USER-packet.
 *
 * @param data Data array
 * @param client_caps Client capabilities
 * @return Result object
 */
ChangeUserParseResult parse_change_user_packet(ByteVec& data, uint32_t client_caps);

/**
 * Parse fields from an authentication switch request-packet. Is somewhat different from the other parsing
 * functions in that wrong packet type is detected and null-termination is not assumed.
 *
 * @param data Packet data without the header.
 * @return Result object
 */
mariadb::AuthSwitchReqContents parse_auth_switch_request(ByteVec& data);
}
