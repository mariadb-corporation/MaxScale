/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <atomic>
#include <string>
#include <memory>
#include <vector>

#include <maxbase/jansson.h>
#include <maxscale/protocol.hh>
#include <maxscale/ssl.hh>
#include <maxscale/service.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/workerlocal.hh>

class DCB;
class Service;
class Listener;
using SListener = std::shared_ptr<Listener>;
class ListenerSessionData;

/**
 * The Listener class is used to link a network port to a service. It defines the name of the
 * protocol module that should be loaded as well as the authenticator that is used.
 */
class Listener : public MXB_POLL_DATA
{
public:
    enum class Type
    {
        UNIX_SOCKET,    // UNIX domain socket shared between workers
        SHARED_TCP,     // TCP listening socket shared between workers
        UNIQUE_TCP,     // Unique TCP listening socket for each worker
        MAIN_WORKER,    // Listener that always moves the execution to the main worker
    };

    ~Listener();

    /**
     * Create a new listener
     *
     * @param name     Name of the listener
     * @param protocol Protocol module to use
     * @param params   Parameters for the listener
     *
     * @return New listener or nullptr on error
     */
    static SListener create(const std::string& name,
                            const std::string& protocol,
                            const MXS_CONFIG_PARAMETER& params);

    /**
     * Destroy a listener
     *
     * This removes the listener from the global list of active listeners. Once destroyed, the port used
     * by a listener is open for immediate reuse.
     *
     * @param listener Listener to destroy
     */
    static void destroy(const SListener& listener);

    /**
     * Start listening on the configured port
     *
     * @return True if the listener was able to start listening
     */
    bool listen();

    /**
     * Stop the listener
     *
     * @return True if the listener was successfully stopped
     */
    bool stop();

    /**
     * Start a stopped listener
     *
     * @return True if the listener was successfully started
     */
    bool start();

    /**
     * Listener name
     */
    const char* name() const;

    /**
     * Network address the listener listens on
     */
    const char* address() const;

    /**
     * Network port the listener listens on
     */
    uint16_t port() const;

    /**
     * Service the listener points to
     */
    SERVICE* service() const;

    /**
     * The protocol module name
     */
    const char* protocol() const;

    /**
     * The state of the listener
     */
    const char* state() const;

    /**
     * Convert to JSON
     *
     * @return JSON representation of the object
     */
    json_t* to_json() const;

    /**
     * Load users for a listener
     *
     * @return The result from the authenticator module
     */
    int load_users();

    Type type() const
    {
        return m_type;
    }

    // Functions that are temporarily public
    bool create_listener_config(const char* filename);

    std::shared_ptr<ListenerSessionData> shared_data() const
    {
        return m_shared_data;
    }

private:
    enum State
    {
        CREATED,
        STARTED,
        STOPPED,
        FAILED,
        DESTROYED
    };

    std::string m_name;             /**< Name of the listener */
    State       m_state;            /**< Listener state */
    std::string m_protocol;         /**< Protocol module to load */
    uint16_t    m_port;             /**< Port to listen on */
    std::string m_address;          /**< Address to listen with */

    // Protocol module. Ownership shared with sessions created from this listener.
    std::shared_ptr<mxs::ProtocolModule> m_proto_module;

    Service*             m_service;         /**< The service to which new sessions are sent */
    MXS_CONFIG_PARAMETER m_params;          /**< Configuration parameters */

    Type m_type;    /**< The type of the listener */

    mxs::WorkerLocal<int> m_local_fd {-1};  /**< File descriptor the listener listens on */
    int                   m_shared_fd {-1}; /**< File descriptor the listener listens on */

    std::shared_ptr<ListenerSessionData> m_shared_data; /**< Data shared with sessions */

    /**
     * Creates a new listener that points to a service
     *
     * @param service       Service where the listener points to
     * @param name          Name of the listener
     * @param address       The address where the listener listens
     * @param port          The port on which the listener listens
     * @param protocol      The protocol module to use
     */
    Listener(Service* service, const std::string& name,
             const std::string& address, uint16_t port,
             const std::string& protocol,
             std::unique_ptr<mxs::ProtocolModule> proto_instance,
             const MXS_CONFIG_PARAMETER& params,
             std::unique_ptr<ListenerSessionData> shared_data);

    /**
     * Listen on a file descriptor shared between all workers
     *
     * @return True if the listening was started successfully
     */
    bool listen_shared();

    /**
     * Listen with a unique file descriptor for each worker
     *
     * @return True if the listening was started successfully
     */
    bool listen_unique();

    /**
     * Close all opened file descriptors for this listener
     */
    void close_all_fds();

    /**
     * Accept a single client connection
     *
     * @param fd The opened file descriptor to which the client is connected to
     * @param addr The network information
     * @param host The host where the client is connecting from
     *
     * @return The new DCB or nullptr on error
     */
    ClientDCB* accept_one_dcb(int fd, const sockaddr_storage* addr, const char* host);

    /**
     * Accept all available client connections
     */
    void accept_connections();

    /**
     * Reject a client connection
     *
     * Writes an error message to the fd if the protocol supports it and then closes it.
     *
     * @param fd   The file descriptor to close
     * @param host The host where the connection originated from
     */
    void reject_connection(int fd, const char* host);

    /**
     * The file descriptor for accepting new connections
     *
     * @return The worker-local file descriptor
     */
    int fd() const
    {
        return m_type == Type::UNIQUE_TCP ? *m_local_fd : m_shared_fd;
    }

    // Handler for EPOLL_IN events
    static uint32_t poll_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);
};

/**
 * Find all listeners that point to a service
 *
 * @param service Service whose listeners are returned
 *
 * @return The listeners that point to the service
 */
std::vector<SListener> listener_find_by_service(const SERVICE* service);

/**
 * Listener settings and other data that is shared with all sessions created by the listener.
 * Should be referred to with shared_ptr.
 *
 * The contents should not change once a session with the data has been created, as this could
 * create concurrency issues. If listener settings are changed, the listener should create a new
 * shared data object and share that with new sessions. The old sessions will keep using the
 * previous settings.
 */
class ListenerSessionData
{
public:
    ListenerSessionData(qc_sql_mode_t default_sql_mode, SERVICE* service);
    ListenerSessionData(const ListenerSessionData&) = delete;
    ListenerSessionData& operator=(const ListenerSessionData&) = delete;

    /**
     * Increment the number of authentication failures from the remote address. If the number
     * exceeds the configured limit, future attempts to connect from the remote are be rejected.
     *
     * @param remote The address where the connection originated
     */
    void mark_auth_as_failed(const std::string& remote);

    mxs::SSLContext     m_ssl;                      /**< SSL settings */
    const qc_sql_mode_t m_default_sql_mode;         /**< Default sql mode for the listener */
    SERVICE&            m_service;                  /**< The service the listener feeds */
};
