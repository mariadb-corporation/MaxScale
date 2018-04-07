#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <algorithm>
#include <iterator>
#include <new>
#include <vector>
#include <maxscale/buffer.h>

namespace maxscale
{

/**
 * @class Buffer
 *
 * @c Buffer is a simple wrapper around @ GWBUF that is more convenient to
 * use in a C++ context.
 *
 * As @c Buffer is a handle class, it should be created on the stack or as a
 * member of an enclosing class, *never* dynamically.
 *
 * @c Buffer exposed _forward_ iterators to the underlying buffer that can
 * be used in conjunction with standard C++ algorithms and functions.
 */
class Buffer
{
public:
    // buf_type      : The type of the buffer, either "GWBUF*" or "const GWBUF*"
    // pointer_type  : The type of a pointer to an element, either "uint8_t*" or "const uint8_t*".
    // reference_type: The type of a reference to an element, either "uint8_t&" or "const uint8_t&".
    template<class buf_type, class pointer_type, class reference_type>
    class iterator_base : public std::iterator <
        std::forward_iterator_tag, // The type of the iterator
        uint8_t,                   // The type of the elems
        std::ptrdiff_t,            // Difference between two its
        pointer_type,              // The type of pointer to an elem
        reference_type >           // The reference type of an elem
    {
    public:
        /**
         * Returns address of the internal pointer to a GWBUF.
         *
         * @attention This is provided as a backdoor for situations where it is
         *            unavoidable to access the interal pointer directly. It should
         *            carefully be assessed whether it actually can be avoided.
         *
         * @return Pointer to pointer to GWBUF.
         */
        pointer_type* address_of()
        {
            return &m_i;
        }

    protected:
        iterator_base(buf_type pBuffer = NULL)
            : m_pBuffer(pBuffer)
            , m_i(m_pBuffer ? GWBUF_DATA(m_pBuffer) : NULL)
            , m_end(m_pBuffer ? (m_i + GWBUF_LENGTH(m_pBuffer)) : NULL)
        {}

        void advance()
        {
            ss_dassert(m_i != m_end);

            ++m_i;

            if (m_i == m_end)
            {
                m_pBuffer = m_pBuffer->next;

                if (m_pBuffer)
                {
                    m_i = GWBUF_DATA(m_pBuffer);
                    m_end = m_i + GWBUF_LENGTH(m_pBuffer);
                }
                else
                {
                    m_i = NULL;
                    m_end = NULL;
                }
            }
        }

        bool eq(const iterator_base& rhs) const
        {
            return m_i == rhs.m_i;
        }

        bool neq(const iterator_base& rhs) const
        {
            return !eq(rhs);
        }

    protected:
        buf_type     m_pBuffer;
        pointer_type m_i;
        pointer_type m_end;
    };

    class const_iterator;

    // Buffer type, type of pointer to element and type of reference to element.
    typedef iterator_base<GWBUF*, uint8_t*, uint8_t&> iterator_base_typedef;

    /**
     * A forward iterator to Buffer.
     */
    class iterator : public iterator_base_typedef
    {
        friend class const_iterator;

    public:
        explicit iterator(GWBUF* pBuffer = NULL)
            : iterator_base_typedef(pBuffer)
        {}

        iterator& operator++()
        {
            advance();
            return *this;
        }

        iterator operator++(int)
        {
            iterator rv(*this);
            ++(*this);
            return rv;
        }

        bool operator == (const iterator& rhs) const
        {
            return eq(rhs);
        }

        bool operator != (const iterator& rhs) const
        {
            return neq(rhs);
        }

        reference operator*()
        {
            ss_dassert(m_i);
            return *m_i;
        }
    };

    // Buffer type, type of pointer to element and type of reference to element.
    typedef iterator_base<const GWBUF*, const uint8_t*, const uint8_t&> const_iterator_base_typedef;

    /**
     * A const forward iterator to Buffer.
     */
    class const_iterator : public const_iterator_base_typedef
    {
    public:
        explicit const_iterator(const GWBUF* pBuffer = NULL)
            : const_iterator_base_typedef(pBuffer)
        {}

        const_iterator(const Buffer::iterator& rhs)
            : const_iterator_base_typedef(rhs.m_pBuffer)
        {}

        const_iterator& operator++()
        {
            advance();
            return *this;
        }

        const_iterator operator++(int)
        {
            const_iterator rv(*this);
            ++(*this);
            return rv;
        }

        bool operator == (const const_iterator& rhs) const
        {
            return eq(rhs);
        }

        bool operator != (const const_iterator& rhs) const
        {
            return neq(rhs);
        }

        reference operator*() const
        {
            ss_dassert(m_i);
            return *m_i;
        }
    };

    /**
     * Creates an empty buffer.
     */
    Buffer()
        : m_pBuffer(NULL)
    {}

    /**
     * Copy constructor.
     *
     * @param rhs  The @c Buffer to copy.
     *
     * @throws @c std::bad_alloc if the underlying @c GWBUF cannot be cloned.
     *
     */
    Buffer(const Buffer& rhs)
    {
        if (rhs.m_pBuffer)
        {
            m_pBuffer = gwbuf_clone(rhs.m_pBuffer);

            if (!m_pBuffer)
            {
                ss_dassert(!true);
                throw std::bad_alloc();
            }
        }
        else
        {
            m_pBuffer = NULL;
        }
    }

#if __cplusplus >= 201103
    /**
     * Move constructor.
     *
     * @param rhs  The @c Buffer to be moved.
     */
    Buffer(Buffer&& rhs)
        : m_pBuffer(NULL)
    {
        swap(rhs);
    }
#endif

    /**
     * Creates a @Buffer from a @ GWBUF
     *
     * @param pBuffer  The buffer to create the @c Buffer from.
     *
     * @attention  The ownership of @c pBuffer is transferred to the
     *             @c Buffer being created.
     */
    Buffer(GWBUF* pBuffer)
        : m_pBuffer(pBuffer)
    {}

    /**
     * Creates a buffer of specified size.
     *
     * @param size  The size of the buffer.
     *
     * @attention  @c std::bad_alloc is thrown if the allocation fails.
     */
    Buffer(size_t size)
        : m_pBuffer(gwbuf_alloc(size))
    {
        if (!m_pBuffer)
        {
            throw std::bad_alloc();
        }
    }

    /**
     * Creates a buffer from existing data.
     *
     * @param pData  Pointer to data.
     * @param size   The size of the data.
     *
     * @attention  @c std::bad_alloc is thrown if the allocation fails.
     */
    Buffer(const void* pData, size_t size)
        : m_pBuffer(gwbuf_alloc_and_load(size, pData))
    {
        if (!m_pBuffer)
        {
            throw std::bad_alloc();
        }
    }

    /**
     * Creates a buffer from a std::vector.
     *
     * @param data  The data to be copied.
     *
     * @attention  @c std::bad_alloc is thrown if the allocation fails.
     */
    Buffer(const std::vector<uint8_t>& data)
        : m_pBuffer(gwbuf_alloc(data.size()))
    {
        if (m_pBuffer)
        {
            std::copy(data.begin(), data.end(), GWBUF_DATA(m_pBuffer));
        }
        else
        {
            throw std::bad_alloc();
        }
    }

    /**
     * Destructor
     */
    ~Buffer()
    {
        reset();
    }

    /**
     * Assignment operator
     *
     * @param rhs  The @c Buffer to be assigned to this.
     *
     * @return this
     *
     * @attention  The @c Buffer provided as argument will be copied, which
     *             may cause @c std::bad_alloc to be thrown.
     *
     * @attention  Does not invalidates iterators, but after the call, the iterators
     *             will refer to the data of the other @c Buffer.
     *
     * @see Buffer::copy_from
     */
    Buffer& operator = (Buffer rhs)
    {
        swap(rhs);
        return *this;
    }

#if __cplusplus >= 201103
    /**
     * Move assignment operator
     *
     * @param rhs  The @c Buffer to be moves.
     */
    Buffer& operator = (Buffer&& rhs)
    {
        reset();
        swap(rhs);
        return *this;
    }
#endif

    /**
     * Returns a forward iterator to the beginning of the Buffer.
     *
     * @return  A forward iterator.
     */
    iterator begin()
    {
        return iterator(m_pBuffer);
    }

    /**
     * Returns a forward iterator to the end of the Buffer.
     *
     * @return  A forward iterator.
     */
    iterator end()
    {
        return iterator();
    }

    /**
     * Returns a const forward iterator to the beginning of the Buffer.
     *
     * @return  A const forward iterator.
     */
    const_iterator begin() const
    {
        return const_iterator(m_pBuffer);
    }

    /**
     * Returns a const forward iterator to the end of the Buffer.
     *
     * @return  A const forward iterator.
     */
    const_iterator end() const
    {
        return const_iterator();
    }

    /**
     * Swap the contents with another @c Buffer
     *
     * @param buffer  The @c Buffer to swap contents with.
     */
    void swap(Buffer& buffer)
    {
        GWBUF* pBuffer = buffer.m_pBuffer;
        buffer.m_pBuffer = m_pBuffer;
        m_pBuffer = pBuffer;
    }

    /**
     * Clones the underlying @c GWBUF of the provided @c Buffer, and frees
     * the current buffer. Effectively an assignment operator that does
     * not throw.
     *
     * @param rhs  The @c Buffer to be copied.
     *
     * @return  True, if the buffer could be copied.
     *
     * @attention  Invalidates all iterators.
     *
     * @see Buffer::operator =
     */
    bool copy_from(const Buffer& rhs)
    {
        bool copied = true;
        GWBUF* pBuffer = rhs.m_pBuffer;

        if (pBuffer)
        {
            pBuffer = gwbuf_clone(pBuffer);

            if (!pBuffer)
            {
                copied = false;
            }
        }

        if (copied)
        {
            reset(pBuffer);
        }

        return copied;
    }

    /**
     * Compare content with another @ Buffer
     *
     * @param buffer  The buffer to compare with.
     *
     * @return  0 if identical,
     *         -1 if this less that @c buffer, and
     *         +1 if @c buffer less than this.
     */
    int compare(const Buffer& buffer) const
    {
        return gwbuf_compare(m_pBuffer, buffer.m_pBuffer);
    }

    /**
     * Compare content with a @c GWBUF
     *
     * @param buffer  The buffer to compare with.
     *
     * @return  0 if identical,
     *         -1 if this less that @c buffer, and
     *         +1 if @c buffer less than this.
     */
    int compare(const GWBUF& buffer) const
    {
        return gwbuf_compare(m_pBuffer, &buffer);
    }

    /**
     * Is content identical
     *
     * @param buffer  The buffer to compare with.
     *
     * @return True, if identical, otherwise false.
     */
    bool eq(const Buffer& buffer) const
    {
        return compare(buffer) == 0;
    }

    /**
     * Is content identical.
     *
     * @param pBuffer  The buffer to compare with.
     *
     * @return True, if identical, otherwise false.
     */
    bool eq(const GWBUF& buffer) const
    {
        return compare(buffer) == 0;
    }

    /**
     * Appends a @GWBUF to this.
     *
     * @param pBuffer  The buffer to be appended to this @c Buffer. Becomes
     *                 the property of the buffer.
     *
     * @return this
     *
     * @attention  Does not invalidate any iterators, but an iterator
     *             that has reached the end will remain there.
     */
    Buffer& append(GWBUF* pBuffer)
    {
        m_pBuffer = gwbuf_append(m_pBuffer, pBuffer);
        return *this;
    }

    /**
     * Appends a @Buffer to this.
     *
     * @param buffer  The buffer to be appended to this Buffer.
     *
     * @return this
     *
     * @attention After the call, the @c Buffer provided as argument
     *            will be empty.
     *
     * @attention  Does not invalidate any iterators, but an iterator
     *             that has reached the end will remain there.
     */
    Buffer& append(Buffer& buffer)
    {
        m_pBuffer = gwbuf_append(m_pBuffer, buffer.release());
        return *this;
    }

    /**
     * Get the underlying GWBUF.
     *
     * @return  The underlying @c GWBUF.
     *
     * @attention This does not release ownership of the buffer. The returned pointer must never be
     *            freed by the caller.
     */
    GWBUF* get()
    {
        return m_pBuffer;
    }

    /**
     * Resets the underlying GWBUF.
     *
     * @param pBuffer  The @c GWBUF the @c Buffer should be reset with.
     *
     * @attention  The ownership of @c pBuffer is moved to the @c Buffer.
     *
     * @attention  Invalidates all iterators.
     */
    void reset(GWBUF* pBuffer = NULL)
    {
        gwbuf_free(m_pBuffer);
        m_pBuffer = pBuffer;
    }

    /**
     * Releases the underlying GWBUF.
     *
     * @return  The underlying @c GWBUF.
     *
     * @attention  The ownership of the buffer is transferred to the caller.
     *
     * @attention  Does not invalidate existing iterators, but any manipulation
     *             of the returned @c GWBUF may invalidate them.
     */
    GWBUF* release()
    {
        GWBUF* pBuffer = m_pBuffer;
        m_pBuffer = NULL;
        return pBuffer;
    }

    /**
     * Returns the address of the underlying @ GWBUF. This is intended to only
     * be used in a context where a function returns a @c GWBUF as an out argument.
     * For instance:
     *
     *    void get_gwbuf(GWBUF** ppBuffer);
     *    ...
     *    Buffer buffer;
     *
     *    get_gwbuf(&buffer);
     *
     * @return  The address of the internal @c GWBUF pointer.
     *
     * @attention If the @c Buffer already refers to a @c GWBUF, that underlying
     *            buffer will first be freed.
     *
     * @attention Invalidates all iterators.
     */
    GWBUF** operator & ()
    {
        reset();
        return &m_pBuffer;
    }

    /**
     * The total length of the buffer.
     *
     * @return The total length of the buffer.
     */
    size_t length() const
    {
        return gwbuf_length(m_pBuffer);
    }

    /**
     * Whether the buffer is contiguous.
     *
     * @return  True, if the buffer is contiguous.
     */
    bool is_contiguous() const
    {
        return GWBUF_IS_CONTIGUOUS(m_pBuffer);
    }

    /**
     * Make the buffer contiguous.
     *
     * @return  True, if the buffer could be made contiguous.
     *
     * @attention  Invalidates all iterators.
     */
    bool make_contiguous(std::nothrow_t)
    {
        GWBUF* pBuffer = gwbuf_make_contiguous(m_pBuffer);

        if (pBuffer)
        {
            m_pBuffer = pBuffer;
        }

        return pBuffer != NULL;
    }

    /**
     * Make the buffer contiguous.
     *
     * @throws  @c std::bad_alloc if an allocation failed.
     *
     * @attention  Invalidates all iterators.
     */
    void make_contiguous()
    {
        if (!make_contiguous(std::nothrow))
        {
            ss_dassert(!true);
            throw std::bad_alloc();
        }
    }

private:
    // To prevent @c Buffer from being created on the heap.
    void* operator new(size_t);          // standard new
    void* operator new(size_t, void*);   // placement new
    void* operator new[](size_t);        // array new
    void* operator new[](size_t, void*); // placement array new

private:
    GWBUF* m_pBuffer;
};

/**
 * Checks two @c Buffers for equality.
 *
 * @return True if equal, false otherwise.
 */
inline bool operator == (const Buffer& lhs, const Buffer& rhs)
{
    return lhs.eq(rhs);
}

/**
 * Checks a @c Buffer and a @c GWBUF for equality.
 *
 * @return True if equal, false otherwise.
 */
inline bool operator == (const Buffer& lhs, const GWBUF& rhs)
{
    return lhs.eq(rhs);
}

/**
 * Checks two @c Buffers for un-equality.
 *
 * @return True if un-equal, false otherwise.
 */
inline bool operator != (const Buffer& lhs, const Buffer& rhs)
{
    return !lhs.eq(rhs);
}

/**
 * Checks a @c Buffer and a @c GWBUF for un-equality.
 *
 * @return True if un-equal, false otherwise.
 */
inline bool operator != (const Buffer& lhs, const GWBUF& rhs)
{
    return !lhs.eq(rhs);
}

}
