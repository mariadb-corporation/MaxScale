/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/listener.hh>

#include <set>
#include <maxscale/config2.hh>

class Service;

/**
 * Find a listener
 *
 * @param name Name of the listener
 *
 * @return The listener if it exists or an empty SListener if it doesn't
 */
std::shared_ptr<Listener> listener_find(const std::string& name);

/**
 * Find all listeners that point to a service
 *
 * @param service Service whose listeners are returned
 *
 * @return The listeners that point to the service
 */
std::vector<std::shared_ptr<Listener>> listener_find_by_service(const SERVICE* service);

void listener_destroy_instances();
