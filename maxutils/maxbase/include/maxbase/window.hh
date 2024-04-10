/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/assert.hh>

#include <vector>

namespace maxbase
{
/**
 * Sliding window with a fixed size.
 *
 * This is essentially an append-only ring buffer. It uses a flag variable to detect the case when the ring
 * wraps around to the first element which is then used to calculate how many elements have been stored for
 * iteration.
 *
 * The usual implementations of ring buffers store two pointers to the data and waste one slot for the
 * past-the-end pointer or use a pointer-and-length approach that makes the insertion and iteration slightly
 * more complex. The implementation in the Window class is simpler as elements are never removed and thus only
 * the knowledge of whether the buffer has overflowed is needed.
 *
 * If the size of the buffer would always be a power of two, faster bitwise masking arithmetic could be used
 * instead of comparing it to the end pointer. However the practical performance difference to the boolean
 * flag approach seems to be minimal.
 */
template<class T>
class Window
{
public:
    struct const_iterator
    {
        using difference_type = typename std::vector<T>::difference_type;
        using value_type = typename std::vector<T>::value_type;
        using pointer = typename std::vector<T>::pointer;
        using reference = typename std::vector<T>::reference;
        using const_reference = typename std::vector<T>::const_reference;
        using iterator_category = std::input_iterator_tag;

        const_iterator(typename std::vector<T>::const_iterator pos, size_t size, const std::vector<T>& vec)
            : m_data(vec)
            , m_pos(pos)
            , m_size(size)
        {
        }

        bool operator==(const const_iterator& other) const
        {
            return m_pos == other.m_pos && m_size == other.m_size;
        }

        bool operator!=(const const_iterator& other) const
        {
            return !(*this == other);
        }

        const_reference operator*() const
        {
            return *m_pos;
        }

        const_reference operator->() const
        {
            return *m_pos;
        }

        const_iterator& operator++()
        {
            --m_size;

            if (++m_pos == m_data.end())
            {
                m_pos = m_data.begin();
            }

            return *this;
        }

        const_iterator operator++(int)
        {
            auto rv = *this;
            --m_size;

            if (++m_pos == m_data.end())
            {
                m_pos = m_data.begin();
            }

            return rv;
        }

        const std::vector<T>&                   m_data;
        typename std::vector<T>::const_iterator m_pos;
        size_t                                  m_size;
    };

    /**
     * Constructs a new window
     *
     * @param max_size How many elements to keep
     */
    Window(size_t max_size)
        : m_data(max_size)
        , m_pos(m_data.begin())
    {
    }

    /**
     * Move construct a window
     *
     * @param max_size How many elements to keep
     * @param other    The other window where values are moved from
     */
    Window(size_t max_size, Window&& other)
        : Window(max_size)
    {
        ptrdiff_t diff = (ptrdiff_t)other.size() - (ptrdiff_t)size();

        for (auto&& val : other)
        {
            if (diff > (ptrdiff_t)max_size)
            {
                --diff;
            }
            else
            {
                push(std::move(val));
            }
        }
    }

    /**
     * Push a value into the window
     *
     * If the window size is exceeded, older values are discarded from it.
     *
     * @param v Value to push
     */
    template<class V>
    void push(V&& v)
    {
        if (m_pos == m_data.end())
        {
            mxb_assert_message(m_data.size() == 0, "Window size should be 0");
            return;
        }

        *m_pos++ = std::forward<V>(v);

        if (m_pos == m_data.end())
        {
            m_full = true;
            m_pos = m_data.begin();
        }
    }

    const_iterator begin() const
    {
        return m_full ? const_iterator(m_pos, m_data.size(), m_data) :
               const_iterator(m_data.begin(), m_pos - m_data.begin(), m_data);
    }

    const_iterator end() const
    {
        return const_iterator(m_pos, 0, m_data);
    }

    /**
     * Check if the window is empty
     *
     * @return True if the window is empty
     */
    bool empty() const
    {
        return m_pos == m_data.begin() && !m_full;
    }

    /**
     * Clear the window
     */
    void clear()
    {
        m_pos = m_data.begin();
        m_full = false;
    }

    /**
     * Get the size of the window
     *
     * @return The size of the window
     */
    size_t size() const
    {
        return m_full ? capacity() : m_pos - m_data.begin();
    }

    /**
     * Get the capacity of the window
     *
     * @return The capacity of the window
     */
    size_t capacity() const
    {
        return m_data.size();
    }

private:
    std::vector<T>                    m_data;
    typename std::vector<T>::iterator m_pos;
    bool                              m_full {false};
};
}
