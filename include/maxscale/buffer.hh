/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include <maxscale/ccdefs.hh>
#include <memory>
#include <vector>
#include <maxbase/assert.hh>
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
class GWBUF final
{
public:
    class ProtocolInfo
    {
    public:
        virtual ~ProtocolInfo() = default;
        virtual size_t size() const = 0;

        // If true, the ProtocolInfo can be cached and reused based on the canonical form of the query. If
        // false, the result should not be cached and should always be created again.
        bool cacheable() const
        {
            return m_cacheable;
        }

        void set_cacheable(bool value)
        {
            m_cacheable = value;
        }

    private:
        bool m_cacheable {true};
    };

    enum Type : uint32_t
    {
        TYPE_UNDEFINED      = 0,
        TYPE_COLLECT_RESULT = (1 << 0),

        // This causes the current resultset rows to be collected into mxs::Reply. They can be accessed
        // using mxs::Reply::row_data() inside the clientReply function and they are only available for
        // the duration of the function call. The rows should be considered a read-only view into
        // the buffer that contains them.
        TYPE_COLLECT_ROWS = (1 << 2),
    };

    using HintVector = std::vector<Hint>;

    using iterator = uint8_t*;
    using const_iterator = const uint8_t*;

    /**
     * Constructs an empty GWBUF. Does not allocate any storage. Calling most storage-accessing functions
     * on an empty buffer is an error.
     */
    GWBUF() = default;

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

    GWBUF(GWBUF&& rhs) noexcept = default;

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
    GWBUF& operator=(GWBUF&& rhs) noexcept = default;

    /**
     * Shallow-clones the source buffer. In general, should not be used to create long-term copies as this
     * prevents freeing the underlying data. GWBUFs traveling along the routing chain are best
     * recycled when written to a socket. Any existing shallow copies prevent this from happening.
     */
    GWBUF shallow_clone() const;

    /**
     * Deep-clones the source buffer. Only allocates minimal capacity. Is best used when the GWBUF is
     * stored for later use. The buffer will be as if it was just read from memory and will not contain any of
     * the auxiliary data or the protocol information.
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
    void set_protocol_info(std::shared_ptr<ProtocolInfo> new_info)
    {
        m_protocol_info = std::move(new_info);
    }

    /**
     * Get out-of-band protocol information associated with the buffer.
     *
     * @return Information or null.
     */
    const std::shared_ptr<ProtocolInfo>& get_protocol_info() const
    {
        return m_protocol_info;
    }

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

    /**
     * Get the hints attached to this buffer
     *
     * @return The hints if there were any
     */
    const HintVector& hints() const;

    /**
     * Add a routing hint to this buffer
     *
     * @param args Arguments given to the Hint object
     */
    template<typename ... Args>
    void add_hint(Args && ... args);

    // Sets the buffer type
    void set_type(Type type);

    // Checks if the buffer is of the given type
    bool type_is_undefined() const;
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

    /**
     * Minimize the object memory footprint
     *
     * This function should be called whenever the buffer is stored for a longer time. Only the raw data is
     * stored and everything else that can be derived from it is freed. The ID, type and hints that aren't
     * derived from it stay the same.
     */
    void minimize()
    {
        m_protocol_info.reset();
        GWBUF tmp = deep_clone();
        *this = std::move(tmp);
    }

private:
    std::shared_ptr<SHARED_BUF>   m_sbuf;           /**< The shared buffer with the real data */
    std::shared_ptr<ProtocolInfo> m_protocol_info;  /**< Protocol info */
    HintVector                    m_hints;          /**< Hint data for this buffer */

    uint8_t* m_start {nullptr};         /**< Start of the valid data */
    uint8_t* m_end {nullptr};           /**< First byte after the valid data */
    uint32_t m_id {0};                  /**< Buffer ID. Typically used for session command tracking. */
    uint32_t m_type {TYPE_UNDEFINED};   /**< Data type information */

    // The copy-constructor is only used by shallow_clone()
    GWBUF(const GWBUF& rhs) = default;
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

inline const GWBUF::HintVector& GWBUF::hints() const
{
    return m_hints;
}

template<typename ... Args>
void GWBUF::add_hint(Args&& ... args)
{
    m_hints.emplace_back(std::forward<Args>(args)...);
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
 * Append a buffer onto a linked list of buffer structures.
 *
 * @param head  The current head of the linked list or NULL.
 * @param tail  Another buffer to make the tail of the linked list, must not be NULL
 *
 * @return The new head of the linked list
 */
GWBUF* gwbuf_append(GWBUF* head, GWBUF* tail);

namespace maxscale
{
// Conversion functions. Likely needed only temporarily.
GWBUF* gwbuf_to_gwbufptr(GWBUF&& buffer);
GWBUF  gwbufptr_to_gwbuf(GWBUF* buffer);
}
