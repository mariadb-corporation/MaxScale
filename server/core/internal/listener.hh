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

#include <maxscale/listener.hh>

/**
 * @brief Serialize a listener to a file
 *
 * This converts @c listener into an INI format file. This allows created listeners
 * to be persisted to disk. This will replace any existing files with the same
 * name.
 *
 * @param listener Listener to serialize
 * @return True if the serialization of the listener was successful, false if it fails
 */
bool listener_serialize(const SListener& listener);

/**
 * Find a listener
 *
 * @param name Name of the listener
 *
 * @return The listener if it exists or an empty SListener if it doesn't
 */
SListener listener_find(const std::string& name);

/**
 * Find listener by socket
 *
 * @param socket  Path to a socket file
 *
 * @return The matching listener if one was found
 */
SListener listener_find_by_socket(const std::string& socket);

/**
 * Find listener by address and port
 *
 * @param address Network address
 * @param port    Network port
 *
 * @return The matching listener if one was found
 */
SListener listener_find_by_address(const std::string& address, unsigned short port);

/**
 * Get common listener parameter definitions.
 *
 * @return Listener parameters
 */
const MXS_MODULE_PARAM* common_listener_params();
