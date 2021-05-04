/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/listener.hh>
#include <maxscale/workerlocal.hh>

class Service;

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
    static std::shared_ptr<Listener> create(const std::string& name, const std::string& protocol,
                                            const mxs::ConfigParameters& params);

    /**
     * Destroy a listener
     *
     * This removes the listener from the global list of active listeners. Once destroyed, the port used
     * by a listener is open for immediate reuse.
     *
     * @param listener Listener to destroy
     */
    static void destroy(const std::shared_ptr<Listener>& listener);

    // Stop all listeners
    static void stop_all();

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
     * @param host The hostname of this server
     *
     * @return JSON representation of the object
     */
    json_t* to_json(const char* host) const;

    /**
     * Get listener as a JSON API resource
     *
     * @param host The hostname of this server
     *
     * @return JSON API resource representation of the object
     */
    json_t* to_json_resource(const char* host) const;

    /**
     * Get all listeners as a JSON API resource collection
     *
     * @param host The hostname of this server
     *
     * @return The listeners resource collection
     */
    static json_t* to_json_collection(const char* host);

    Type type() const
    {
        return m_type;
    }

    /**
     * Persist listener configuration into a stream
     *
     * @param os Output stream where the listener is persisted
     *
     * @return The output stream given as the parameter
     */
    std::ostream& persist(std::ostream& os) const;

    std::shared_ptr<mxs::ListenerSessionData> shared_data() const
    {
        return m_shared_data;
    }

    static std::unique_ptr<mxs::ListenerSessionData>
    create_shared_data(const mxs::ConfigParameters& params, const std::string& listener_name);

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

    Service*              m_service;        /**< The service to which new sessions are sent */
    mxs::ConfigParameters m_params;         /**< Configuration parameters */

    Type m_type;    /**< The type of the listener */

    mxs::WorkerLocal<int> m_local_fd {-1};  /**< File descriptor the listener listens on */
    int                   m_shared_fd {-1}; /**< File descriptor the listener listens on */

    std::shared_ptr<mxs::ListenerSessionData> m_shared_data;    /**< Data shared with sessions */

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
             const mxs::ConfigParameters& params,
             std::unique_ptr<mxs::ListenerSessionData> shared_data);

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

    static bool read_connection_init_sql(const std::string& filepath,
                                         mxs::ListenerSessionData::ConnectionInitSql* output);
};

/**
 * Find a listener
 *
 * @param name Name of the listener
 *
 * @return The listener if it exists or an empty SListener if it doesn't
 */
std::shared_ptr<Listener> listener_find(const std::string& name);

/**
 * Find listener by socket
 *
 * @param socket  Path to a socket file
 *
 * @return The matching listener if one was found
 */
std::shared_ptr<Listener> listener_find_by_socket(const std::string& socket);

/**
 * Find listener by address and port
 *
 * @param address Network address
 * @param port    Network port
 *
 * @return The matching listener if one was found
 */
std::shared_ptr<Listener> listener_find_by_address(const std::string& address, unsigned short port);

/**
 * Find all listeners that point to a service
 *
 * @param service Service whose listeners are returned
 *
 * @return The listeners that point to the service
 */
std::vector<std::shared_ptr<Listener>> listener_find_by_service(const SERVICE* service);

/**
 * Get common listener parameter definitions.
 *
 * @return Listener parameters
 */
const MXS_MODULE_PARAM* common_listener_params();

void listener_destroy_instances();
