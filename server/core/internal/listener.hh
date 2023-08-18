/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
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

class ListenerManager
{
public:
    using SListener = std::shared_ptr<Listener>;

    template<class Params>
    SListener create(const std::string& name, Params params);

    void                   destroy_instances();
    void                   remove(const SListener& listener);
    json_t*                to_json_collection(const char* host);
    SListener              find(const std::string& name);
    std::vector<SListener> find_by_service(const SERVICE* service);
    void                   stop_all();
    bool                   reload_tls();

private:
    std::list<SListener> m_listeners;
    std::mutex           m_lock;

    bool listener_is_duplicate(const SListener& listener);
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
 * Find all listeners that point to a service
 *
 * @param service Service whose listeners are returned
 *
 * @return The listeners that point to the service
 */
std::vector<std::shared_ptr<Listener>> listener_find_by_service(const SERVICE* service);

void listener_destroy_instances();
