/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/assert.hh>

#include <array>
#include <algorithm>
#include <limits>
#include <vector>
#include <variant>

namespace maxbase
{

// Calculates the number of values that can be stored "for free" in a mxb::small_vector. This is the number of
// elements that fit into the same space that the heap portion uses. Usually this is around 23 bytes of
// storage. Values larger than the "free space" return 1.
template<class T>
constexpr uint8_t ideal_small_vector_size()
{
    size_t ideal_size = (sizeof(std::vector<T>) - sizeof(uint8_t)) / sizeof(T);
    return std::clamp(ideal_size, 1UL, (size_t)std::numeric_limits<uint8_t>::max());
}

static_assert(ideal_small_vector_size<uint8_t>() == 23);
static_assert(ideal_small_vector_size<uint16_t>() == 11);
static_assert(ideal_small_vector_size<uint32_t>() == 5);
static_assert(ideal_small_vector_size<uint64_t>() == 2);
static_assert(ideal_small_vector_size<std::array<uint64_t, 4>>() == 1);

// A very simple implementation of a small vector, similar to the ones found in LLVM and Boost (InnoDB
// supposedly has one too). It allocates some internal storage but moves onto heap storage if the internal one
// runs out. The container is primarily designed for use as a temporary sort buffer and it doesn't switch back
// to the internal storage unless clear() is called or the container goes empty as a result of a erase() call.
//
// The implementation uses a std::variant to share the storage used by the heap and the internal storage
// implementations. This makes it possible to store some values "for free" without having to allocate separate
// space for them. The container might store more internal values than is requested if it does not cause
// the size of the object to grow.
template<class T, uint8_t Capacity = 1>
class small_vector
{
public:
    static_assert(Capacity > 0, "Capacity must be positive");

    using size_type = size_t;
    using difference_type = std::ptrdiff_t;
    using const_reference = const T&;
    using const_iterator = const T*;
    using Big = std::vector<T>;

    static constexpr uint8_t REAL_CAPACITY = std::max(Capacity, ideal_small_vector_size<T>());

    struct Small
    {
        std::array<T, REAL_CAPACITY> data;
        uint8_t                      size {0};
    };

    void push_back(T t)
    {
        if (is_small())
        {
            Small& stack = std::get<Small>(m_storage);

            if (stack.size == stack.data.size())
            {
                Big heap;
                heap.reserve(stack.size + 1);
                heap.assign(stack.data.begin(), stack.data.end());
                heap.push_back(std::move(t));
                m_storage = std::move(heap);
            }
            else
            {
                mxb_assert(stack.size < stack.data.size());
                stack.data[stack.size++] = std::move(t);
            }
        }
        else
        {
            std::get<Big>(m_storage).push_back(std::move(t));
        }
    }

    void erase(const_iterator it)
    {
        mxb_assert(it != end());
        auto offset = std::distance(begin(), it);

        if (is_small())
        {
            Small& stack = std::get<Small>(m_storage);
            std::copy(stack.data.begin() + offset + 1,
                      stack.data.begin() + stack.size,
                      stack.data.begin() + offset);
            --stack.size;
        }
        else
        {
            Big& heap = std::get<Big>(m_storage);
            heap.erase(heap.begin() + offset);

            if (heap.empty())
            {
                m_storage = Small{};
            }
        }
    }

    const_iterator begin() const
    {
        return is_small() ? std::get<Small>(m_storage).data.data() :  std::get<Big>(m_storage).data();
    }

    const_iterator end() const
    {
        if (is_small())
        {
            const Small& stack = std::get<Small>(m_storage);
            return stack.data.data() + stack.size;
        }
        else
        {
            const Big& heap = std::get<Big>(m_storage);
            return heap.data() + heap.size();
        }
    }

    const_reference front() const
    {
        return is_small() ? std::get<Small>(m_storage).data.front() : std::get<Big>(m_storage).front();
    }

    const_reference back() const
    {
        if (is_small())
        {
            const Small& stack = std::get<Small>(m_storage);
            return stack.data[stack.size - 1];
        }
        else
        {
            return std::get<Big>(m_storage).back();
        }
    }

    const_reference operator[](size_t i) const
    {
        return *(begin() + i);
    }

    size_type size() const
    {
        return is_small() ? std::get<Small>(m_storage).size : std::get<Big>(m_storage).size();
    }

    bool empty() const
    {
        return is_small() ? std::get<Small>(m_storage).size == 0 : std::get<Big>(m_storage).empty();
    }

    void clear()
    {
        m_storage = Small{};
    }

private:
    bool is_small() const
    {
        return std::holds_alternative<Small>(m_storage);
    }

    std::variant<Small, Big> m_storage{Small{}};
};
}
