/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

#include <maxscale/buffer.h>
#include <maxscale/utils.h>
#include <maxscale/jansson.hh>

namespace maxscale
{

/**
 * @brief Left trim a string.
 *
 * @param s  The string to be trimmed.
 */
inline void ltrim(std::string& s)
{
    s.erase(s.begin(),
            std::find_if(s.begin(),
                         s.end(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))));
}

/**
 * @brief Right trim a string.
 *
 * @param s  The string to be trimmed.
 */
inline void rtrim(std::string& s)
{
    s.erase(std::find_if(s.rbegin(),
                         s.rend(),
                         std::not1(std::ptr_fun<int, int>(std::isspace))).base(),
            s.end());
}

/**
 * @brief Trim a string.
 *
 * @param s  The string to be trimmed.
 */
inline void trim(std::string& s)
{
    ltrim(s);
    rtrim(s);
}

/**
 * @brief Left-trimmed copy of a string.
 *
 * @param s  The string to the trimmed.
 *
 * @return A left-trimmed copy of the string.
 */
inline std::string ltrimmed_copy(const std::string& original)
{
    std::string s(original);
    ltrim(s);
    return s;
}

/**
 * @brief Right-trimmed copy of a string.
 *
 * @param s  The string to the trimmed.
 *
 * @return A right-trimmed copy of the string.
 */
inline std::string rtrimmed_copy(const std::string& original)
{
    std::string s(original);
    rtrim(s);
    return s;
}

/**
 * @brief Trimmed copy of a string.
 *
 * @param s  The string to the trimmed.
 *
 * @return A trimmed copy of the string.
 */
inline std::string trimmed_copy(const std::string& original)
{
    std::string s(original);
    ltrim(s);
    rtrim(s);
    return s;
}

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
    std::vector<std::string> rval;
    char* save_ptr;
    char* tok = strtok_r(&str[0], delim, &save_ptr);

    while (tok)
    {
        rval.emplace_back(tok);
        tok = strtok_r(NULL, delim, &save_ptr);
    }

    return rval;
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
}


namespace maxscale
{

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
    typedef typename RegistryTraits<EntryType>::id_type    id_type;
    typedef typename RegistryTraits<EntryType>::entry_type entry_type;

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

private:
    typedef typename std::unordered_map<id_type, entry_type> ContainerType;
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
    EqualPointees(const T& lhs) : m_ppLhs(&lhs)
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
     * @param buffer Buffer to add to the calculation
     */
    virtual void update(GWBUF* buffer) = 0;

    /**
     * Finalize the calculation
     *
     * This function must be called before the hex function is called or
     * a comparison between two Checksums is made. This resets the calculation
     * state so a new checksum can be started after a call to this function is
     * made.
     *
     * Calling finalize will overwrite the currently stored calculation.
     *
     * @param buffer Optional buffer to process before finalizing
     */
    virtual void finalize(GWBUF* buffer = NULL) = 0;

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

    typedef std::array<uint8_t, SHA_DIGEST_LENGTH> Sum;

    SHA1Checksum()
    {
        SHA1_Init(&m_ctx);
        m_sum.fill(0);      // CentOS 6 doesn't like aggregate initialization...
    }

    void update(GWBUF* buffer)
    {
        for (GWBUF* b = buffer; b; b = b->next)
        {
            SHA1_Update(&m_ctx, GWBUF_DATA(b), GWBUF_LENGTH(b));
        }
    }

    void finalize(GWBUF* buffer = NULL)
    {
        update(buffer);
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

    CRC32Checksum()
    {
        m_ctx = crc32(0L, NULL, 0);
    }

    void update(GWBUF* buffer)
    {
        for (GWBUF* b = buffer; b; b = b->next)
        {
            m_ctx = crc32(m_ctx, GWBUF_DATA(b), GWBUF_LENGTH(b));
        }
    }

    void finalize(GWBUF* buffer = NULL)
    {
        update(buffer);
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
}
