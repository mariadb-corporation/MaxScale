/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
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
#include <maxbase/assert.hh>
#include <maxsimd/canonical.hh>
#include <maxscale/hint.hh>

class SERVER;

namespace maxscale
{
class Buffer;
}

namespace maxbase
{
class Worker;
}

namespace mxb = maxbase;

/**
 * A structure to encapsulate the data in a form that the data itself can be
 * shared between multiple GWBUF's without the need to make multiple copies
 * but still maintain separate data pointers.
 */
class SHARED_BUF
{
public:
    explicit SHARED_BUF(size_t len);

    size_t size() const
    {
        return buf_end - buf_start.get();
    }

    std::unique_ptr<uint8_t[]> buf_start;           /**< Actual memory that was allocated */
    uint8_t*                   buf_end {nullptr};   /**< Past end pointer of allocated memory */
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
    class ProtocolInfo
    {
    public:
        virtual ~ProtocolInfo() = default;
        virtual size_t size() const = 0;
    };

    enum Type : uint32_t
    {
        TYPE_UNDEFINED      = 0,
        TYPE_COLLECT_RESULT = (1 << 0),
        TYPE_REPLAYED       = (1 << 1),

        // This causes the current resultset rows to be collected into mxs::Reply. They can be accessed
        // using mxs::Reply::row_data() inside the clientReply function and they are only available for
        // the duration of the function call. The rows should be considered a read-only view into
        // the buffer that contains them.
        TYPE_COLLECT_ROWS = (1 << 2),
    };

    using HintVector = std::vector<Hint>;

    using iterator = uint8_t*;
    using const_iterator = const uint8_t*;

    // TODO: make private?
    HintVector hints;   /*< Hint data for this buffer */

    /**
     * Constructs an empty GWBUF. Does not allocate any storage. Calling most storage-accessing functions
     * on an empty buffer is an error.
     */
    GWBUF();

    /**
     * Create an uninitialized buffer.
     *
     * The buffer allocates `size` bytes of storage to which data can be written. The newly constructed buffer
     * will not be empty but the data allocated for it will be uninitialized. The caller must make sure that
     * the allocated data is correctly initialized.
     *
     * @param size Allocated size of the underlying buffer
     */
    explicit GWBUF(size_t size);

    /**
     * Create a buffer with the given data.
     *
     * @param data Pointer to data. The contents are copied.
     * @param datasize Size of the data
     */
    explicit GWBUF(const uint8_t* data, size_t datasize);

    GWBUF(GWBUF&& rhs) noexcept;

    /**
     * Move-assignment operator
     *
     * A move from a GWBUF guarantees that the moved-from object will be empty if the move took place.
     * However, this still means that in order to bring the moved-from variable into a known state after a
     * call to std::move, a call to clear() must be made.
     *
     * @param rhs prvalue to assign
     *
     * @return *this
     */
    GWBUF& operator=(GWBUF&& rhs) noexcept;

    /**
     * Shallow-clones the source buffer. In general, should not be used to create long-term copies as this
     * prevents freeing the underlying data. GWBUFs traveling along the routing chain are best
     * recycled when written to a socket. Any existing shallow copies prevent this from happening.
     */
    GWBUF shallow_clone() const;

    /**
     * Deep-clones the source buffer. Only allocates minimal capacity. Is best used when the GWBUF is
     * stored for later use.
     *
     * @return Cloned buffer
     */
    GWBUF deep_clone() const;

    /**
     * Set out-of-band protocol information associated with the buffer.
     * Should only be set by the client or backend protocol.
     *
     * @param new_info  Out of band protocol information of the buffer.
     */
    void set_protocol_info(std::shared_ptr<ProtocolInfo> new_info);

    /**
     * Get out-of-band protocol information associated with the buffer.
     *
     * @return Information or null.
     */
    const std::shared_ptr<ProtocolInfo>& get_protocol_info() const;

    iterator       begin();
    iterator       end();
    const_iterator begin() const;
    const_iterator end() const;
    const uint8_t* data() const;
    uint8_t*       data();
    size_t         length() const;
    bool           empty() const;

    /**
     * @return True if buffer is not empty
     */
    explicit operator bool() const;

    // Sets the buffer type
    void set_type(Type type);

    // Checks if the buffer is of the given type
    bool type_is_undefined() const;
    bool type_is_replayed() const;
    bool type_is_collect_result() const;
    bool type_is_collect_rows() const;

    /**
     * Capacity of underlying shared buffer.
     *
     * @return Internal buffer capacity
     */
    size_t capacity() const;

    /**
     * Copy data from buffer. Data is not consumed. If the buffer does not have enough data to fulfill the
     * copy, copies less than requested.
     *
     * @param offset Copy start
     * @param n_bytes Number of bytes to copy
     * @param dst Destination buffer
     * @return How many bytes were copied
     */
    size_t copy_data(size_t offset, size_t n_bytes, uint8_t* dst) const;

    /**
     * Prepare the buffer for writing. May reserve more space if needed. write_complete should be called
     * once the write is ready.
     *
     * @param bytes How many bytes may be written
     * @return Pointer to the start of the write and the space available
     */
    std::tuple<uint8_t*, size_t> prepare_to_write(size_t n_bytes);

    /**
     * Tell the buffer that the write is complete. Advances the end pointer. Writing more than
     * there is space for is an error.
     *
     * @param n_bytes How much to advance the pointer
     */
    void write_complete(size_t n_bytes);

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
     * @param buffer Buffer to copy from
     */
    void append(const GWBUF& buffer);

    /**
     * Append to front of current data.
     *
     * @param buffer Source buffer
     */
    void merge_front(GWBUF&& buffer);

    /**
     * Append to back of current data.
     *
     * @param buffer Source buffer
     */
    void merge_back(GWBUF&& buffer);

    /**
     * Split a part from the front of the current buffer. The splitted part is returned, rest remains in
     * the buffer.
     *
     * @param n_bytes Bytes to split
     * @return Splitted bytes
     */
    GWBUF split(uint64_t n_bytes);

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

    /**
     * Clears the buffer. Releases any internal data.
     */
    void clear();

    /**
     * Clears the buffer without releasing the internal buffer. Resets start and end pointers so buffer
     * has no data.
     */
    void reset();

    /**
     * Ensure the underlying data is uniquely owned by this GWBUF. If not, will clone the data. This should
     * be called before writing manually to the internal buffer.
     */
    void ensure_unique();

    /**
     * Is the buffer unique? Returns true if internal data exists and is not shared with other GWBUFs.
     *
     * @return True if unique
     */
    bool is_unique() const;

    /**
     * Compare contents of two buffers. Returns 0 if buffers are equal length and have same contents.
     * Returns negative if this buffer is shorter or the first byte that differs is smaller. Otherwise
     * returns a positive number.
     *
     * @param rhs Buffer to compare to
     * @return Comparison result
     */
    int compare(const GWBUF& rhs) const;

    /**
     * Set the buffer ID
     *
     * The buffer ID is used to logically label the contents of a buffer so that they can later on be referred
     * to by it. Currently only used by session commands in the MariaDB protocol.
     *
     * @param new_id The ID to set
     */
    void set_id(uint32_t new_id);

    /**
     * Get the buffer ID, if iset
     *
     * @return The ID if set, otherwise 0
     */
    uint32_t id() const;

#ifdef SS_DEBUG
    void set_owner(mxb::Worker* owner);
#endif

    /**
     * Access byte at the given offset
     *
     * @param ind Offset into the buffer
     *
     * @return Reference to the byte
     */
    const uint8_t& operator[](size_t ind) const;

    /**
     * Returns the current size of the varying part of the instance.
     *
     * @return The varying size.
     */
    size_t varying_size() const;

    /**
     * Returns the runtime size of the instance; i.e. the static size + the varying size.
     *
     * @return The runtime size.
     */
    size_t runtime_size() const;

private:
    std::shared_ptr<SHARED_BUF>   m_sbuf;          /**< The shared buffer with the real data */
    std::shared_ptr<ProtocolInfo> m_protocol_info; /**< Protocol info */

    uint8_t* m_start {nullptr};         /**< Start of the valid data */
    uint8_t* m_end {nullptr};           /**< First byte after the valid data */
    uint32_t m_id {0};                  /**< Buffer ID. Typically used for session command tracking. */
    uint32_t m_type {TYPE_UNDEFINED};   /**< Data type information */

#ifdef SS_DEBUG
    mxb::Worker* m_owner {nullptr};     /**< Owning thread. Used for debugging */
#endif

    void move_helper(GWBUF&& other) noexcept;
    void clone_helper(const GWBUF& other);
};

inline bool GWBUF::type_is_undefined() const
{
    return m_type == TYPE_UNDEFINED;
}

inline bool GWBUF::type_is_collect_result() const
{
    return m_type & TYPE_COLLECT_RESULT;
}

inline bool GWBUF::type_is_collect_rows() const
{
    return m_type & TYPE_COLLECT_ROWS;
}

// True if the query is not initiated by the client but an internal replaying mechanism
inline bool GWBUF::type_is_replayed() const
{
    return m_type & TYPE_REPLAYED;
}

inline uint8_t* GWBUF::data()
{
    return m_start;
}

inline const uint8_t* GWBUF::data() const
{
    return m_start;
}

inline uint8_t* GWBUF::begin()
{
    return m_start;
}

inline uint8_t* GWBUF::end()
{
    return m_end;
}

inline const uint8_t* GWBUF::begin() const
{
    return m_start;
}

inline const uint8_t* GWBUF::end() const
{
    return m_end;
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
    return m_end - m_start;
}

inline bool GWBUF::empty() const
{
    return m_start == m_end;
}

inline GWBUF::operator bool() const
{
    return !empty();
}

inline void GWBUF::write_complete(size_t n_bytes)
{
    m_end += n_bytes;
    mxb_assert(m_end <= m_sbuf->buf_end);
}

inline uint32_t GWBUF::id() const
{
    return m_id;
}

/*< Number of bytes in the individual buffer */
inline size_t gwbuf_link_length(const GWBUF* b)
{
    return b->length();
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
 * Return the number of bytes of data in the linked list.
 *
 * @param head  The current head of the linked list
 *
 * @return The number of bytes of data in the linked list
 */
extern unsigned int gwbuf_length(const GWBUF* head);

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

#if defined (BUFFER_TRACE)
extern void dprintAllBuffers(void* pdcb);
#endif

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
        std::forward_iterator_tag                       // The type of the iterator
        , uint8_t                                       // The type of the elems
        , std::ptrdiff_t                                // Difference between two its
        , pointer_type                                  // The type of pointer to an elem
        , reference_type>                               // The reference type of an elem
    {
    public:
        /**
         * Returns address of the internal pointer to a GWBUF.
         *
         * @attention This is provided as a backdoor for situations where it is
         *            unavoidable to access the internal pointer directly. It should
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
        m_pBuffer->set_id(id);
    }

    /**
     * Get the ID of this buffer
     *
     * @return The current ID or 0 if no ID is set
     */
    uint32_t id() const
    {
        return m_pBuffer->id();
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
    void set_type(GWBUF::Type type)
    {
        m_pBuffer->set_type(type);
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

    /**
     * Returns the static size of the instance, i.e. sizeof(*this).
     *
     * @return The static size.
     */
    size_t static_size() const
    {
        return sizeof(*this);
    }

    /**
     * Returns the current size of the varying part of the instance.
     *
     * @return The varying size.
     */
    size_t varying_size() const
    {
        return m_pBuffer ? m_pBuffer->runtime_size() : 0;
    }

    /**
     * Returns the runtime size of the instance; i.e. the static size + the varying size.
     *
     * @return The runtime size.
     */
    size_t runtime_size() const
    {
        return static_size() + varying_size();
    }

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

// Conversion functions. Likely needed only temporarily.
GWBUF* gwbuf_to_gwbufptr(GWBUF&& buffer);
GWBUF  gwbufptr_to_gwbuf(GWBUF* buffer);
}
