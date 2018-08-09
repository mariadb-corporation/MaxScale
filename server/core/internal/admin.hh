#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>

#include <string>
#include <microhttpd.h>

#include <maxscale/thread.h>

class Client
{
    Client(const Client&);
    Client& operator=(const Client&);

public:

    enum state
    {
        OK,
        FAILED,
        INIT,
        CLOSED
    };

    /**
     * @brief Create a new client
     *
     * @param connection The connection handle for this client
     */
    Client(MHD_Connection *connection):
        m_connection(connection),
        m_state(INIT)
    {
    }

    ~Client()
    {
    }

    /**
     * @brief Process a client request
     *
     * This function can be called multiple times if a PUT/POST/PATCH
     * uploads a large amount of data.
     *
     * @param url    Requested URL
     * @param method Request method
     * @param data   Pointer to request data
     * @param size   Size of request data
     *
     * @return MHD_YES on success, MHD_NO on error
     */
    int process(std::string url, std::string method, const char* data, size_t *size);

    /**
     * @brief Authenticate the client
     *
     * @param connection The MHD connection object
     * @param url        Requested URL
     * @param method     The request method
     *
     * @return True if authentication was successful
     */
    bool auth(MHD_Connection* connection, const char* url, const char* method);

    /**
     * Get client state
     *
     * @return The client state
     */
    state get_state() const
    {
        return m_state;
    }

    /**
     * Close the client connection
     *
     * All further requests will be rejected immediately
     */
    void close()
    {
        m_state = CLOSED;
    }

private:
    MHD_Connection* m_connection; /**< Connection handle */
    std::string     m_data;       /**< Uploaded data */
    state           m_state;      /**< Client state */
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
 * @brief Check if admin interface uses HTTPS protocol
 *
 * @return True if HTTPS is enabled
 */
bool mxs_admin_https_enabled();
