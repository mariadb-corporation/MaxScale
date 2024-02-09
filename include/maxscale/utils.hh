/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <cstdio>
#include <netinet/in.h>
#include <sys/un.h>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <maxbase/jansson.hh>
#include <maxbase/string.hh>
#include <maxscale/buffer.hh>

#define MXS_ARRAY_NELEMS(array) ((size_t)(sizeof(array) / sizeof(array[0])))

struct addrinfo;

/** The type of the socket */
enum class MxsSocketType
{
    LISTEN,
    CONNECT
};

/**
 * Configure network socket options
 *
 * This is a helper function for setting various socket options that are always wanted for all types
 * of connections. It sets the socket into nonblocking mode, configures sndbuf and rcvbuf sizes
 * and sets TCP_NODELAY (no Nagle algorithm).
 *
 * @param so   Socket to configure
 * @param type Socket type
 *
 * @return True if configuration was successful
 */
bool configure_network_socket(int so, int type);

/**
 * @brief Create a UNIX domain socket
 *
 * This opens and prepares a UNIX domain socket for use. The @c addr parameter
 * can be given to the bind() function to bind the socket.
 *
 * @param type Type of the socket, either MXS_SOCKET_LISTENER for a listener
 *             socket or MXS_SOCKET_NETWORK for a network connection socket
 * @param addr Pointer to a struct sockaddr_un where the socket configuration
 *             is stored
 * @param path Path to the socket
 *
 * @return The opened socket or -1 on failure
 */
int open_unix_socket(MxsSocketType type, sockaddr_un* addr, const char* path);

/**
 * Create network listener socket. The return value can be given to listen().
 *
 * @param host Address to bind to
 * @param port Port to bind to
 * @return Socket or -1 on error
 */
int open_listener_network_socket(const char* host, uint16_t port);

/**
 * @brief Create an outbound network socket
 *
 * After calling this function, give @c addr and the return value as the parameters to connect().
 *
 * @param port The target port on the host
 * @param addr Pointer to address storage where the socket configuration is stored
 *
 * @return The opened socket or -1 on failure
 */
int open_outbound_network_socket(const addrinfo& ai, uint16_t port, sockaddr_storage* addr);

struct AiDeleter
{
    void operator()(addrinfo* ai);
};

using SAddrInfo = std::unique_ptr<addrinfo, AiDeleter>;

namespace maxscale
{
/**
 * Resolve hostname to IP address
 *
 * @param host The host to resolve
 * @param flgs Flags passed as a hint in the `ai_flags` field of the `addrinfo` structure
 *
 * @return The IP address if the resolution was successful and an error message if it wasn't
 */
std::tuple<SAddrInfo, std::string> getaddrinfo(const char* host, int flags = 0);
}

/**
 * Return true if the address info lists are equal.
 */
bool addrinfo_equal(const addrinfo* lhs, const addrinfo* rhs);

char* gw_strend(const char* s);
void  gw_sha1_str(const uint8_t* in, int in_len, uint8_t* out);
void  gw_sha1_2_str(const uint8_t* in, int in_len, const uint8_t* in2, int in2_len, uint8_t* out);
int   gw_getsockerrno(int fd);

bool is_valid_posix_path(char* path);

/**
 * Create a directory and any parent directories that do not exist
 *
 * @param path       Path to create
 * @param mask       Bitmask to use
 * @param log_errors Whether to log errors
 *
 * @return True if directory exists or it was successfully created, false on error
 */
bool mxs_mkdir_all(const char* path, int mask, bool log_errors = true);

/**
 * Return the number of processors on the system
 *
 * @return Number of processors or 1 if the information is not available
 */
long get_processor_count();

/**
 * Get the number of CPUs (cores) that are available to the process
 *
 * This differs from get_processor_count() by taking CPU affinities into account. This
 * results to a number that is smaller than or equal to what @c get_processor_count()
 * returns.
 *
 * @return Number of available cores or 1 if the information is not available.
 */
long get_cpu_count();

/**
 * Get number of virtual CPUs (cores) that are available to the process
 *
 * This differs from get_processor_count() by taking CPU affinities and cgroup CPU quotas into account. This
 * results in a "virtual" CPU count that estimates how much CPU resoures, in terms of CPU cores, are
 * available. Note that the returned value may be a fraction.
 *
 * @return The "virtual" CPU count available to this process. If no limits or quotas have been placed, this
 *         will return the same value as get_processor_count().
 */
double get_vcpu_count();

/**
 * Returns the CPU quota and period of the current process.
 *
 * The functions looks for this information in /sys/fs/cgroup.
 *
 * @param quota   On output, the process CPU quota.
 * @param period  On output, the process CPU period.
 *
 * @return True, if the quota and period could be obtained.
 */
bool get_cpu_quota_and_period(int* quota, int* period);

/**
 * Return total system memory
 *
 * @return Total memory in bytes or 0 if the information is not available
 */
int64_t get_total_memory();

/**
 * @brief Get the current available memory
 *
 * Compared to get_total_memory(), this function takes cgroup memory limitations into use.
 *
 * @return The amount of memory this process can use. If no limits have been placed, this will return the same
 *         value as get_total_memory()
 */
int64_t get_available_memory();

namespace maxscale
{

/**
 * Tokenize a string
 *
 * @param str   String to tokenize
 * @param delim List of delimiters (see strtok(3))
 *
 * @return List of tokenized strings
 */
inline std::vector<std::string> strtok(std::string str, const char* delim)
{
    return mxb::strtok(str, delim);
}

// binary compare of pointed-to objects
template<typename Ptr>
bool equal_pointees(const Ptr& lhs, const Ptr& rhs)
{
    return *lhs == *rhs;
}

// Unary predicate for equality of pointed-to objects
template<typename T>
class EqualPointees
{
public:
    EqualPointees(const T& lhs)
        : m_ppLhs(&lhs)
    {
    }
    bool operator()(const T& pRhs)
    {
        return **m_ppLhs == *pRhs;
    }
private:
    const T* m_ppLhs;
};

template<typename T>
EqualPointees<T> equal_pointees(const T& t)
{
    return EqualPointees<T>(t);
}

/**
 * Decode hexadecimal data
 *
 * @param str Hex string to decode
 *
 * @return Decoded data
 */
std::vector<uint8_t> from_hex(const std::string& str);

/**
 * Encode data as Base64
 *
 * @param ptr Pointer to data to convert
 * @param len Length of data pointed by `ptr`
 *
 * @return Base64 encoded string of the given data
 */
std::string to_base64(const uint8_t* ptr, size_t len);
template<class T>
static inline std::string to_base64(const T& v)
{
    return to_base64(v.data(), v.size());
}

/**
 * Decode Base64 data
 *
 * @param str Base64 string to decide
 *
 * @return Decoded data
 */
std::vector<uint8_t> from_base64(std::string_view str);

/**
 * C++ wrapper function for the `crypt` password hashing
 *
 * @param password Password to hash
 * @param salt     Salt to use (see man crypt)
 *
 * @return The hashed password
 */
std::string crypt(const std::string& password, const std::string& salt);

/**
 * Get kernel version
 *
 * @return The kernel version as `major * 10000 + minor * 100 + patch`
 */
int get_kernel_version();

/**
 * Does the system support SO_REUSEPORT
 *
 * @return True if the system supports SO_REUSEPORT
 */
bool have_so_reuseport();

/**
 * Create a HEX(SHA1(SHA1(password)))
 *
 * @param password      Cleartext password
 * @return              Double-hashed password
 */
std::string create_hex_sha1_sha1_passwd(const char* passwd);

/**
 * Converts a HEX string to binary data, combining two hex chars to one byte.
 *
 * @param out Preallocated output buffer
 * @param in Input string
 * @param in_len Input string length
 * @return True on success
 */
bool hex2bin(const char* in, unsigned int in_len, uint8_t* out);

/**
 * Convert binary data to hex string. Doubles the size.
 *
 * @param in Input buffer
 * @param len Input buffer length
 * @param out Preallocated output buffer
 * @return Output or null on error
 */
char* bin2hex(const uint8_t* in, unsigned int len, char* out);

/**
 * Fill a preallocated buffer with XOR(in1, in2). Byte arrays should be equal length.
 *
 * @param input1 First input
 * @param input2 Second input
 * @param input_len Input length
 * @param output Output buffer
 */
void bin_bin_xor(const uint8_t* input1, const uint8_t* input2, unsigned int input_len, uint8_t* output);
}

/**
 * Remove duplicate and trailing forward slashes from a path.
 *
 * @param path Path to clean up
 *
 * @return The @c path parameter
 */
std::string clean_up_pathname(std::string path);
