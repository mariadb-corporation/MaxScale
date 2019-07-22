/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>
#include "server.hh"

/**
 * This class contains internal server management functions that should not be exposed in the public
 * server class.
 */
class ServerManager
{
public:

    /**
     * @brief Allocate a new server
     *
     * This will create a new server that represents a backend server that services
     * can use. This function will add the server to the running configuration but
     * will not persist the changes.
     *
     * @param name   Unique server name
     * @param params Parameters for the server
     *
     * @return       The newly created server or NULL if an error occurred
     */
    static Server* create_server(const char* name, const MXS_CONFIG_PARAMETER& params);

    /**
     * Deallocate the server.
     *
     * @param server The server to deallocate
     */
    static void server_free(Server* server);

    /**
     * Find a server by name.
     *
     * @param name Name of the server
     * @return The server or NULL if not found
     */
    static Server* find_by_unique_name(const std::string& name);

    /**
     * Print all servers
     *
     * Designed to be called within a debugger session in order
     * to display all active servers within the gateway
     */
    static void printAllServers();

    /**
     * Print all servers to a DCB
     *
     * Designed to be called within a debugger session in order
     * to display all active servers within the gateway
     */
    static void dprintAllServers(DCB* dcb);

    /**
     * Print all servers in Json format to a DCB
     */
    static void dprintAllServersJson(DCB* dcb);

    /**
     * List all servers in a tabular form to a DCB
     *
     */
    static void dListServers(DCB* dcb);
    static std::unique_ptr<ResultSet> getList();

    /**
     * Convert all servers into JSON format
     *
     * @param host    Hostname of this server
     * @return JSON array of servers or NULL if an error occurred
     */
    static json_t* server_list_to_json(const char* host);
};
