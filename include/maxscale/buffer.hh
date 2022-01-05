/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <algorithm>
#include <iterator>
#include <memory>
#include <cstdint>
#include <cstring>
#include <vector>
#include <maxbase/assert.h>
#include <maxscale/hint.hh>
#include <maxsimd/canonical.hh>

class SERVER;

namespace maxscale
{
class Buffer;
}

enum gwbuf_type_t
{
    GWBUF_TYPE_UNDEFINED      = 0,
    GWBUF_TYPE_COLLECT_RESULT = (1 << 0),
    GWBUF_TYPE_REPLAYED       = (1 << 1),
    GWBUF_TYPE_TRACK_STATE    = (1 << 2),
    GWBUF_TYPE_COLLECT_ROWS   = (1 << 3),
};

/**
 * A structure for adding arbitrary data to a buffer.
 */
struct BufferObject
{
    void* data = nullptr;
    void  (* deleter)(void*) = nullptr;

    ~BufferObject();
};

/**
 * A structure to encapsulate the data in a form that the data itself can be
 * shared between multiple GWBUF's without the need to make multiple copies
 * but still maintain separate data pointers.
 */
class SHARED_BUF
{
public:
    explicit SHARED_BUF(size_t len)
        : data(len)
    {
    }
    BufferObject         classifier_data;       /**< Parsing info */
    std::vector<uint8_t> data;                  /**< Actual memory that was allocated */
};

/**
 * The buffer structure used by the descriptor control blocks.
 *
 * Linked lists of buffers are created as data is read from a descriptor
 * or written to a descriptor. The use of linked lists of buffers with
 * flexible data pointers is designed to minimise the need for data to
 * be copied within the gateway.
 */
class GWBUF
{
public:
    using HintVector = std::vector<Hint>;

    uint8_t* start {nullptr};   /*< Start of the valid data */
    uint8_t* end {nullptr};     /*< First byte after the valid data */

    HintVector hints;                               /*< Hint data for this buffer */
    uint32_t   gwbuf_type {GWBUF_TYPE_UNDEFINED};   /*< buffer's data type information */
    uint32_t   id {0};                              /*< Unique ID for this buffer, 0 if no ID
                                                     * is assigned */
#ifdef SS_DEBUG
    int owner {-1};     /*< Owner of the thread, only for debugging */
#endif

    const std::string& get_sql() const;
    const std::string& get_canonical() const;

    /**
     * Constructs an empty GWBUF. Does not allocate any storage. Calling most storage-accessing functions
     * on an empty buffer is an error.
     */
    GWBUF();

    explicit GWBUF(uint64_t size);

    GWBUF(GWBUF&& rhs) noexcept;
    GWBUF& operator=(GWBUF&& rhs) noexcept;

    // No copy-ctor, as it is not intuitively clear whether it should deep or shallow clone. Separate
    // functions keep things clear.
    GWBUF(GWBUF& rhs) = delete;

    GWBUF clone_shallow() const;
    GWBUF clone_deep() const;

    /**
     * Set classifier data. Can only be set once.
     *
     * @param new_data Data of the object
     * @param deleter Deleter function
     */
    void set_classifier_data(void* new_data, void (* deleter)(void*));

    /**
     * Get classifier data.
     *
     * @return Data or null
     */
    void* get_classifier_data() const;

    const uint8_t* data() const;
    uint8_t*       data();
    size_t         length() const;
    bool           empty() const;

    /**
     * Append bytes to buffer, starting at the end pointer. May invalidate start and end pointers.
     * If underlying data is shared, will clone it.
     *
     * @param new_data Pointer to data. The bytes will be copied.
     * @param n_bytes Number of bytes to copy
     */
    void append(const uint8_t* new_data, uint64_t n_bytes);

    /**
     * Append contents of the buffer given as parameter to this buffer.
     *
     * @param buffer Buffer to copy from. The object is left untouched.
     */
    void append(GWBUF* buffer);

    /**
     * Moves the start-pointer forward.
     *
     * @param bytes Number of bytes to consume
     * @return New start pointer
     */
    uint8_t* consume(uint64_t bytes);

    /**
     * Moves the end-pointer backward.
     *
     * @param bytes Number of bytes to trim
     */
    void rtrim(uint64_t bytes);

private:
    std::shared_ptr<SHARED_BUF> m_sbuf;     /*< The shared buffer with the real data */

    mutable std::string      m_sql;
    mutable std::string      m_canonical;
    mutable maxsimd::Markers m_markers;

    void move_helper(GWBUF&& other) noexcept;
    void clone_helper(const GWBUF& other);
};

inline bool gwbuf_is_type_undefined(const GWBUF* b)
{
    return b->gwbuf_type == 0;
}

inline bool gwbuf_should_collect_result(const GWBUF* b)
{
    return b->gwbuf_type & GWBUF_TYPE_COLLECT_RESULT;
}

inline bool gwbuf_should_collect_rows(const GWBUF* b)
{
    return b->gwbuf_type & GWBUF_TYPE_COLLECT_ROWS;
}

// True if the query is not initiated by the client but an internal replaying mechanism
inline bool gwbuf_is_replayed(const GWBUF* b)
{
    return b->gwbuf_type & GWBUF_TYPE_REPLAYED;
}

// Track session state change response
inline bool gwbuf_should_track_state(const GWBUF* b)
{
    return b->gwbuf_type & GWBUF_TYPE_TRACK_STATE;
}

inline bool gwbuf_is_parsed(const GWBUF* b)
{
    return b->get_classifier_data() != nullptr;
}

inline uint8_t* GWBUF::data()
{
    return start;
}

inline const uint8_t* GWBUF::data() const
{
    return start;
}

/*< First valid, unconsumed byte in the buffer */
inline uint8_t* gwbuf_link_data(GWBUF* b)
{
    return b->data();
}

inline const uint8_t* gwbuf_link_data(const GWBUF* b)
{
    return b->data();
}

inline uint8_t* GWBUF_DATA(GWBUF* b)
{
    return b->data();
}

inline const uint8_t* GWBUF_DATA(const GWBUF* b)
{
    return b->data();
}

inline size_t GWBUF::length() const
{
    return end - start;
}

inline bool GWBUF::empty() const
{
    return start == end;
}

/*< Number of bytes in the individual buffer */
inline size_t gwbuf_link_length(const GWBUF* b)
{
    return b->length();
}

/*< Check whether the buffer is contiguous*/
inline bool gwbuf_is_contiguous(const GWBUF* b)
{
    mxb_assert(b);
    return true;
}

/*< True if all bytes in the buffer have been consumed */
inline bool gwbuf_link_empty(const GWBUF* b)
{
    return b->start >= b->end;
}

inline bool GWBUF_EMPTY(const GWBUF* b)
{
    return gwbuf_link_empty(b);
}

inline void gwbuf_link_rtrim(GWBUF* b, unsigned int bytes)
{
    b->rtrim(bytes);
}

inline void GWBUF_RTRIM(GWBUF* b, unsigned int bytes)
{
    gwbuf_link_rtrim(b, bytes);
}

/**
 * Allocate a new gateway buffer of specified size.
 *
 * @param size  The size in bytes of the data area required
 *
 * @return Pointer to the buffer structure or NULL if memory could not
 *         be allocated.
 */
extern GWBUF* gwbuf_alloc(unsigned int size);

/**
 * Allocate a new gateway buffer structure of specified size and load with data.
 *
 * @param size  The size in bytes of the data area required
 * @param data  Pointer to the data (size bytes) to be loaded
 *
 * @return Pointer to the buffer structure or NULL if memory could not
 *         be allocated.
 */
extern GWBUF* gwbuf_alloc_and_load(unsigned int size, const void* data);

/**
 * Free a chain of gateway buffers
 *
 * @param buf  The head of the list of buffers to free, can be NULL.
 */
extern void gwbuf_free(GWBUF* buf);

/**
 * Clone a GWBUF. Note that if the GWBUF is actually a list of
 * GWBUFs, then every GWBUF in the list will be cloned. Note that but
 * for the GWBUF structure itself, the data is shared.
 *
 * @param buf  The GWBUF to be cloned.
 *
 * @return The cloned GWBUF, or NULL if any part of @buf could not be cloned.
 */
GWBUF* gwbuf_clone_shallow(GWBUF* buf);

/**
 * @brief Deep clone a GWBUF
 *
 * Clone the data inside a GWBUF into a new buffer. The created buffer has its
 * own internal buffer and any modifications to the deep cloned buffer will not
 * reflect on the original one. Any buffer objects attached to the original buffer
 * will not be copied. Only the buffer type of the original buffer will be copied
 * over to the cloned buffer.
 *
 * @param buf Buffer to clone
 *
 * @return Deep copy of @c buf or NULL on error
 */
extern GWBUF* gwbuf_deep_clone(const GWBUF* buf);

/**
 * Compare two GWBUFs. Two GWBUFs are considered identical if their
 * content is identical, irrespective of whether one is segmented and
 * the other is not.
 *
 * @param lhs  One GWBUF
 * @param rhs  Another GWBUF
 *
 * @return  0 if the content is identical,
 *         -1 if @c lhs is less than @c rhs, and
 *          1 if @c lhs is more than @c rhs.
 *
 * @attention A shorter @c GWBUF less than a longer one. Otherwise the
 *            sign of the return value is determined by the sign of the
 *            difference between the first pair of bytes (interpreted as
 *            unsigned char) that differ in lhs and rhs.
 */
extern int gwbuf_compare(const GWBUF* lhs, const GWBUF* rhs);

/**
 * Append a buffer onto a linked list of buffer structures.
 *
 * @param head  The current head of the linked list or NULL.
 * @param tail  Another buffer to make the tail of the linked list, must not be NULL
 *
 * @return The new head of the linked list
 */
GWBUF* gwbuf_append(GWBUF* head, GWBUF* tail);

/**
 * @brief Consume data from buffer chain
 *
 * Data is consumed from @p head until either @p length bytes have been
 * processed or @p head is empty. If @p head points to a chain of buffers,
 * those buffers are counted as a part of @p head and will also be consumed if
 * @p length exceeds the size of the first buffer.
 *
 * @param head    The head of the linked list
 * @param length  Number of bytes to consume
 *
 * @return The head of the linked list or NULL if everything was consumed
 */
GWBUF* gwbuf_consume(GWBUF* head, uint64_t length);

/**
 * Trim bytes from the end of a GWBUF structure that may be the first
 * in a list. If the buffer has n_bytes or less then it will be freed and
 * the next buffer in the list will be returned, or if none, NULL.
 *
 * @param head     The buffer to trim
 * @param n_bytes  The number of bytes to trim off
 *
 * @return The buffer chain or NULL if buffer chain now empty
 */
extern GWBUF* gwbuf_rtrim(GWBUF* head, uint64_t length);

/**
 * Return the number of bytes of data in the linked list.
 *
 * @param head  The current head of the linked list
 *
 * @return The number of bytes of data in the linked list
 */
extern unsigned int gwbuf_length(const GWBUF* head);

/**
 * @brief Copy bytes from a buffer
 *
 * Copy bytes from a chain of buffers. Supports copying data from buffers where
 * the data is spread across multiple buffers.
 *
 * @param buffer  Buffer to copy from
 * @param offset  Offset into the buffer
 * @param bytes   Number of bytes to copy
 * @param dest    Destination where the bytes are copied
 *
 * @return Number of bytes copied.
 */
extern size_t gwbuf_copy_data(const GWBUF* buffer, size_t offset, size_t bytes, uint8_t* dest);

/**
 * @brief Split a buffer in two
 *
 * The returned value will be @c length bytes long. If the length of @c buf
 * exceeds @c length, the remaining buffers are stored in @buf.
 *
 * @param buf Buffer chain to split
 * @param length Number of bytes that the returned buffer should contain
 *
 * @return Head of the buffer chain.
 */
extern GWBUF* gwbuf_split(GWBUF** buf, size_t length);

/**
 * Set given type to all buffers on the list.
 * *
 * @param buf   The shared buffer
 * @param type  Type to be added, mask of @c gwbuf_type_t values.
 */
extern void gwbuf_set_type(GWBUF* head, uint32_t type);

/**
 * Convert a chain of GWBUF structures into a single GWBUF structure
 *
 * @param orig  The chain to convert, must not be used after the function call
 *
 * @return A contiguous version of @c buf.
 *
 * @attention Never returns NULL, memory allocation failures abort the process
 */
extern GWBUF* gwbuf_make_contiguous(GWBUF* buf);

#if defined (BUFFER_TRACE)
extern void dprintAllBuffers(void* pdcb);
#endif

/**
 * Assign an ID for this buffer
 *
 * @param buffer The buffer to modify
 * @param id     The ID to set, must be a non-zero value
 */
void gwbuf_set_id(GWBUF* buffer, uint32_t id);

/**
 * Get buffer ID
 *
 * @param buffer The buffer to inspect
 *
 * @return The ID of the buffer or 0 if no ID is assigned
 */
uint32_t gwbuf_get_id(GWBUF* buffer);

/**
 * Debug function for dumping buffer contents to log
 *
 * @see mxs::Buffer::hexdump
 *
 * @param buffer    Buffer to dump
 * @param log_level Log priority where the message is written
 */
void gwbuf_hexdump(GWBUF* buffer, int log_level = LOG_INFO);

/**
 * Debug function for pretty-printing buffer contents to log
 *
 * @see mxs::Buffer::hexdump_pretty
 *
 * @param buffer    Buffer to dump
 * @param log_level Log priority where the message is written
 */
void gwbuf_hexdump_pretty(GWBUF* buffer, int log_level = LOG_INFO);

/**
 * Return pointer of the byte at offset from start of chained buffer
 * Warning: It not guaranteed to point to a contiguous segment of memory,
 * it is only safe to modify the first byte this pointer point to.
 *
 * @param buffer  one or more chained buffer
 * @param offset  Offset into the buffer
 * @return  if total buffer length is bigger than offset then return
 *      the offset byte pointer, otherwise return null
 */
extern uint8_t* gwbuf_byte_pointer(GWBUF* buffer, size_t offset);

#ifdef SS_DEBUG
/**
 * Set the owner of the GWBUF.
 *
 * @param int  An integer identifying the owner.
 */
inline void gwbuf_set_owner(GWBUF* buf, int owner)
{
    buf->owner = owner;
}
#endif

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
    class iterator_base : public std::iterator<
                            std::forward_iterator_tag   // The type of the iterator
                            , uint8_t                   // The type of the elems
                            , std::ptrdiff_t            // Difference between two its
                            , pointer_type              // The type of pointer to an elem
                            , reference_type>           // The reference type of an elem
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

        /**
         * Advance the iterator
         *
         * This provides similar behavior to random access iterators with operator+= but does it in
         * non-constant time.
         *
         * @param i Number of steps to advance the iterator
         */
        void advance(int i)
        {
            mxb_assert(m_i != m_end || i == 0);
            mxb_assert(i >= 0);

            while (m_i && m_i + i >= m_end)
            {
                i -= m_end - m_i;
                m_pBuffer = nullptr;

                m_i = nullptr;
                m_end = nullptr;
            }

            if (m_i)
            {
                m_i += i;
            }
        }

    protected:
        iterator_base(buf_type pBuffer = NULL)
            : m_pBuffer(pBuffer)
            , m_i(m_pBuffer ? GWBUF_DATA(m_pBuffer) : NULL)
            , m_end(m_pBuffer ? (m_i + gwbuf_link_length(m_pBuffer)) : NULL)
        {
        }

        void advance()
        {
            mxb_assert(m_i != m_end);

            ++m_i;

            if (m_i == m_end)
            {
                m_pBuffer = nullptr;
                m_i = nullptr;
                m_end = nullptr;
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
        {
        }

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

        bool operator==(const iterator& rhs) const
        {
            return eq(rhs);
        }

        bool operator!=(const iterator& rhs) const
        {
            return neq(rhs);
        }

        reference operator*()
        {
            mxb_assert(m_i);
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
        {
        }

        const_iterator(const Buffer::iterator& rhs)
            : const_iterator_base_typedef(rhs.m_pBuffer)
        {
            m_i = rhs.m_i;
            m_end = rhs.m_end;
        }

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

        bool operator==(const const_iterator& rhs) const
        {
            return eq(rhs);
        }

        bool operator!=(const const_iterator& rhs) const
        {
            return neq(rhs);
        }

        reference operator*() const
        {
            mxb_assert(m_i);
            return *m_i;
        }
    };

    /**
     * Creates an empty buffer.
     */
    Buffer()
        : m_pBuffer(NULL)
    {
    }

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
            m_pBuffer = gwbuf_clone_shallow(rhs.m_pBuffer);

            if (!m_pBuffer)
            {
                mxb_assert(!true);
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
    {
        mxb_assert(pBuffer);
    }

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
    Buffer& operator=(const Buffer& rhs)
    {
        Buffer temp(rhs);
        swap(temp);
        return *this;
    }

#if __cplusplus >= 201103
    /**
     * Move assignment operator
     *
     * @param rhs  The @c Buffer to be moves.
     */
    Buffer& operator=(Buffer&& rhs)
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
        return copy_from(rhs.m_pBuffer);
    }

    /**
     * Clone a GWBUF and free the current buffer
     *
     * @param buf Buffer to clone
     *
     * @return True if buffer was copied
     *
     * @attention  Invalidates all iterators.
     */
    bool copy_from(GWBUF* pBuffer)
    {
        bool copied = true;

        if (pBuffer)
        {
            pBuffer = gwbuf_clone_shallow(pBuffer);

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
     * Appends a vector of bytes to this.
     *
     * @param buffer  The std::vector<uint8_t> to append
     *
     * @return this
     *
     * @attention  Does not invalidate any iterators, but an iterator
     *             that has reached the end will remain there.
     */
    Buffer& append(const std::vector<uint8_t>& data)
    {
        m_pBuffer = gwbuf_append(m_pBuffer, gwbuf_alloc_and_load(data.size(), data.data()));
        return *this;
    }

    iterator erase(const_iterator first, const_iterator last)
    {
        if (first == end())
        {
            // Nothing to do
            return end();
        }
        else if (first == last)
        {
            // Empty range deletion is a no-op that must return a non-const version of the given iterators
            iterator it = begin();
            it.advance(std::distance(const_iterator(it), first));
            mxb_assert(const_iterator(it) == first);
            return it;
        }
        else if (first == begin() && last == end())
        {
            // Clear out the whole buffer
            reset();
            return end();
        }

        iterator rval;
        const_iterator b = begin();
        auto offset = std::distance(b, first);
        auto num_bytes = std::distance(first, last);
        mxb_assert(num_bytes > 0);

        auto head = gwbuf_split(&m_pBuffer, offset);

        if (m_pBuffer && (m_pBuffer = gwbuf_consume(m_pBuffer, num_bytes)))
        {
            if (head)
            {
                m_pBuffer = gwbuf_append(head, m_pBuffer);
            }
            else
            {
                mxb_assert(offset == 0);
            }

            rval = begin();
            rval.advance(offset + 1);
        }
        else
        {
            m_pBuffer = head;
            rval = end();
        }

        return rval;
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

    const GWBUF* get() const
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
     * The total length of the buffer.
     *
     * @return The total length of the buffer.
     */
    size_t length() const
    {
        return m_pBuffer ? gwbuf_length(m_pBuffer) : 0;
    }

    /**
     * Whether the buffer is empty.
     *
     * @return True if the buffer is empty
     */
    bool empty() const
    {
        return m_pBuffer == nullptr;
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
        return m_pBuffer != NULL;
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
            mxb_assert(!true);
            throw std::bad_alloc();
        }
    }

    /**
     * Get pointer to internal data. The data can only be assumed contiguous if 'make_contiguous' has been
     * called.
     *
     * @return Pointer to internal data.
     */
    uint8_t* data();

    const uint8_t* data() const;

    /**
     * Set a buffer ID
     *
     * @param id ID to set
     */
    void set_id(uint32_t id)
    {
        gwbuf_set_id(m_pBuffer, id);
    }

    /**
     * Get the ID of this buffer
     *
     * @return The current ID or 0 if no ID is set
     */
    uint32_t id() const
    {
        return gwbuf_get_id(m_pBuffer);
    }

    /**
     * Get the sql of the buffer (if any)
     */
    const std::string& get_sql() const
    {
        return m_pBuffer->get_sql();
    }

    /**
     * Append a hint to the buffer
     *
     * @param args Arguments given to the Hint class constructor
     */
    template<typename ... Args>
    void add_hint(Args&& ... args)
    {
        m_pBuffer->hints.emplace_back(std::forward<Args>(args)...);
    }

    /**
     * Set the buffer type
     *
     * @param type The type to set
     */
    void set_type(gwbuf_type_t type)
    {
        gwbuf_set_type(m_pBuffer, type);
    }

    /**
     * Debug function for dumping buffer contents to log
     *
     * Prints contents as hexadecimal. Only the first 1024 bytes are dumped to avoid filling up the log.
     *
     * @param log_level Log priority where the message is written
     */
    void hexdump(int log_level = LOG_INFO) const;

    /**
     * Debug function for pretty-printing buffer contents to log
     *
     * The output format is similar to `hexdump -C` and provides both hex and human-readable values.
     *
     * @param log_level Log priority where the message is written
     */
    void hexdump_pretty(int log_level = LOG_INFO) const;

private:
    // To prevent @c Buffer from being created on the heap.
    void* operator new(size_t);         // standard new
    void* operator new(size_t, void*);  // placement new
    void* operator new[](size_t);       // array new
    void* operator new[](size_t, void*);// placement array new

private:
    GWBUF* m_pBuffer;
};

/**
 * Checks two @c Buffers for equality.
 *
 * @return True if equal, false otherwise.
 */
inline bool operator==(const Buffer& lhs, const Buffer& rhs)
{
    return lhs.eq(rhs);
}

/**
 * Checks a @c Buffer and a @c GWBUF for equality.
 *
 * @return True if equal, false otherwise.
 */
inline bool operator==(const Buffer& lhs, const GWBUF& rhs)
{
    return lhs.eq(rhs);
}

/**
 * Checks two @c Buffers for un-equality.
 *
 * @return True if un-equal, false otherwise.
 */
inline bool operator!=(const Buffer& lhs, const Buffer& rhs)
{
    return !lhs.eq(rhs);
}

/**
 * Checks a @c Buffer and a @c GWBUF for un-equality.
 *
 * @return True if un-equal, false otherwise.
 */
inline bool operator!=(const Buffer& lhs, const GWBUF& rhs)
{
    return !lhs.eq(rhs);
}
}
