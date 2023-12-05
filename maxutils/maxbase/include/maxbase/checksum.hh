/*
 * Copyright (c) 2023 MariaDB plc
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

#pragma once
#include <maxbase/ccdefs.hh>

#include <openssl/sha.h>
#include <zlib.h>

#include <maxbase/string.hh>

#define XXH_INLINE_ALL 1
#include <maxbase/xxHash/xxhash.h>

namespace maxbase
{

/**
 * Base class for checksums
 */
template<class Derived>
class ChecksumBase
{
public:
    /**
     * Update the checksum calculation
     *
     * @param ptr Pointer to data
     * @param len Length of the data
     */
    void update(const uint8_t* ptr, size_t len)
    {
        static_cast<Derived*>(this)->update_impl(ptr, len);
    }

    /**
     * Update the checksum calculation
     *
     * @param c Container whose contents are added to the calculation
     */
    template<class Container>
    void update(const Container& c)
    {
        update(c.data(), c.length());
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
    void finalize()
    {
        static_cast<Derived*>(this)->finalize_impl();
    }

    void finalize(const uint8_t* ptr, size_t len)
    {
        static_cast<Derived*>(this)->update_impl(ptr, len);
        static_cast<Derived*>(this)->finalize();
    }

    template<class Container>
    void finalize(const Container& c)
    {
        finalize(c.data(), c.length());
    }

    /**
     * Reset the checksum to a zero state
     */
    void reset()
    {
        static_cast<Derived*>(this)->reset_impl();
    }

    /**
     * Get the value of the checksum
     *
     * @return The checksum value
     */
    auto value() const
    {
        return static_cast<const Derived*>(this)->value_impl();
    }

    /**
     * Get hexadecimal representation of the checksum
     *
     * The value must be finalized before this function is called. Calling reset() on a Checksum will
     * also invalidate the calculation and the hex output.
     *
     * @return String containing the hexadecimal form of the checksum
     */
    std::string hex() const
    {
        const auto& val = value();
        std::array<uint8_t, sizeof(val)> data{};
        memcpy(data.data(), &val, sizeof(val));
        return mxb::to_hex(data.begin(), data.end());
    }

    /**
     * Equality operator, can be defined in the base class
     */
    bool operator==(const Derived& rhs) const
    {
        return static_cast<const Derived*>(this)->value() == rhs.value();
    }

    /**
     * Inequality operator, can be defined in the base class
     */
    bool operator!=(const Derived& rhs) const
    {
        return !(*static_cast<const Derived*>(this) == rhs);
    }
};

/**
 * A SHA1 checksum
 */
class Sha1Sum final : public ChecksumBase<Sha1Sum>
{
// This disables the deprecation warnings for SHA1
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
public:
    friend class ChecksumBase<Sha1Sum>;
    using value_type = std::array<uint8_t, SHA_DIGEST_LENGTH>;

    Sha1Sum()
    {
        SHA1_Init(&m_ctx);
        m_sum.fill(0);      // CentOS 6 doesn't like aggregate initialization...
    }

private:
    void update_impl(const uint8_t* ptr, size_t len)
    {
        SHA1_Update(&m_ctx, ptr, len);
    }

    void finalize_impl()
    {
        SHA1_Final(&m_sum.front(), &m_ctx);
    }

    void reset_impl()
    {
        SHA1_Init(&m_ctx);
        m_sum.fill(0);
    }

    auto value_impl() const
    {
        return m_sum;
    }

    SHA_CTX    m_ctx;   /**< SHA1 context */
    value_type m_sum;   /**< Final checksum */
#pragma GCC diagnostic pop
};

/**
 * A CRC32 checksum
 */
class CRC32 final : public ChecksumBase<CRC32>
{
public:
    friend class ChecksumBase<CRC32>;
    using value_type = uint32_t;

private:
    void update_impl(const uint8_t* ptr, size_t len)
    {
        m_sum = crc32(m_sum, ptr, len);
    }

    void finalize_impl()
    {
    }

    void reset_impl()
    {
        m_sum = 0;
    }

    auto value_impl() const
    {
        return m_sum;
    }

    value_type m_sum {0};       /**< Final checksum */
};

/**
 * 128-bit xxHash checksum
 */
class xxHash final : public ChecksumBase<xxHash>
{
public:
    friend class ChecksumBase<xxHash>;
    using value_type = std::array<uint8_t, sizeof(XXH128_hash_t)>;

    xxHash()
    {
        reset();
    }

private:
    void update_impl(const uint8_t* ptr, size_t len)
    {
        XXH3_128bits_update(&m_state, ptr, len);
    }

    void finalize_impl()
    {
        static_assert(sizeof(decltype(XXH3_128bits_digest(&m_state))) == sizeof(m_sum));
        const auto val = XXH3_128bits_digest(&m_state);
        memcpy(m_sum.data(), &val, m_sum.size());
    }

    void reset_impl()
    {
        XXH3_128bits_reset(&m_state);
        memset(&m_sum, 0, sizeof(m_sum));
    }

    auto value_impl() const
    {
        return m_sum;
    }

    XXH3_state_t m_state;
    value_type   m_sum;
};

struct xxHasher
{
    template<class T>
    size_t operator()(T&& t) const
    {
        return XXH3_64bits(t.data(), t.size());
    }
};

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
}
