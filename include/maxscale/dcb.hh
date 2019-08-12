/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file dcb.h  The Descriptor Control Block
 */

#include <maxscale/ccdefs.hh>

#include <openssl/ssl.h>
#include <netinet/in.h>

#include <maxbase/poll.h>
#include <maxscale/authenticator.hh>
#include <maxscale/buffer.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/protocol.hh>

#include <memory>

class DCB;
class SERVICE;
class MXS_SESSION;
class SERVER;

namespace maxscale
{
class AuthenticatorSession;
class AuthenticatorBackendSession;
}

#define DCBFD_CLOSED -1

/**
 * The statistics gathered on a descriptor control block
 */
typedef struct dcbstats
{
    int n_reads;        /*< Number of reads on this descriptor */
    int n_writes;       /*< Number of writes on this descriptor */
    int n_accepts;      /*< Number of accepts on this descriptor */
    int n_buffered;     /*< Number of buffered writes */
    int n_high_water;   /*< Number of crosses of high water mark */
    int n_low_water;    /*< Number of crosses of low water mark */
} DCBSTATS;

#define DCBSTATS_INIT {0}

namespace maxscale
{

class SSLContext;

}

/**
 * State of SSL connection
 */
typedef enum
{
    SSL_HANDSHAKE_UNKNOWN,          /*< The DCB has unknown SSL status */
    SSL_HANDSHAKE_REQUIRED,         /*< SSL handshake is needed */
    SSL_HANDSHAKE_DONE,             /*< The SSL handshake completed OK */
    SSL_ESTABLISHED,                /*< The SSL connection is in use */
    SSL_HANDSHAKE_FAILED            /*< The SSL handshake failed */
} SSL_STATE;

/**
 * Descriptor Control Block
 *
 * A wrapper for a network descriptor within the gateway, it contains all the
 * state information necessary to allow for the implementation of the asynchronous
 * operation of the protocol and gateway functions. It also provides links to the service
 * and session data that is required to route the information within the gateway.
 *
 * It is important to hold the state information here such that any thread within the
 * gateway may be selected to execute the required actions when a network event occurs.
 *
 * Note that the first few fields (up to and including "entry_is_ready") must
 * precisely match the LIST_ENTRY structure defined in the list manager.
 */
class DCB : public MXB_POLL_DATA
{
public:
    class Manager
    {
    public:
        /**
         * Called by DCB when created.
         */
        virtual void add(DCB* dcb) = 0;

        /**
         * Called by DCB when destroyed.
         */
        virtual void remove(DCB* dcb) = 0;

        /**
         * Called by DCB when it needs to be destroyed.
         */
        virtual void destroy(DCB* dcb) = 0;

    protected:
        static void call_destroy(DCB* dcb)
        {
            dcb->destroy();
        }
    };

    enum class Role
    {
        CLIENT,         /*< Serves dedicated client */
        BACKEND,        /*< Serves back end connection */
        INTERNAL        /*< Internal DCB not connected to the outside */
    };

    enum class State
    {
        ALLOC,          /*< Created but not added to the poll instance */
        POLLING,        /*< Added to the poll instance */
        DISCONNECTED,   /*< Socket closed */
        NOPOLLING       /*< Removed from the poll instance */
    };

    enum class Reason
    {
        DRAINED,        /*< The write delay queue has drained */
        HIGH_WATER,     /*< Cross high water mark */
        LOW_WATER       /*< Cross low water mark */
    };

    virtual ~DCB();

    Role role() const
    {
        return m_role;
    }

    State state() const
    {
        return m_state;
    }

    /**
     * Is the DCB ready for event handling.
     *
     * @return True if ready, false otherwise.
     */
    virtual bool ready() const = 0;

    MXS_SESSION* session() const
    {
        return m_session;
    }

    SERVICE* service() const;

    MXS_PROTOCOL_SESSION* protocol_session() const
    {
        return m_protocol;
    }

    bool ssl_enabled() const
    {
        return m_ssl != nullptr;
    }

    SSL_STATE ssl_state() const
    {
        return m_ssl_state;
    }

    virtual int ssl_handshake() = 0;

    /**
     * Read data from the DCB.
     *
     * @param ppHead    Pointer to pointer to GWBUF to append to. The GWBUF pointed to
     *                  may be NULL in which case it will be non-NULL after a successful read.
     * @param maxbytes  Maximum amount of bytes to read, 0 means no limit.
     *
     * @return -1 on error, otherwise the total length of the GWBUF. That is, not only
     *         the amount of data appended to the GWBUF.
     */
    int read(GWBUF** ppHead, int maxbytes);

    /**
     * Write data to the DCB.
     *
     * @param pData  The GWBUF to write.
     *
     * @return False on failure, true on success.
     */
    bool write(GWBUF* pData);

    /**
     * Drain the write queue of the DCB.
     *
     * This is called as part of the EPOLLOUT handling of a socket and will try
     * to send any buffered data from the write queue up until the point the
     * write would block.
     *
     * @return The number of bytes written.
     */
    int drain_writeq();

    int32_t protocol_write(GWBUF* pData)
    {
        return m_protocol_api.write(this, pData);
    }

    json_t* protocol_diagnostics_json()
    {
        return m_protocol_api.diagnostics_json ? m_protocol_api.diagnostics_json(this) : nullptr;
    }

    // Starts the shutdown process, called when a client DCB is closed
    void shutdown();

    /**
     * Adds the DCB to the epoll set of the current worker, which in practice
     * means that the DCB will receive I/O events related to its file descriptor,
     * and that corresponding handlers will be called.
     *
     * NOTE: The current worker *must* be the owner of the DCB.
     *
     * @return True on success, false on error.
     */
    virtual bool enable_events();

    /**
     * Remove the DCB from the epoll set of the current worker, which in practice
     * means that the DCB will no longer receive I/O events related to its file
     * descriptor and that corresponding handlers will no longer be called.
     *
     * NOTE: The current worker *must* be the owner of the DCB.
     *
     * @return True on success, false on error.
     */
    virtual bool disable_events();

    // BEGIN: Temporarily here, do not use.
    static void close(DCB* dcb);
    void set_session(MXS_SESSION* s)
    {
        m_session = s;
    }
    static void add_event(DCB* dcb, GWBUF* buf, uint32_t ev);
    // END

    int add_callback(Reason, int (*)(DCB*, Reason, void*), void*);
    int remove_callback(Reason, int (*)(DCB*, Reason, void*), void*);

    bool                    m_dcb_errhandle_called = false;   /**< this can be called only once */
    int                     m_fd = DCBFD_CLOSED;                /**< The descriptor */
    SSL_STATE               m_ssl_state = SSL_HANDSHAKE_UNKNOWN;/**< Current state of SSL if in use */
    char*                   m_remote = nullptr;                   /**< Address of remote end */
    char*                   m_user = nullptr;                     /**< User name for connection */
    struct sockaddr_storage m_ip;                                 /**< remote IPv4/IPv6 address */
    size_t                  m_protocol_packet_length = 0;         /**< protocol packet length */
    size_t                  m_protocol_bytes_processed = 0;       /**< How many bytes have been read */
    uint64_t                m_writeqlen = 0;                    /**< Bytes in writeq */
    uint64_t                m_high_water = 0;                     /**< High water mark of write queue */
    uint64_t                m_low_water = 0;                      /**< Low water mark of write queue */
    GWBUF*                  m_writeq = nullptr;                 /**< Write Data Queue */
    GWBUF*                  m_delayq = nullptr;                   /**< Delay Backend Write Data Queue */
    GWBUF*                  m_readq = nullptr;                  /**< Read queue for incomplete reads */
    GWBUF*                  m_fakeq = nullptr;                  /**< Fake event queue for generated events */
    uint32_t                m_fake_event = 0;                     /**< Fake event to be delivered to handler */

    DCBSTATS m_stats = {};                      /**< DCB related statistics */
    void*          m_data = nullptr;              /**< Client protocol data, owned by client DCB */

    struct CALLBACK
    {
        Reason           reason;   /*< The reason for the callback */
        int           (* cb)(DCB* dcb, Reason reason, void* userdata);
        void*            userdata; /*< User data to be sent in the callback */
        struct CALLBACK* next;     /*< Next callback for this DCB */
    };

    CALLBACK*      m_callbacks = nullptr;        /**< The list of callbacks for the DCB */
    int64_t        m_last_read = 0;              /**< Last time the DCB received data */
    int64_t        m_last_write = 0;             /**< Last time the DCB sent data */
    SERVER*        m_server = nullptr;           /**< The associated backend server */
    bool           m_high_water_reached = false; /** High water mark reached, to determine whether we need to
                                                 * release
                                                 * throttle */
    uint32_t m_nClose = 0;   /** How many times dcb_close has been called. */
    uint64_t m_uid;         /**< Unique identifier for this DCB */

protected:
    DCB(int fd,
        Role role,
        MXS_SESSION* session,
        MXS_PROTOCOL_SESSION* protocol,
        MXS_PROTOCOL_API protocol_api,
        SERVER* server,
        Manager* manager);

    int create_SSL(mxs::SSLContext* ssl);

    /**
     * Release the instance from the associated session.
     *
     * @param session The session to release the DCB from.
     *
     * @return True, if the DCB was released and can be deleted, false otherwise.
     */
    virtual bool release_from(MXS_SESSION* session) = 0;

    /**
     * Prepare the instance for destruction.
     *
     * @return True if it was prepared and can be destroyed, false otherwise.
     */
    virtual bool prepare_for_destruction() = 0;

    void stop_polling_and_shutdown();

    State                 m_state = State::ALLOC;        /**< Current state */
    MXS_SESSION*          m_session;                     /**< The owning session */
    MXS_PROTOCOL_SESSION* m_protocol;                    /**< The protocol session */
    MXS_PROTOCOL_API      m_protocol_api;                /**< Protocol functions for the DCB */
    SSL*                  m_ssl = nullptr;               /**< SSL struct for connection */
    bool                  m_ssl_read_want_read = false;
    bool                  m_ssl_read_want_write = false;
    bool                  m_ssl_write_want_read = false;
    bool                  m_ssl_write_want_write = false;

private:
    friend class Manager;

    int read_SSL(GWBUF** head);
    GWBUF* basic_read_SSL(int* nsingleread);

    int write_SSL(GWBUF* writeq, bool* stop_writing);

    void destroy();
    static void free(DCB* dcb);

    static uint32_t poll_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);
    static uint32_t event_handler(DCB* dcb, uint32_t events);
    static uint32_t process_events(DCB* dcb, uint32_t events);

    class FakeEventTask;
    friend class FakeEventTask;

    void call_callback(Reason reason);

private:
    Role         m_role;    /**< The role of the DCB */
    Manager*     m_manager; /**< The DCB manager to use */
};

namespace maxscale
{

const char* to_string(DCB::State state);

}

class ClientDCB : public DCB
{
public:
    ~ClientDCB() override;

    static ClientDCB* create(int fd, MXS_SESSION* session, DCB::Manager* manager);

    int ssl_handshake() override;

    std::unique_ptr<mxs::AuthenticatorSession> m_auth_session; /**< Client authentication data */

    bool ready() const;

protected:
    // Only for InternalDCB.
    ClientDCB(int fd,
              DCB::Role role,
              MXS_SESSION* session,
              MXS_PROTOCOL_SESSION* protocol,
              MXS_PROTOCOL_API protocol_api,
              Manager* manager);

private:
    ClientDCB(int fd,
              MXS_SESSION* session,
              MXS_PROTOCOL_SESSION* protocol,
              MXS_PROTOCOL_API protocol_api,
              Manager* manager);

    bool release_from(MXS_SESSION* session) override;
    bool prepare_for_destruction() override;
};

class BackendDCB : public DCB
{
public:
    static BackendDCB* connect(SERVER* server, MXS_SESSION* session, DCB::Manager* manager);

    /**
     * Hangup all BackendDCBs connected to a particular server.
     *
     * @param server  BackendDCBs connected to this server should be closed.
     */
    static void hangup(const SERVER* server);

    int ssl_handshake() override;

    std::unique_ptr<mxs::AuthenticatorBackendSession> m_auth_session; /**< Backend authentication data */

    bool ready() const;

    bool is_in_persistent_pool() const
    {
        return m_persistentstart > 0;
    }

    bool was_persistent() const
    {
        return m_was_persistent;
    }

    void clear_was_persistent()
    {
        m_was_persistent = false;
    }

    static int persistent_clean_count(BackendDCB* dcb, int id, bool cleanall);

    // TODO: Temporarily public.
    BackendDCB* m_nextpersistent = nullptr; /**< Next DCB in the persistent pool for SERVER */

private:
    BackendDCB(int fd,
               MXS_SESSION* session,
               MXS_PROTOCOL_SESSION* protocol,
               MXS_PROTOCOL_API protocol_api,
               SERVER* server,
               Manager* manager,
               std::unique_ptr<mxs::AuthenticatorBackendSession> auth_ses);

    static BackendDCB* create(int fd,
                              SERVER* server,
                              MXS_SESSION* session,
                              DCB::Manager* manager);

    static BackendDCB* take_from_connection_pool(SERVER* server, MXS_SESSION* session);

    static bool maybe_add_persistent(BackendDCB* dcb);

    bool release_from(MXS_SESSION* session) override;
    bool prepare_for_destruction() override;

    static void hangup_cb(MXB_WORKER* worker, const SERVER* server);

    time_t      m_persistentstart = 0;      /**<    0: Not in the persistent pool.
                                             *      -1: Evicted from the persistent pool and being closed.
                                             *   non-0: Time when placed in the persistent pool.
                                             */
    bool m_was_persistent = false;          /**< Whether this DCB was in the persistent pool */
};

class InternalDCB : public ClientDCB
{
public:
    static InternalDCB* create(MXS_SESSION* session, DCB::Manager* manager);

    int ssl_handshake() override;

    bool enable_events() override;
    bool disable_events() override;

private:
    InternalDCB(MXS_SESSION* session, MXS_PROTOCOL_API protocol_api, Manager* manager);

    bool prepare_for_destruction() override;
};

namespace maxscale
{

const char* to_string(DCB::Role role);

}

inline bool dcb_write(DCB* dcb, GWBUF* queue)
{
    return dcb->write(queue);
}

inline int dcb_read(DCB* dcb, GWBUF** head, int maxbytes)
{
    return dcb->read(head, maxbytes);
}
int  dcb_bytes_readable(DCB* dcb);
inline int dcb_drain_writeq(DCB* dcb)
{
    return dcb->drain_writeq();
}
inline void dcb_close(DCB* dcb)
{
    DCB::close(dcb);
}

/**
 * @brief Close DCB in the thread that owns it.
 *
 * @param dcb The dcb to be closed.
 *
 * @note Even if the calling thread owns the dcb, the closing will
 *       still be made via the event loop.
 */
void dcb_close_in_owning_thread(DCB* dcb);

void printAllDCBs();                                                            /* Debug to print all DCB in
                                                                                 * the system */
void printDCB(DCB*);                                                            /* Debug print routine */
void dprintDCBList(DCB*);                                                       /* Debug print DCB list
                                                                                 * statistics */
void dprintAllDCBs(DCB*);                                                       /* Debug to print all DCB in
                                                                                 * the system */
void dprintOneDCB(DCB*, DCB*);                                                  /* Debug to print one DCB */
void dListDCBs(DCB*);                                                           /* List all DCBs in the system
                                                                                 * */
void dListClients(DCB*);                                                        /* List al the client DCBs */
void dcb_printf(DCB*, const char*, ...) __attribute__ ((format(printf, 2, 3))); /* DCB version of printf */

/**
 * Return DCB counts filtered by role
 *
 * @param role   What kind of DCBs should be counted.
 *
 * @return  Count of DCBs in the specified role.
 */
int dcb_count_by_role(DCB::Role role);

uint64_t dcb_get_session_id(DCB* dcb);
void     dcb_enable_session_timeouts();
void     dcb_process_timeouts(int thr);

/**
 * @brief Append a buffer the DCB's readqueue
 *
 * Usually data is stored into the DCB's readqueue when not enough data is
 * available and the processing needs to be deferred until more data is available.
 *
 * @param dcb    The DCB to be appended to.
 * @param buffer The buffer to append.
 */
static inline void dcb_readq_append(DCB* dcb, GWBUF* buffer)
{
    dcb->m_readq = gwbuf_append(dcb->m_readq, buffer);
}

/**
 * @brief Returns the read queue of the DCB.
 *
 * @note The read queue remains the property of the DCB.
 *
 * @return A buffer of NULL if there is no read queue.
 */
static GWBUF* dcb_readq_get(DCB* dcb)
{
    return dcb->m_readq;
}

/**
 * @brief Returns whether a DCB currently has a read queue.
 *
 * @return True, if the DCB has a read queue, otherwise false.
 */
static inline bool dcb_readq_has(DCB* dcb)
{
    return dcb->m_readq != NULL;
}

/**
 * @brief Returns the current length of the read queue
 *
 * @return Length of read queue
 */
static unsigned int dcb_readq_length(DCB* dcb)
{
    return dcb->m_readq ? gwbuf_length(dcb->m_readq) : 0;
}

/**
 * @brief Prepend a buffer the DCB's readqueue
 *
 * @param dcb    The DCB to be prepended to.
 * @param buffer The buffer to prepend
 */
static inline void dcb_readq_prepend(DCB* dcb, GWBUF* buffer)
{
    dcb->m_readq = gwbuf_append(buffer, dcb->m_readq);
}

/**
 * @brief Returns the read queue of the DCB and sets the read queue to NULL.
 *
 * @note The read queue becomes the property of the caller.
 *
 * @return A buffer of NULL if there is no read queue.
 */
static GWBUF* dcb_readq_release(DCB* dcb)
{
    GWBUF* readq = dcb->m_readq;
    dcb->m_readq = NULL;
    return readq;
}

/**
 * @brief Set read queue of a DCB
 *
 * The expectation is that there is no readqueue when this is done.
 * The ownership of the provided buffer moved to the DCB.
 *
 * @param dcb    The DCB to be reset.
 * @param buffer The buffer to reset with
 */
static inline void dcb_readq_set(DCB* dcb, GWBUF* buffer)
{
    if (dcb->m_readq)
    {
        MXS_ERROR("Read-queue set when there already is a read-queue.");
        // TODO: Conceptually this should be freed here. However, currently
        // TODO: the code just assigns without checking, so we do the same
        // TODO: for now. If this is not set to NULL when it has been consumed,
        // TODO: we would get a double free.
        // TODO: gwbuf_free(dcb->m_readq);
        dcb->m_readq = NULL;
    }
    dcb->m_readq = buffer;
}

/**
 * @brief Call a function for each connected DCB
 *
 * @deprecated You should not use this function, use dcb_foreach_parallel instead
 *
 * @warning This must only be called from the main thread, otherwise deadlocks occur
 *
 * @param func Function to call. The function should return @c true to continue iteration
 * and @c false to stop iteration earlier. The first parameter is a DCB and the second
 * is the value of @c data that the user provided.
 * @param data User provided data passed as the second parameter to @c func
 * @return True if all DCBs were iterated, false if the callback returned false
 */
bool dcb_foreach(bool (* func)(DCB* dcb, void* data), void* data);

/**
 * @brief Call a function for each connected DCB on the current worker
 *
 * @param func Function to call. The function should return @c true to continue
 *             iteration and @c false to stop iteration earlier. The first parameter
 *             is the current DCB.
 *
 * @param data User provided data passed as the second parameter to @c func
 */
void dcb_foreach_local(bool (* func)(DCB* dcb, void* data), void* data);

/**
 * @brief Return the port number this DCB is connected to
 *
 * @param dcb DCB to inspect
 * @return Port number the DCB is connected to or -1 if information is not available
 */
int dcb_get_port(const DCB* dcb);

/**
 * @brief Return the DCB currently being handled by the calling thread.
 *
 * @return A DCB, or NULL if the calling thread is not currently handling
 *         a DCB or if the calling thread is not a polling/worker thread.
 */
DCB* dcb_get_current();

/**
 * Get JSON representation of the DCB
 *
 * @param dcb DCB to convert to JSON
 *
 * @return The JSON representation
 */
json_t* dcb_to_json(DCB* dcb);
