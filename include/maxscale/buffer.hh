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
#include <algorithm>
#include <iterator>
#include <memory>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <maxbase/assert.h>
#include <maxscale/hint.h>

class SERVER;

/**
 * Buffer properties - used to store properties related to the buffer
 * contents. This may be added at any point during the processing of the
 * data, especially in the protocol stage of the processing.
 */
struct BUF_PROPERTY
{
    char*           name;
    char*           value;
    BUF_PROPERTY*   next;
};

enum gwbuf_type_t
{
    GWBUF_TYPE_UNDEFINED      = 0,
    GWBUF_TYPE_HTTP           = (1 << 0),
    GWBUF_TYPE_IGNORABLE      = (1 << 1),
    GWBUF_TYPE_COLLECT_RESULT = (1 << 2),
    GWBUF_TYPE_RESULT         = (1 << 3),
    GWBUF_TYPE_REPLY_OK       = (1 << 4),
    GWBUF_TYPE_REPLAYED       = (1 << 5),
};

#define GWBUF_IS_TYPE_UNDEFINED(b)     ((b)->gwbuf_type == 0)
#define GWBUF_IS_IGNORABLE(b)          ((b)->gwbuf_type & GWBUF_TYPE_IGNORABLE)
#define GWBUF_IS_COLLECTED_RESULT(b)   ((b)->gwbuf_type & GWBUF_TYPE_RESULT)
#define GWBUF_SHOULD_COLLECT_RESULT(b) ((b)->gwbuf_type & GWBUF_TYPE_COLLECT_RESULT)
#define GWBUF_IS_REPLY_OK(b)           ((b)->gwbuf_type & GWBUF_TYPE_REPLY_OK)

// True if the query is not initiated by the client but an internal replaying mechanism
#define GWBUF_IS_REPLAYED(b)           ((b)->gwbuf_type & GWBUF_TYPE_REPLAYED)

enum  gwbuf_info_t
{
    GWBUF_INFO_NONE   = 0x0,
    GWBUF_INFO_PARSED = 0x1
};

#define GWBUF_IS_PARSED(b) (b->sbuf->info & GWBUF_INFO_PARSED)

/**
 * A structure for cleaning up memory allocations of structures which are
 * referred to by GWBUF and deallocated in gwbuf_free but GWBUF doesn't
 * know what they are.
 * All functions on the list are executed before freeing memory of GWBUF struct.
 */
enum bufobj_id_t
{
    GWBUF_PARSING_INFO
};

struct buffer_object_t
{
    bufobj_id_t      bo_id;
    void*            bo_data;
    void             (* bo_donefun_fp)(void*);
    buffer_object_t* bo_next;
};

/**
 * A structure to encapsulate the data in a form that the data itself can be
 * shared between multiple GWBUF's without the need to make multiple copies
 * but still maintain separate data pointers.
 */
struct SHARED_BUF
{
    buffer_object_t* bufobj;    /*< List of objects referred to by GWBUF */
    int32_t          refcount;  /*< Reference count on the buffer */
    uint32_t         info;      /*< Info bits */
    unsigned char    data[1];   /*< Actual memory that was allocated */
};

/**
 * The buffer structure used by the descriptor control blocks.
 *
 * Linked lists of buffers are created as data is read from a descriptor
 * or written to a descriptor. The use of linked lists of buffers with
 * flexible data pointers is designed to minimise the need for data to
 * be copied within the gateway.
 */
struct GWBUF
{
    GWBUF*         next;        /*< Next buffer in a linked chain of buffers */
    GWBUF*         tail;        /*< Last buffer in a linked chain of buffers */
    void*          start;       /*< Start of the valid data */
    void*          end;         /*< First byte after the valid data */
    SHARED_BUF*    sbuf;        /*< The shared buffer with the real data */
    HINT*          hint;        /*< Hint data for this buffer */
    BUF_PROPERTY*  properties;  /*< Buffer properties */
    SERVER*        server;      /*< The target server where the buffer is executed */
    uint32_t       gwbuf_type;  /*< buffer's data type information */
#ifdef SS_DEBUG
    int owner;      /*< Owner of the thread, only for debugging */
#endif
};

/*<
 * Macros to access the data in the buffers
 */
/*< First valid, unconsumed byte in the buffer */
#define GWBUF_DATA(b) ((uint8_t*)(b)->start)

/*< Number of bytes in the individual buffer */
#define GWBUF_LENGTH(b) ((size_t)((char*)(b)->end - (char*)(b)->start))

/*< Return the byte at offset byte from the start of the unconsumed portion of the buffer */
#define GWBUF_DATA_CHAR(b, byte) (GWBUF_LENGTH(b) < ((byte) + 1) ? -1 : *(((char*)(b)->start) + 4))

/*< Check that the data in a buffer has the SQL marker*/
#define GWBUF_IS_SQL(b) (0x03 == GWBUF_DATA_CHAR(b, 4))

/*< Check whether the buffer is contiguous*/
#define GWBUF_IS_CONTIGUOUS(b) (((b) == NULL) || ((b)->next == NULL))

/*< True if all bytes in the buffer have been consumed */
#define GWBUF_EMPTY(b) ((char*)(b)->start >= (char*)(b)->end)

/*< Consume a number of bytes in the buffer */
#define GWBUF_CONSUME(b, \
                      bytes) ((b)->start = bytes \
                                  > ((char*)(b)->end \
                                     - (char*)(b)->start) ? (b)->end : (void*)((char*)(b)->start + (bytes)));

/*< Check if a given pointer is within the buffer */
#define GWBUF_POINTER_IN_BUFFER \
    (ptr, b) \
    ((char*)(ptr) >= (char*)(b)->start && (char*)(ptr) < (char*)(b)->end)

/*< Consume a complete buffer */
#define GWBUF_CONSUME_ALL(b) gwbuf_consume((b), GWBUF_LENGTH((b)))

#define GWBUF_RTRIM(b, bytes) \
    ((b)->end = bytes > ((char*)(b)->end - (char*)(b)->start) ? (b)->start   \
                                                              : (void*)((char*)(b)->end - (bytes)));

#define GWBUF_TYPE(b) (b)->gwbuf_type
/*<
 * Function prototypes for the API to maniplate the buffers
 */

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
 * @param buf  The head of the list of buffers to free
 */
extern void gwbuf_free(GWBUF* buf);

/**
 * Clone a GWBUF. Note that if the GWBUF is actually a list of
 * GWBUFs, then every GWBUF in the list will be cloned. Note that but
 * for the GWBUF structure itself, the data is shared.
 *
 * @param buf  The GWBUF to be cloned.
 *
 * @return The cloned GWBUF, or NULL if @buf was NULL or if any part
 *         of @buf could not be cloned.
 */
extern GWBUF* gwbuf_clone(GWBUF* buf);

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
 * @attention A NULL @c GWBUF is considered to be less than a non-NULL one,
 *            and a shorter @c GWBUF less than a longer one. Otherwise the
 *            the sign of the return value is determined by the sign of the
 *            difference between the first pair of bytes (interpreted as
 *            unsigned char) that differ in lhs and rhs.
 */
extern int gwbuf_compare(const GWBUF* lhs, const GWBUF* rhs);

/**
 * Append a buffer onto a linked list of buffer structures.
 *
 * This call should be made with the caller holding the lock for the linked
 * list.
 *
 * @param head  The current head of the linked list
 * @param tail  The new buffer to make the tail of the linked list
 *
 * @return The new head of the linked list
 */
extern GWBUF* gwbuf_append(GWBUF* head, GWBUF* tail);

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
extern GWBUF* gwbuf_consume(GWBUF* head, unsigned int length);

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
extern GWBUF* gwbuf_rtrim(GWBUF* head, unsigned int length);

/**
 * Return the number of bytes of data in the linked list.
 *
 * @param head  The current head of the linked list
 *
 * @return The number of bytes of data in the linked list
 */
extern unsigned int gwbuf_length(const GWBUF* head);

/**
 * Return the number of individual buffers in the linked list.
 *
 * Currently not used, provided mainly for use during debugging sessions.
 *
 * @param head  The current head of the linked list
 *
 * @return The number of bytes of data in the linked list
 */
extern int gwbuf_count(const GWBUF* head);

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
 * Add a property to a buffer.
 *
 * @param buf    The buffer to add the property to
 * @param name   The property name
 * @param value  The property value
 *
 * @return True on success, false otherwise.
 */
extern bool gwbuf_add_property(GWBUF* buf, const char* name, const char* value);

/**
 * Return the value of a buffer property
 *
 * @param buf   The buffer itself
 * @param name  The name of the property to return
 *
 * @return The property value or NULL if the property was not found.
 */
extern char* gwbuf_get_property(GWBUF* buf, const char* name);

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

/**
 * Add a buffer object to GWBUF buffer.
 *
 * @param buf         GWBUF where object is added
 * @param id          Type identifier for object
 * @param data        Object data
 * @param donefun_fp  Clean-up function to be executed before buffer is freed.
 */
void gwbuf_add_buffer_object(GWBUF* buf,
                             bufobj_id_t id,
                             void* data,
                             void (* donefun_fp)(void*));

/**
 * Search buffer object which matches with the id.
 *
 * @param buf  GWBUF to be searched
 * @param id   Identifier for the object
 *
 * @return Searched buffer object or NULL if not found
 */
void* gwbuf_get_buffer_object_data(GWBUF* buf, bufobj_id_t id);
#if defined (BUFFER_TRACE)
extern void dprintAllBuffers(void* pdcb);
#endif

/**
 * Debug function for dumping buffer contents to log
 *
 * @param buffer    Buffer to dump
 * @param log_level Log priority where the message is written
 */
void gwbuf_hexdump(GWBUF* buffer, int log_level);

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

namespace std
{

template<>
struct default_delete<GWBUF>
{
    void operator()(GWBUF* pBuffer)
    {
        gwbuf_free(pBuffer);
    }
};
}

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

    protected:
        iterator_base(buf_type pBuffer = NULL)
            : m_pBuffer(pBuffer)
            , m_i(m_pBuffer ? GWBUF_DATA(m_pBuffer) : NULL)
            , m_end(m_pBuffer ? (m_i + GWBUF_LENGTH(m_pBuffer)) : NULL)
        {
        }

        void advance()
        {
            mxb_assert(m_i != m_end);

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
            m_pBuffer = gwbuf_clone(rhs.m_pBuffer);

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
    GWBUF** operator&()
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
            mxb_assert(!true);
            throw std::bad_alloc();
        }
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
}
