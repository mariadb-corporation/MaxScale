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
#include <microhttpd.h>

#include <maxscale/thread.h>

using std::string;

class Client
{
public:

    /**
     * @brief Create a new client
     *
     * @param connection The connection handle for this client
     */
    Client(struct MHD_Connection *connection):
        m_connection(connection)
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
    int process(string url, string method, const char* data, size_t *size);

private:
    struct MHD_Connection* m_connection; /**< Connection handle */
    string                 m_data;       /**< Uploaded data */
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
