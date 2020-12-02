/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <stdio.h>
#include <openssl/sha.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <maxscale/buffer.hh>
#include <maxscale/utils.h>
#include <maxscale/jansson.hh>
#include <maxbase/string.hh>

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

/**
 * @class CloserTraits utils.hh <maxscale/utils.hh>
 *
 * A traits class used by Closer. To be specialized for all types that are
 * used with Closer.
 */
template<class T>
struct CloserTraits
{
    /**
     * Closes/frees/destroys a resource.
     *
     * @param t  Close the resource *if* it has not been closed already.
     */
    static void close_if(T t)
    {
        static_assert(sizeof(T) != sizeof(T), "The base closer should never be used");
    }

    /**
     * Resets a reference to a resource. After the call, the value of t should
     * be such that @c close_if can recognize that the reference already has
     * been closed.
     *
     * @param t  Reference to a resource.
     */
    static void reset(T& t);
};

/**
 * @class Closer utils.hh <maxscale/utils.hh>
 *
 * The template class Closer is a class that is intended to be used
 * for ensuring that a C style resource is released at the end of a
 * scoped block, irrespective of how that block is exited (by reaching
 * the end of it, or by a return or exception in the middle of it).
 *
 * Closer performs the actual resource releasing using CloserTraits
 * that need to be specialized for every type of resource to be managed.
 *
 * Example:
 * @code
 * void f()
 * {
 *     FILE* pFile = fopen(...);
 *
 *     if (pFile)
 *     {
 *         Closer<FILE*> file(pFile);
 *
 *         // Use pFile, call functions that potentually may throw
 *     }
 * }
 * @endcode
 *
 * Without @c Closer all code would have to be placed within try/catch
 * blocks, which quickly becomes unwieldy as the number of managed
 * resources grows.
 */
template<class T>
class Closer
{
public:
    /**
     * Creates the closer and stores the provided resourece. Note that
     * the constructor assumes that the resource exists already.
     *
     * @param resource  The resource whose closing is to be ensured.
     */
    Closer(T resource)
        : m_resource(resource)
    {
    }

    /**
     * Destroys the closer and releases the resource.
     */
    ~Closer()
    {
        CloserTraits<T>::close_if(m_resource);
    }

    /**
     * Returns the original resource. Note that the ownership of the
     * resource remains with the closer.
     *
     * @return The resource that was provided in the constructor.
     */
    T get() const
    {
        return m_resource;
    }

    /**
     * Resets the closer, that is, releases the resource.
     */
    void reset()
    {
        CloserTraits<T>::close_if(m_resource);
        CloserTraits<T>::reset(m_resource);
    }

    /**
     * Resets the closer, that is, releases the resource and assigns a
     * new resource to it.
     */
    void reset(T resource)
    {
        CloserTraits<T>::close_if(m_resource);
        m_resource = resource;
    }

    /**
     * Returns the original resource together with its ownership. That is,
     * after this call the responsibility for releasing the resource belongs
     * to the caller.
     *
     * @return The resource that was provided in the constructor.
     */
    T release()
    {
        T resource = m_resource;
        CloserTraits<T>::reset(m_resource);
        return resource;
    }

private:
    Closer(const Closer&);
    Closer& operator=(const Closer&);

private:
    T m_resource;
};

/**
 * @class CloserTraits<FILE*> utils.hh <maxscale/utils.hh>
 *
 * Specialization of @c CloserTraits for @c FILE*.
 */
template<>
struct CloserTraits<FILE*>
{
    static void close_if(FILE* pFile)
    {
        if (pFile)
        {
            fclose(pFile);
        }
    }

    static void reset(FILE*& pFile)
    {
        pFile = NULL;
    }
};

/* Helper type for Registry. Must be specialized for each EntryType. The types
 * listed below are just examples and will not compile. */
template<typename EntryType>
struct RegistryTraits
{
    typedef int        id_type;
    typedef EntryType* entry_type;

    static id_type get_id(entry_type entry)
    {
        static_assert(sizeof(EntryType) != sizeof(EntryType),
                      "get_id() and the"
                      " surrounding struct must be specialized for every EntryType!");
        return 0;
    }
    static entry_type null_entry()
    {
        return NULL;
    }
};

/**
 * Class Registy wraps a map, allowing only a few operations on it. The intended
 * use is simple registries, such as the session registry in Worker. The owner
 * can expose a reference to this class without exposing all the methods the
 * underlying container implements. When instantiating with a new EntryType, the
 * traits-class RegistryTraits should be specialized for the new type as well.
 */
template<typename EntryType>
class Registry
{
    Registry(const Registry&);
    Registry& operator=(const Registry&);
public:
    typedef typename RegistryTraits<EntryType>::id_type      id_type;
    typedef typename RegistryTraits<EntryType>::entry_type   entry_type;
    typedef typename std::unordered_map<id_type, entry_type> ContainerType;
    typedef typename ContainerType::const_iterator           const_iterator;

    Registry()
    {
    }
    /**
     * Find an entry in the registry.
     *
     * @param id Entry key
     * @return The found entry, or NULL if not found
     */
    entry_type lookup(id_type id) const
    {
        entry_type rval = RegistryTraits<EntryType>::null_entry();
        typename ContainerType::const_iterator iter = m_registry.find(id);
        if (iter != m_registry.end())
        {
            rval = iter->second;
        }
        return rval;
    }

    /**
     * Add an entry to the registry.
     *
     * @param entry The entry to add
     * @return True if successful, false if id was already in
     */
    bool add(entry_type entry)
    {
        id_type id = RegistryTraits<EntryType>::get_id(entry);
        typename ContainerType::value_type new_value(id, entry);
        return m_registry.insert(new_value).second;
    }

    /**
     * Remove an entry from the registry.
     *
     * @param id Entry id
     * @return True if an entry was removed, false if not found
     */
    bool remove(id_type id)
    {
        entry_type rval = lookup(id);
        if (rval)
        {
            m_registry.erase(id);
        }
        return rval;
    }

    const_iterator begin() const
    {
        return m_registry.begin();
    }

    const_iterator end() const
    {
        return m_registry.end();
    }

    bool empty() const
    {
        return m_registry.empty();
    }

    auto size() const
    {
        return m_registry.size();
    }

private:
    ContainerType m_registry;
};

// binary compare of pointed-to objects
template<typename Ptr>
bool equal_pointees(const Ptr& lhs, const Ptr& rhs)
{
    return *lhs == *rhs;
}

// Unary predicate for equality of pointed-to objects
template<typename T>
class EqualPointees : public std::unary_function<T, bool>
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
 * Get hexadecimal string representation of @c value
 *
 * @param value Value to convert
 *
 * @return Hexadecimal string representation of @c value
 */
std::string to_hex(uint8_t value);

template<typename T, typename V>
struct hex_iterator
{
};

template<typename T>
struct hex_iterator<T, uint8_t>
{
    std::string operator()(T begin, T end)
    {
        std::string rval;
        for (auto it = begin; it != end; it++)
        {
            rval += to_hex(*it);
        }
        return rval;
    }
};

/**
 * Create hexadecimal representation of a type
 *
 * @param begin Starting iterator
 * @param end   End iterator
 *
 * @return Hexadecimal string representation of the data
 */
template<typename Iter>
std::string to_hex(Iter begin, Iter end)
{
    return hex_iterator<Iter, typename std::iterator_traits<Iter>::value_type>()(begin, end);
}

/**
 * Encode data as Base64
 *
 * @param ptr Pointer to data to convert
 * @param len Length of data pointed by `ptr`
 *
 * @return Base64 encoded string of the given data
 */
std::string to_base64(const uint8_t* ptr, size_t len);
static inline std::string to_base64(const std::vector<uint8_t>& v)
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
std::vector<uint8_t> from_base64(const std::string& str);

/**
 * Base class for checksums
 */
class Checksum
{
public:

    virtual ~Checksum()
    {
    }

    /**
     * Update the checksum calculation
     *
     * @param ptr Pointer to data
     * @param len Length of the data
     */
    virtual void update(uint8_t* ptr, size_t len) = 0;

    /**
     * Update the checksum calculation
     *
     * @param buffer Buffer to add to the calculation
     */
    void update(GWBUF* buffer)
    {
        for (GWBUF* b = buffer; b; b = b->next)
        {
            update(GWBUF_DATA(b), gwbuf_link_length(b));
        }
    }

    /**
     * Finalize the calculation
     *
     * This function must be called before the hex function is called or
     * a comparison between two Checksums is made. This resets the calculation
     * state so a new checksum can be started after a call to this function is
     * made.
     *
     * Calling finalize will overwrite the currently stored calculation.
     */
    virtual void finalize() = 0;

    // Overload with an update before finalize
    void finalize(GWBUF* buffer)
    {
        update(buffer);
        finalize();
    }

    /**
     * Reset the checksum to a zero state
     */
    virtual void reset() = 0;

    /**
     * Get hexadecimal representation of the checksum
     *
     * @return String containing the hexadecimal form of the checksum
     */
    virtual std::string hex() const = 0;
};

/**
 * A SHA1 checksum
 */
class SHA1Checksum : public Checksum
{
public:
    using Checksum::update;
    using Checksum::finalize;
    using Sum = std::array<uint8_t, SHA_DIGEST_LENGTH>;

    SHA1Checksum()
    {
        SHA1_Init(&m_ctx);
        m_sum.fill(0);      // CentOS 6 doesn't like aggregate initialization...
    }

    void update(uint8_t* ptr, size_t len)
    {
        SHA1_Update(&m_ctx, ptr, len);
    }

    void finalize()
    {
        SHA1_Final(&m_sum.front(), &m_ctx);
    }

    void reset()
    {
        SHA1_Init(&m_ctx);
    }

    std::string hex() const
    {
        return mxs::to_hex(m_sum.begin(), m_sum.end());
    }

    bool eq(const SHA1Checksum& rhs) const
    {
        return m_sum == rhs.m_sum;
    }

private:

    SHA_CTX m_ctx;  /**< SHA1 context */
    Sum     m_sum;  /**< Final checksum */
};

static inline bool operator==(const SHA1Checksum& lhs, const SHA1Checksum& rhs)
{
    return lhs.eq(rhs);
}

static inline bool operator!=(const SHA1Checksum& lhs, const SHA1Checksum& rhs)
{
    return !(lhs == rhs);
}

/**
 * A CRC32 checksum
 */
class CRC32Checksum : public Checksum
{
public:
    using Checksum::update;
    using Checksum::finalize;

    CRC32Checksum()
    {
        m_ctx = crc32(0L, NULL, 0);
    }

    void update(uint8_t* ptr, size_t len)
    {
        m_ctx = crc32(m_ctx, ptr, len);
    }

    void finalize()
    {
        m_sum = m_ctx;
        reset();
    }

    void reset()
    {
        m_ctx = crc32(0L, NULL, 0);
    }

    std::string hex() const
    {
        const uint8_t* start = reinterpret_cast<const uint8_t*>(&m_sum);
        const uint8_t* end = start + sizeof(m_sum);
        return mxs::to_hex(start, end);
    }

    bool eq(const CRC32Checksum& rhs) const
    {
        return m_sum == rhs.m_sum;
    }

private:

    uint32_t m_ctx;     /**< Ongoing checksum value */
    uint32_t m_sum;     /**< Final checksum */
};

static inline bool operator==(const CRC32Checksum& lhs, const CRC32Checksum& rhs)
{
    return lhs.eq(rhs);
}

static inline bool operator!=(const CRC32Checksum& lhs, const CRC32Checksum& rhs)
{
    return !(lhs == rhs);
}

// Convenience function for calculating a hex checksum
template<class T>
std::string checksum(uint8_t* ptr, size_t len)
{
    T cksum;
    cksum.update(ptr, len);
    cksum.finalize();
    return cksum.hex();
}

template<class T>
std::string checksum(const std::string& str)
{
    return checksum<T>((uint8_t*)str.c_str(), str.size());
}

/**
 * Read bytes into a 64-bit unsigned integer.
 *
 * @param ptr   Pointer where value is stored. Read as a little-endian byte array.
 * @param bytes How many bytes to read. Must be 0 to 8.
 *
 * @return The read value
 */
uint64_t get_byteN(const uint8_t* ptr, int bytes);

/**
 * Store bytes to a byte array in little endian format.
 *
 * @param ptr   Pointer where value should be stored
 * @param value Value to store
 * @param bytes How many bytes to store. Must be 0 to 8.
 *
 * @return The next byte after the stored value
 */
uint8_t* set_byteN(uint8_t* ptr, uint64_t value, int bytes);

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
