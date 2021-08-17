/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>

#include <string>
#include <iosfwd>
#include <unordered_set>

/** Host is a streamable class that represents an address and port, or a unix domain socket.
 */
namespace maxbase
{

class Host;

std::ostream& operator<<(std::ostream&, const Host& host);
std::istream& operator>>(std::istream&, Host& host);

class Host
{
public:
    enum class Type {Invalid, UnixDomainSocket, HostName, IPV4, IPV6};      // to_string() provided
    static constexpr int InvalidPort = -1;

    Host() = default;   // type() returns Type::Invalid

    /**
     * @brief from_string
     * @param str.  A string parsed according to this format (the brackets are real brackets):
     *              unix_domain_socket | addr | addr:port | [addr] | [addr]:port
     *              'addr' is a plain ipv4, ipv6, host name or unix domain socket.
     *              An ipv6 address with a port must use the format [ipv6]:port.
     *              A unix domain socket must start with a forward slash ('/') and must not specify a port
     *              either in the string or as default_port (-1 will be accepted).
     *              The default port is InvalidPort.
     * @return Host. Check with is_valid() that the input string could be parsed, and port is ok.
     */
    static Host from_string(const std::string& input, int default_port = InvalidPort);

    /**
     * Constructor. Set the address and port. Validation is performed on the combination,
     *              and the Type is set. The passed in address and port are always set without
     *              modification, regardless of validation results, and can be read back with
     *              the address() and port() functions.
     * @param addr. Plain ipv4, ipv6, host name or unix domain socket (no brackets or port specifiers).
     * @param port. Port number. If 'addr' is a unix domain socket, port must be InvalidPort.
     */
    Host(const std::string& addr, int port);

    Type               type() const;
    bool               is_valid() const;
    const std::string& address() const;
    int                port() const;

    const std::string& org_input() const;       // for better error messages

    static bool is_valid_ipv4(const std::string& ip);
    static bool is_valid_ipv6(const std::string& ip);
private:
    void set_type();        // set m_type based on m_address and m_port

    std::string m_address;
    int         m_port;
    Type        m_type = Type::Invalid;
    std::string m_org_input;
};

std::string to_string(Host::Type type);

// impl below
inline Host::Type Host::type() const
{
    return m_type;
}

inline bool Host::is_valid() const
{
    return m_type != Type::Invalid;
}

inline const std::string& Host::address() const
{
    return m_address;
}

inline int Host::port() const
{
    return m_port;
}

inline const std::string& Host::org_input() const
{
    return m_org_input;
}

inline bool operator==(const Host& l, const Host& r)
{
    bool port_ok = (l.port() == r.port())
        || (l.type() == Host::Type::UnixDomainSocket && r.type() == Host::Type::UnixDomainSocket);

    return port_ok && l.address() == r.address() && l.type() == r.type();
}

inline bool operator!=(const Host& l, const Host& r)
{
    return !(l == r);
}

/**
 * Perform domain name resolution on a hostname or text-form IP address.
 *
 * @param host Hostname to convert.
 * @param addresses_out Output buffer. The output is in IPv6-form as returned by "inet_ntop(AF_INET6, ...)".
 * @param error_out Error output
 * @return True if successful
 */
bool name_lookup(const std::string& host, std::unordered_set<std::string>* addresses_out,
                 std::string* error_out = nullptr);

/**
 * Perform reverse DNS on an IP address. This may involve network communication so can be slow.
 *
 * @param ip IP to convert to hostname
 * @param output Where to write the output. If operation fails, original IP is written.
 * @return True on success
 */
bool reverse_name_lookup(const std::string& ip, std::string* output);
}
