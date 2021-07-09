/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
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
    static Server* create_server(const char* name, const mxs::ConfigParameters& params);
    static Server* create_server(const char* name, json_t* json);

    /**
     * Deallocate the server.
     *
     * @param server The server to deallocate
     */
    static void server_free(Server* server);

    /**
     * Destroys all created servers
     */
    static void destroy_all();

    /**
     * Find a server by name.
     *
     * @param name Name of the server
     * @return The server or NULL if not found
     */
    static Server* find_by_unique_name(const std::string& name);

    /**
     * Find a server by address and port
     *
     * @param address The network address
     * @param port    The network port
     *
     * @return The server that exists at this address or NULL if no server is found
     */
    static Server* find_by_address(const std::string& address, uint16_t port);

    /**
     * Convert all servers into JSON format
     *
     * @param host    Hostname of this server
     * @return JSON array of servers or NULL if an error occurred
     */
    static json_t* server_list_to_json(const char* host);

    /**
     * Convert a server to JSON format
     *
     * @param host Hostname of this server as given in request
     * @return JSON representation of server or NULL if an error occurred
     */
    static json_t* server_to_json_resource(const Server* server, const char* host);

    /**
     * Set whether servers with duplicate host/port combinations are allowed
     *
     * @param value Whether to allow duplicate servers
     */
    static void set_allow_duplicates(bool value);

private:
    static json_t* server_to_json_data_relations(const Server* server, const char* host);
    static json_t* server_to_json_attributes(const Server* server);
};
