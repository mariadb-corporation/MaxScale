#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include <string>
#include <deque>

#include <maxscale/thread.h>

#include "http.hh"
#include "adminclient.hh"

using std::deque;
using std::string;

typedef deque<SAdminClient> ClientList;

/** The admin interface configuration */
struct AdminConfig
{
    string         host;
    uint16_t       port;
    enum http_auth auth;
};

class AdminListener
{
public:
    /**
     * @brief Create a new admin interface instance
     *
     * @param sock Listener socket for the interface
     */
    AdminListener(int sock);
    ~AdminListener();

    /**
     * Start the admin interface
     */
    void start();

    /**
     * Stop the admin listener
     */
    void stop();

    /**
     * Close timed out connections
     */
    void check_timeouts();

private:
    void         handle_clients();
    void         handle_timeouts();
    AdminClient* accept_client();

    int        m_socket;  /**< The network socket we listen on */
    int        m_active;  /**< Positive value if the admin is active */
    int        m_timeout; /**< Network timeout in seconds */
    ClientList m_clients;
};

/**
 * @brief Start the administrative interface
 *
 * @return True if the interface was successfully started
 */
bool mxs_admin_init();

/**
 * @brief Shutdown the administrative interface
 */
void mxs_admin_shutdown();

/**
 * @brief Get the administative interface configuration
 *
 * @return A reference to the administrative interface configuration
 */
AdminConfig& mxs_admin_get_config();
