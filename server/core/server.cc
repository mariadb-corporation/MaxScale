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

/**
 * @file server.c  - A representation of a backend server within the gateway.
 *
 */

#include "internal/server.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string>
#include <list>
#include <mutex>
#include <sstream>
#include <mutex>

#include <maxbase/atomic.hh>
#include <maxbase/stopwatch.hh>

#include <maxscale/config.h>
#include <maxscale/service.h>
#include <maxscale/session.h>
#include <maxscale/dcb.h>
#include <maxscale/poll.h>
#include <maxscale/log.h>
#include <maxscale/ssl.h>
#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include <maxscale/utils.h>
#include <maxscale/json_api.h>
#include <maxscale/clock.h>
#include <maxscale/http.hh>
#include <maxscale/maxscale.h>
#include <maxscale/server.hh>
#include <maxscale/routingworker.hh>

#include "internal/monitor.h"
#include "internal/poll.hh"
#include "internal/config.hh"
#include "internal/service.hh"
#include "internal/modules.h"


using maxbase::Worker;
using maxscale::RoutingWorker;

using std::string;
using Guard = std::lock_guard<std::mutex>;

/** The latin1 charset */
#define SERVER_DEFAULT_CHARSET 0x08

const char CN_MONITORPW[] = "monitorpw";
const char CN_MONITORUSER[] = "monitoruser";
const char CN_PERSISTMAXTIME[] = "persistmaxtime";
const char CN_PERSISTPOOLMAX[] = "persistpoolmax";
const char CN_PROXY_PROTOCOL[] = "proxy_protocol";

static std::mutex server_lock;
static std::list<Server*> all_servers;
static const char ERR_CANNOT_MODIFY[] = "The server is monitored, so only the maintenance status can be "
                                        "set/cleared manually. Status was not modified.";
static const char WRN_REQUEST_OVERWRITTEN[] = "Previous maintenance request was not yet read by the monitor "
                                              "and was overwritten.";
static void server_parameter_free(SERVER_PARAM* tofree);


SERVER* server_alloc(const char* name, MXS_CONFIG_PARAMETER* params)
{
    const char* monuser = config_get_string(params, CN_MONITORUSER);
    const char* monpw = config_get_string(params, CN_MONITORPW);

    if ((*monuser != '\0') != (*monpw != '\0'))
    {
        MXS_ERROR("Both '%s' and '%s' need to be defined for server '%s'",
                  CN_MONITORUSER,
                  CN_MONITORPW,
                  name);
        return NULL;
    }

    const char* protocol = config_get_string(params, CN_PROTOCOL);
    const char* authenticator = config_get_string(params, CN_AUTHENTICATOR);

    if (!authenticator[0] && !(authenticator = get_default_authenticator(protocol)))
    {
        MXS_ERROR("No authenticator defined for server '%s' and no default "
                  "authenticator for protocol '%s'.",
                  name,
                  protocol);
        return NULL;
    }

    void* auth_instance = NULL;
    // Backend authenticators do not have options.
    if (!authenticator_init(&auth_instance, authenticator, NULL))
    {
        MXS_ERROR("Failed to initialize authenticator module '%s' for server '%s' ",
                  authenticator,
                  name);
        return NULL;
    }

    SSL_LISTENER* ssl = NULL;

    if (!config_create_ssl(name, params, false, &ssl))
    {
        MXS_ERROR("Unable to initialize SSL for server '%s'", name);
        return NULL;
    }

    Server* server = new(std::nothrow) Server;
    char* my_name = MXS_STRDUP(name);
    char* my_protocol = MXS_STRDUP(protocol);
    char* my_authenticator = MXS_STRDUP(authenticator);
    DCB** persistent = (DCB**)MXS_CALLOC(config_threadcount(), sizeof(*persistent));

    if (!server || !my_name || !my_protocol || !my_authenticator || !persistent)
    {
        delete server;
        MXS_FREE(my_name);
        MXS_FREE(persistent);
        MXS_FREE(my_protocol);
        MXS_FREE(my_authenticator);
        SSL_LISTENER_free(ssl);
        return NULL;
    }

    const char* address = config_get_string(params, CN_ADDRESS);

    if (snprintf(server->address, sizeof(server->address), "%s", address) > (int)sizeof(server->address))
    {
        MXS_WARNING("Truncated server address '%s' to the maximum size of %lu characters.",
                    address,
                    sizeof(server->address));
    }

    server->name = my_name;
    server->port = config_get_integer(params, CN_PORT);
    server->protocol = my_protocol;
    server->authenticator = my_authenticator;
    server->monuser[0] = '\0';
    server->monpw[0] = '\0';
    server->persistpoolmax = config_get_integer(params, CN_PERSISTPOOLMAX);
    server->persistmaxtime = config_get_integer(params, CN_PERSISTMAXTIME);
    server->proxy_protocol = config_get_bool(params, CN_PROXY_PROTOCOL);
    server->parameters = NULL;
    server->is_active = true;
    server->auth_instance = auth_instance;
    server->server_ssl = ssl;
    server->persistent = persistent;
    server->charset = SERVER_DEFAULT_CHARSET;
    memset(&server->stats, 0, sizeof(server->stats));
    server->persistmax = 0;
    server->last_event = SERVER_UP_EVENT;
    server->triggered_at = 0;
    server->status = SERVER_RUNNING;
    server->maint_request = MAINTENANCE_NO_CHANGE;
    memset(server->version_string, '\0', MAX_SERVER_VERSION_LEN);
    server->version = 0;
    server->server_type = SERVER_TYPE_MARIADB;
    server->node_id = -1;
    server->rlag = MXS_RLAG_UNDEFINED;
    server->node_ts = 0;
    server->master_id = -1;
    server->master_err_is_logged = false;
    server->warn_ssl_not_enabled = true;
    server->disk_space_threshold = NULL;

    if (*monuser && *monpw)
    {
        server_add_mon_user(server, monuser, monpw);
    }

    for (MXS_CONFIG_PARAMETER* p = params; p; p = p->next)
    {
        server_set_parameter(server, p->name, p->value);
    }

    Guard guard(server_lock);
    // This keeps the order of the servers the same as in 2.2
    all_servers.push_front(server);

    return server;
}


/**
 * Deallocate the specified server
 *
 * @param server        The service to deallocate
 * @return Returns true if the server was freed
 */
void server_free(Server* server)
{
    mxb_assert(server);

    {
        Guard guard(server_lock);
        auto it = std::find(all_servers.begin(), all_servers.end(), server);
        mxb_assert(it != all_servers.end());
        all_servers.erase(it);
    }

    /* Clean up session and free the memory */
    MXS_FREE(server->protocol);
    MXS_FREE(server->name);
    MXS_FREE(server->authenticator);
    server_parameter_free(server->parameters);

    if (server->persistent)
    {
        int nthr = config_threadcount();

        for (int i = 0; i < nthr; i++)
        {
            dcb_persistent_clean_count(server->persistent[i], i, true);
        }
        MXS_FREE(server->persistent);
    }

    delete server->disk_space_threshold;
    delete server;
}

/**
 * Get a DCB from the persistent connection pool, if possible
 *
 * @param server      The server to set the name on
 * @param user        The name of the user needing the connection
 * @param ip          Client IP address
 * @param protocol    The name of the protocol needed for the connection
 * @param id          Thread ID
 *
 * @return A DCB or NULL if no connection is found
 */
DCB* server_get_persistent(SERVER* server, const char* user, const char* ip, const char* protocol, int id)
{
    DCB* dcb, * previous = NULL;

    if (server->persistent[id]
        && dcb_persistent_clean_count(server->persistent[id], id, false)
        && server->persistent[id]   // Check after cleaning
        && (server->status & SERVER_RUNNING))
    {
        dcb = server->persistent[id];
        while (dcb)
        {
            if (dcb->user
                && dcb->protoname
                && dcb->remote
                && ip
                && !dcb->dcb_errhandle_called
                && !(dcb->flags & DCBF_HUNG)
                && 0 == strcmp(dcb->user, user)
                && 0 == strcmp(dcb->remote, ip)
                && 0 == strcmp(dcb->protoname, protocol))
            {
                if (NULL == previous)
                {
                    server->persistent[id] = dcb->nextpersistent;
                }
                else
                {
                    previous->nextpersistent = dcb->nextpersistent;
                }
                MXS_FREE(dcb->user);
                dcb->user = NULL;
                mxb::atomic::add(&server->stats.n_persistent, -1);
                mxb::atomic::add(&server->stats.n_current, 1, mxb::atomic::RELAXED);
                return dcb;
            }
            else
            {
                MXS_DEBUG("%lu [server_get_persistent] Rejected dcb "
                          "%p from pool, user %s looking for %s, protocol %s "
                          "looking for %s, hung flag %s, error handle called %s.",
                          pthread_self(),
                          dcb,
                          dcb->user ? dcb->user : "NULL",
                          user,
                          dcb->protoname ? dcb->protoname : "NULL",
                          protocol,
                          (dcb->flags & DCBF_HUNG) ? "true" : "false",
                          dcb->dcb_errhandle_called ? "true" : "false");
            }
            previous = dcb;
            dcb = dcb->nextpersistent;
        }
    }
    return NULL;
}

/**
 * @brief Find a server with the specified name
 *
 * @param name Name of the server
 * @return The server or NULL if not found
 */
SERVER* server_find_by_unique_name(const char* name)
{
    Guard guard(server_lock);

    for (Server* server : all_servers)
    {
        if (server->is_active && strcmp(server->name, name) == 0)
        {
            return server;
        }
    }

    return nullptr;
}

/**
 * Find several servers with the names specified in an array with a given size.
 * The returned array (but not the elements) should be freed by the caller.
 * If no valid server names were found or in case of error, nothing is written
 * to the output parameter.
 *
 * @param servers An array of server names
 * @param size Number of elements in the input server names array, equal to output
 * size if any servers are found.
 * @param output Where to save the output. Contains null elements for invalid server
 * names. If all were invalid, the output is left untouched.
 * @return Number of valid server names found
 */
int server_find_by_unique_names(char** server_names, int size, SERVER*** output)
{
    mxb_assert(server_names && (size > 0));

    SERVER** results = (SERVER**)MXS_CALLOC(size, sizeof(SERVER*));
    if (!results)
    {
        return 0;
    }

    int found = 0;
    for (int i = 0; i < size; i++)
    {
        results[i] = server_find_by_unique_name(server_names[i]);
        found += (results[i]) ? 1 : 0;
    }

    if (found)
    {
        *output = results;
    }
    else
    {
        MXS_FREE(results);
    }
    return found;
}

/**
 * Find an existing server
 *
 * @param       servname        The Server name or address
 * @param       port            The server port
 * @return      The server or NULL if not found
 */
SERVER* server_find(const char* servname, unsigned short port)
{
    Guard guard(server_lock);

    for (Server* server : all_servers)
    {
        if (server->is_active && strcmp(server->address, servname) == 0 && server->port == port)
        {
            return server;
        }
    }

    return nullptr;
}

/**
 * Print details of an individual server
 *
 * @param server        Server to print
 */
void printServer(const SERVER* server)
{
    printf("Server %p\n", server);
    printf("\tServer:                       %s\n", server->address);
    printf("\tProtocol:             %s\n", server->protocol);
    printf("\tPort:                 %d\n", server->port);
    printf("\tTotal connections:    %d\n", server->stats.n_connections);
    printf("\tCurrent connections:  %d\n", server->stats.n_current);
    printf("\tPersistent connections:       %d\n", server->stats.n_persistent);
    printf("\tPersistent actual max:        %d\n", server->persistmax);
}

/**
 * Print all servers
 *
 * Designed to be called within a debugger session in order
 * to display all active servers within the gateway
 */
void printAllServers()
{
    Guard guard(server_lock);

    for (Server* server : all_servers)
    {
        if (server->is_active)
        {
            printServer(server);
        }
    }
}

/**
 * Print all servers to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active servers within the gateway
 */
void dprintAllServers(DCB* dcb)
{
    Guard guard(server_lock);

    for (Server* server : all_servers)
    {
        if (server->is_active)
        {
            dprintServer(dcb, server);
        }
    }
}

/**
 * Print all servers in Json format to a DCB
 */
void dprintAllServersJson(DCB* dcb)
{
    json_t* all_servers_json = server_list_to_json("");
    char* dump = json_dumps(all_servers_json, JSON_INDENT(4));
    dcb_printf(dcb, "%s", dump);
    MXS_FREE(dump);
    json_decref(all_servers_json);
}

/**
 * A class for cleaning up persistent connections
 */
class CleanupTask : public Worker::Task
{
public:
    CleanupTask(const SERVER* server)
        : m_server(server)
    {
    }

    void execute(Worker& worker)
    {
        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);
        mxb_assert(&rworker == RoutingWorker::get_current());

        int thread_id = rworker.id();
        dcb_persistent_clean_count(m_server->persistent[thread_id], thread_id, false);
    }

private:
    const SERVER* m_server;     /**< Server to clean up */
};

/**
 * @brief Clean up any stale persistent connections
 *
 * This function purges any stale persistent connections from @c server.
 *
 * @param server Server to clean up
 */
static void cleanup_persistent_connections(const SERVER* server)
{
    CleanupTask task(server);
    RoutingWorker::execute_concurrently(task);
}

/**
 * Print server details to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active servers within the gateway
 */
void dprintServer(DCB* dcb, const SERVER* srv)
{
    if (!server_is_active(srv))
    {
        return;
    }

    const Server* server = static_cast<const Server*>(srv);

    dcb_printf(dcb, "Server %p (%s)\n", server, server->name);
    dcb_printf(dcb, "\tServer:                              %s\n", server->address);
    char* stat = server_status(server);
    dcb_printf(dcb, "\tStatus:                              %s\n", stat);
    MXS_FREE(stat);
    dcb_printf(dcb, "\tProtocol:                            %s\n", server->protocol);
    dcb_printf(dcb, "\tPort:                                %d\n", server->port);
    dcb_printf(dcb, "\tServer Version:                      %s\n", server->version_string);
    dcb_printf(dcb, "\tNode Id:                             %ld\n", server->node_id);
    dcb_printf(dcb, "\tMaster Id:                           %ld\n", server->master_id);
    dcb_printf(dcb,
               "\tLast event:                          %s\n",
               mon_get_event_name((mxs_monitor_event_t)server->last_event));
    time_t t = maxscale_started() + MXS_CLOCK_TO_SEC(server->triggered_at);
    dcb_printf(dcb, "\tTriggered at:                        %s\n", http_to_date(t).c_str());
    if (server_is_slave(server) || server_is_relay(server))
    {
        if (server->rlag >= 0)
        {
            dcb_printf(dcb, "\tSlave delay:                         %d\n", server->rlag);
        }
    }
    if (server->node_ts > 0)
    {
        struct tm result;
        char buf[40];
        dcb_printf(dcb,
                   "\tLast Repl Heartbeat:                 %s",
                   asctime_r(localtime_r((time_t*)(&server->node_ts), &result), buf));
    }
    SERVER_PARAM* param;
    if ((param = server->parameters))
    {
        dcb_printf(dcb, "\tServer Parameters:\n");
        while (param)
        {
            if (param->active)
            {
                dcb_printf(dcb,
                           "\t                                       %s\t%s\n",
                           param->name,
                           param->value);
            }
            param = param->next;
        }
    }
    dcb_printf(dcb, "\tNumber of connections:               %d\n", server->stats.n_connections);
    dcb_printf(dcb, "\tCurrent no. of conns:                %d\n", server->stats.n_current);
    dcb_printf(dcb, "\tCurrent no. of operations:           %d\n", server->stats.n_current_ops);
    dcb_printf(dcb, "\tNumber of routed packets:            %lu\n", server->stats.packets);
    std::ostringstream ave_os;
    if (server_response_time_num_samples(server))
    {
        maxbase::Duration dur(server_response_time_average(server));
        ave_os << dur;
    }
    else
    {
        ave_os << "not available";
    }
    dcb_printf(dcb, "\tAverage response time:               %s\n", ave_os.str().c_str());

    if (server->persistpoolmax)
    {
        dcb_printf(dcb, "\tPersistent pool size:                %d\n", server->stats.n_persistent);
        cleanup_persistent_connections(server);
        dcb_printf(dcb, "\tPersistent measured pool size:       %d\n", server->stats.n_persistent);
        dcb_printf(dcb, "\tPersistent actual size max:          %d\n", server->persistmax);
        dcb_printf(dcb, "\tPersistent pool size limit:          %ld\n", server->persistpoolmax);
        dcb_printf(dcb, "\tPersistent max time (secs):          %ld\n", server->persistmaxtime);
        dcb_printf(dcb, "\tConnections taken from pool:         %lu\n", server->stats.n_from_pool);
        double d = (double)server->stats.n_from_pool / (double)(server->stats.n_connections
                                                                + server->stats.n_from_pool + 1);
        dcb_printf(dcb, "\tPool availability:                   %0.2lf%%\n", d * 100.0);
    }
    if (server->server_ssl)
    {
        SSL_LISTENER* l = server->server_ssl;
        dcb_printf(dcb,
                   "\tSSL initialized:                     %s\n",
                   l->ssl_init_done ? "yes" : "no");
        dcb_printf(dcb,
                   "\tSSL method type:                     %s\n",
                   ssl_method_type_to_string(l->ssl_method_type));
        dcb_printf(dcb, "\tSSL certificate verification depth:  %d\n", l->ssl_cert_verify_depth);
        dcb_printf(dcb, "\tSSL peer verification :  %s\n", l->ssl_verify_peer_certificate ? "true" : "false");
        dcb_printf(dcb,
                   "\tSSL certificate:                     %s\n",
                   l->ssl_cert ? l->ssl_cert : "null");
        dcb_printf(dcb,
                   "\tSSL key:                             %s\n",
                   l->ssl_key ? l->ssl_key : "null");
        dcb_printf(dcb,
                   "\tSSL CA certificate:                  %s\n",
                   l->ssl_ca_cert ? l->ssl_ca_cert : "null");
    }
    if (server->proxy_protocol)
    {
        dcb_printf(dcb, "\tPROXY protocol:                      on.\n");
    }
}

/**
 * Diagnostic to print number of DCBs in persistent pool for a server
 *
 * @param       pdcb    DCB to print results to
 * @param       server  SERVER for which DCBs are to be printed
 */
void dprintPersistentDCBs(DCB* pdcb, const SERVER* server)
{
    dcb_printf(pdcb, "Number of persistent DCBs: %d\n", server->stats.n_persistent);
}

/**
 * List all servers in a tabular form to a DCB
 *
 */
void dListServers(DCB* dcb)
{
    Guard guard(server_lock);
    bool have_servers = std::any_of(all_servers.begin(),
                                    all_servers.end(),
                                    [](Server* s) {
                                        return s->is_active;
                                    });

    if (have_servers)
    {
        dcb_printf(dcb, "Servers.\n");
        dcb_printf(dcb, "-------------------+-----------------+-------+-------------+--------------------\n");
        dcb_printf(dcb,
                   "%-18s | %-15s | Port  | Connections | %-20s\n",
                   "Server",
                   "Address",
                   "Status");
        dcb_printf(dcb, "-------------------+-----------------+-------+-------------+--------------------\n");

        for (Server* server : all_servers)
        {
            if (server->is_active)
            {
                char* stat = server_status(server);
                dcb_printf(dcb,
                           "%-18s | %-15s | %5d | %11d | %s\n",
                           server->name,
                           server->address,
                           server->port,
                           server->stats.n_current,
                           stat);
                MXS_FREE(stat);
            }
        }

        dcb_printf(dcb, "-------------------+-----------------+-------+-------------+--------------------\n");
    }
}

/**
 * Convert a set of  server status flags to a string, the returned
 * string has been malloc'd and must be free'd by the caller
 *
 * @param server The server to return the status of
 * @return A string representation of the status flags
 */
char* server_status(const SERVER* server)
{
    mxb_assert(server);
    uint64_t server_status = server->status;

    string result;
    string separator;

    // Helper function.
    auto concatenate_if = [&result, &separator](bool condition, const string& desc) {
            if (condition)
            {
                result += separator + desc;
                separator = ", ";
            }
        };

    // TODO: The following values should be revisited at some point, but since they are printed by
    // the REST API they should not be changed suddenly. Strictly speaking, even the combinations
    // should not change, but this is more dependant on the monitors and have already changed.
    const string maintenance = "Maintenance";
    const string master = "Master";
    const string relay = "Relay Master";
    const string slave = "Slave";
    const string synced = "Synced";
    const string ndb = "NDB";
    const string slave_ext = "Slave of External Server";
    const string sticky = "Master Stickiness";
    const string auth_err = "Auth Error";
    const string running = "Running";
    const string down = "Down";

    // Maintenance is usually set by user so is printed first.
    concatenate_if(status_is_in_maint(server_status), maintenance);
    // Master cannot be a relay or a slave.
    if (status_is_master(server_status))
    {
        concatenate_if(true, master);
    }
    else
    {
        // Relays are typically slaves as well. The binlog server may be an exception.
        concatenate_if(status_is_relay(server_status), relay);
        concatenate_if(status_is_slave(server_status), slave);
    }

    // The following Galera and Cluster bits may be combined with master/slave.
    concatenate_if(status_is_joined(server_status), synced);
    concatenate_if(status_is_ndb(server_status), ndb);
    // May be combined with other MariaDB monitor flags.
    concatenate_if(server_status & SERVER_SLAVE_OF_EXT_MASTER, slave_ext);

    // Should this be printed only if server is master?
    concatenate_if(server_status & SERVER_MASTER_STICKINESS, sticky);

    concatenate_if(server_status & SERVER_AUTH_ERROR, auth_err);
    concatenate_if(status_is_running(server_status), running);
    concatenate_if(status_is_down(server_status), down);

    return MXS_STRDUP(result.c_str());
}

/**
 * Set a status bit in the server without locking
 *
 * @param server        The server to update
 * @param bit           The bit to set for the server
 */
void server_set_status_nolock(SERVER* server, uint64_t bit)
{
    server->status |= bit;

    /** clear error logged flag before the next failure */
    if (server_is_master(server))
    {
        server->master_err_is_logged = false;
    }
}

/**
 * Clears and sets specified bits.
 *
 * @attention This function does no locking
 *
 * @param server         The server to update
 * @param bits_to_clear  The bits to clear for the server.
 * @param bits_to_set    The bits to set for the server.
 */
void server_clear_set_status_nolock(SERVER* server, uint64_t bits_to_clear, uint64_t bits_to_set)
{
    /** clear error logged flag before the next failure */
    if ((bits_to_set & SERVER_MASTER) && ((server->status & SERVER_MASTER) == 0))
    {
        server->master_err_is_logged = false;
    }

    if ((server->status & bits_to_clear) != bits_to_set)
    {
        server->status = (server->status & ~bits_to_clear) | bits_to_set;
    }
}

/**
 * Clear a status bit in the server without locking
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
void server_clear_status_nolock(SERVER* server, uint64_t bit)
{
    server->status &= ~bit;
}

/**
 * Transfer status bitstring from one server to another
 *
 * @attention This function does no locking
 *
 * @param dest_server       The server to be updated
 * @param source_server         The server to provide the new bit string
 */
void server_transfer_status(SERVER* dest_server, const SERVER* source_server)
{
    dest_server->status = source_server->status;
}

/**
 * Add a user name and password to use for monitoring the
 * state of the server.
 *
 * @param server        The server to update
 * @param user          The user name to use
 * @param passwd        The password of the user
 */
void server_add_mon_user(SERVER* server, const char* user, const char* passwd)
{
    if (user != server->monuser
        && snprintf(server->monuser, sizeof(server->monuser), "%s", user) > (int)sizeof(server->monuser))
    {
        MXS_WARNING("Truncated monitor user for server '%s', maximum username "
                    "length is %lu characters.",
                    server->name,
                    sizeof(server->monuser));
    }

    if (passwd != server->monpw
        && snprintf(server->monpw, sizeof(server->monpw), "%s", passwd) > (int)sizeof(server->monpw))
    {
        MXS_WARNING("Truncated monitor password for server '%s', maximum password "
                    "length is %lu characters.",
                    server->name,
                    sizeof(server->monpw));
    }
}

/**
 * Check and update a server definition following a configuration
 * update. Changes will not affect any current connections to this
 * server, however all new connections will use the new settings.
 *
 * If the new settings are different from those already applied to the
 * server then a message will be written to the log.
 *
 * @param server        The server to update
 * @param protocol      The new protocol for the server
 * @param user          The monitor user for the server
 * @param passwd        The password to use for the monitor user
 */
void server_update_credentials(SERVER* server, const char* user, const char* passwd)
{
    if (user != NULL && passwd != NULL)
    {
        server_add_mon_user(server, user, passwd);
    }
}

static SERVER_PARAM* allocate_parameter(const char* name, const char* value)
{
    char* my_name = MXS_STRDUP(name);
    char* my_value = MXS_STRDUP(value);

    SERVER_PARAM* param = (SERVER_PARAM*)MXS_MALLOC(sizeof(SERVER_PARAM));

    if (!my_name || !my_value || !param)
    {
        MXS_FREE(my_name);
        MXS_FREE(my_value);
        MXS_FREE(param);
        return NULL;
    }

    param->active = true;
    param->name = my_name;
    param->value = my_value;

    return param;
}

bool server_remove_parameter(SERVER* srv, const char* name)
{
    Server* server = static_cast<Server*>(srv);
    bool rval = false;
    std::lock_guard<std::mutex> guard(server->m_lock);

    for (SERVER_PARAM* p = server->parameters; p; p = p->next)
    {
        if (strcmp(p->name, name) == 0 && p->active)
        {
            p->active = false;
            rval = true;
            break;
        }
    }

    return rval;
}

void server_set_parameter(SERVER* srv, const char* name, const char* value)
{
    Server* server = static_cast<Server*>(srv);
    SERVER_PARAM* param = allocate_parameter(name, value);

    if (param)
    {
        std::lock_guard<std::mutex> guard(server->m_lock);

        // Insert new value
        param->next = server->parameters;
        server->parameters = param;

        // Mark old value, if found, as inactive
        for (SERVER_PARAM* p = server->parameters->next; p; p = p->next)
        {
            if (strcmp(p->name, name) == 0 && p->active)
            {
                p->active = false;
                break;
            }
        }
    }
}

/**
 * Free a list of server parameters
 * @param tofree Parameter list to free
 */
static void server_parameter_free(SERVER_PARAM* tofree)
{
    SERVER_PARAM* param;

    if (tofree)
    {
        param = tofree;
        tofree = tofree->next;
        MXS_FREE(param->name);
        MXS_FREE(param->value);
        MXS_FREE(param);
    }
}

/**
 * Same as server_get_parameter but doesn't lock the server
 *
 * @note Should only be called when the server is already locked
 */
static size_t server_get_parameter_nolock(const SERVER* server, const char* name, char* out, size_t size)
{
    size_t len = 0;
    SERVER_PARAM* param = server->parameters;

    while (param)
    {
        if (strcmp(param->name, name) == 0 && param->active)
        {
            len = snprintf(out, out ? size : 0, "%s", param->value);
            break;
        }
        param = param->next;
    }

    return len;
}

/**
 * Retrieve a parameter value from a server
 *
 * @param server The server we are looking for a parameter of
 * @param name   The name of the parameter we require
 * @param out    Buffer where value is stored, use NULL to check if the parameter exists
 * @param size   Size of @c out, ignored if @c out is NULL
 *
 * @return Length of the parameter value or 0 if parameter was not found
 */
size_t server_get_parameter(const SERVER* srv, const char* name, char* out, size_t size)
{
    const Server* server = static_cast<const Server*>(srv);
    std::lock_guard<std::mutex> guard(server->m_lock);
    return server_get_parameter_nolock(server, name, out, size);
}

/**
 * Return a resultset that has the current set of servers in it
 *
 * @return A Result set
 */
std::unique_ptr<ResultSet> serverGetList()
{
    std::unique_ptr<ResultSet> set =
        ResultSet::create({"Server", "Address", "Port", "Connections", "Status"});
    Guard guard(server_lock);

    for (Server* server : all_servers)
    {
        if (server_is_active(server))
        {
            char* stat = server_status(server);
            set->add_row({server->name, server->address, std::to_string(server->port),
                          std::to_string(server->stats.n_current), stat});
            MXS_FREE(stat);
        }
    }

    return set;
}

/*
 * Update the address value of a specific server
 *
 * @param server        The server to update
 * @param address       The new address
 *
 */
void server_update_address(SERVER* server, const char* address)
{
    Guard guard(server_lock);

    if (server && address)
    {
        strcpy(server->address, address);
    }
}

/*
 * Update the port value of a specific server
 *
 * @param server        The server to update
 * @param port          The new port value
 *
 */
void server_update_port(SERVER* server, unsigned short port)
{
    Guard guard(server_lock);

    if (server && port > 0)
    {
        server->port = port;
    }
}

static struct
{
    const char* str;
    uint64_t    bit;
} ServerBits[] =
{
    {"running",     SERVER_RUNNING   },
    {"master",      SERVER_MASTER    },
    {"slave",       SERVER_SLAVE     },
    {"synced",      SERVER_JOINED    },
    {"ndb",         SERVER_NDB       },
    {"maintenance", SERVER_MAINT     },
    {"maint",       SERVER_MAINT     },
    {"stale",       SERVER_WAS_MASTER},
    {NULL,          0                }
};

/**
 * Map the server status bit
 *
 * @param str   String representation
 * @return bit value or 0 on error
 */
uint64_t server_map_status(const char* str)
{
    int i;

    for (i = 0; ServerBits[i].str; i++)
    {
        if (!strcasecmp(str, ServerBits[i].str))
        {
            return ServerBits[i].bit;
        }
    }
    return 0;
}

/**
 * Set the version string of the server.
 *
 * @param server          Server to update
 * @param version_string  Version string
 */
void server_set_version_string(SERVER* server, const char* version_string)
{
    // There is a race here. The string may be accessed, while we are
    // updating it. Thus we take some precautions to ensure that the
    // string cannot be completely garbled at any point.

    size_t old_len = strlen(server->version_string);
    size_t new_len = strlen(version_string);

    if (new_len >= MAX_SERVER_VERSION_LEN)
    {
        new_len = MAX_SERVER_VERSION_LEN - 1;
    }

    if (new_len < old_len)
    {
        // If the new string is shorter, we start by nulling out the
        // excess data.
        memset(server->version_string + new_len, 0, old_len - new_len);
    }

    strncpy(server->version_string, version_string, new_len);
    // No null-byte needs to be set. The array starts out as all zeros
    // and the above memset adds the necessary null, should the new string
    // be shorter than the old.
}

/**
 * Set the version of the server.
 *
 * @param server         Server to update
 * @param version_string Human readable version string.
 * @param version        Version encoded as MariaDB encodes the version, i.e.:
 *                       version = major * 10000 + minor * 100 + patch
 */
void server_set_version(SERVER* server, const char* version_string, uint64_t version)
{
    server_set_version_string(server, version_string);

    atomic_store_uint64(&server->version, version);
}

uint64_t server_get_version(const SERVER* server)
{
    return atomic_load_uint64(&server->version);
}

namespace
{

// Converts SERVER_PARAM to MXS_CONFIG_PARAM and keeps them in the same order
class ParamAdaptor
{
public:
    ParamAdaptor(SERVER_PARAM* params)
    {
        for (auto p = params; p; p = p->next)
        {
            if (p->active)
            {
                // The current tail of the list
                auto it = m_params.begin();

                // Push the new tail
                m_params.push_front({p->name, p->value, nullptr});

                if (it != m_params.end())
                {
                    // Update the old tail to point to the new tail
                    it->next = &m_params.front();
                }
            }
        }
    }

    operator MXS_CONFIG_PARAMETER*()
    {
        // Return the head of the parameter list which is the tail of the internal list
        return m_params.empty() ? nullptr : &m_params.back();
    }

private:

    // Holds the temporary configuration objects. Needs to be a list so that
    // inserts into the container won't invalidate the next pointers
    std::list<MXS_CONFIG_PARAMETER> m_params;
};
}

/**
 * Creates a server configuration at the location pointed by @c filename
 *
 * @param server Server to serialize into a configuration
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
static bool create_server_config(const SERVER* server, const char* filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing server '%s': %d, %s",
                  filename,
                  server->name,
                  errno,
                  mxs_strerror(errno));
        return false;
    }

    // TODO: Check for return values on all of the dprintf calls
    dprintf(file, "[%s]\n", server->name);
    dprintf(file, "%s=server\n", CN_TYPE);

    const MXS_MODULE* mod = get_module(server->protocol, MODULE_PROTOCOL);
    dump_param_list(file,
                    ParamAdaptor(server->parameters),
                    {CN_TYPE},
                    config_server_params,
                    mod->parameters);

    std::unordered_set<std::string> known;

    for (auto a : {config_server_params, mod->parameters})
    {
        for (int i = 0; a[i].name; i++)
        {
            known.insert(a[i].name);
        }
    }

    for (auto p = server->parameters; p; p = p->next)
    {
        if (known.count(p->name) == 0 && p->active)
        {
            dprintf(file, "%s=%s\n", p->name, p->value);
        }
    }

    close(file);

    return true;
}

bool server_serialize(const SERVER* server)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename,
             sizeof(filename),
             "%s/%s.cnf.tmp",
             get_config_persistdir(),
             server->name);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary server configuration at '%s': %d, %s",
                  filename,
                  errno,
                  mxs_strerror(errno));
    }
    else if (create_server_config(server, filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char* dot = strrchr(final_filename, '.');
        mxb_assert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary server configuration at '%s': %d, %s",
                      filename,
                      errno,
                      mxs_strerror(errno));
        }
    }

    return rval;
}

/**
 * Set a status bit in the server under a lock. This ensures synchronization
 * with the server monitor thread. Calling this inside the monitor will likely
 * cause a deadlock. If the server is monitored, only set the pending bit.
 *
 * @param server        The server to update
 * @param bit           The bit to set for the server
 */
bool mxs::server_set_status(SERVER* srv, int bit, string* errmsg_out)
{
    Server* server = static_cast<Server*>(srv);
    bool written = false;
    /* First check if the server is monitored. This isn't done under a lock
     * but the race condition cannot cause significant harm. Monitors are never
     * freed so the pointer stays valid. */
    MXS_MONITOR* mon = monitor_server_in_use(server);
    std::lock_guard<std::mutex> guard(server->m_lock);

    if (mon && mon->state == MONITOR_STATE_RUNNING)
    {
        /* This server is monitored, in which case modifying any other status bit than Maintenance is
         * disallowed. Maintenance is set/cleared using a special variable which the monitor reads when
         * starting the next update cycle. Also set a flag so the next loop happens sooner. */
        if (bit & ~SERVER_MAINT)
        {
            MXS_ERROR(ERR_CANNOT_MODIFY);
            if (errmsg_out)
            {
                *errmsg_out = ERR_CANNOT_MODIFY;
            }
        }
        else if (bit & SERVER_MAINT)
        {
            // Warn if the previous request hasn't been read.
            int previous_request = atomic_exchange_int(&server->maint_request, MAINTENANCE_ON);
            written = true;
            if (previous_request != MAINTENANCE_NO_CHANGE)
            {
                MXS_WARNING(WRN_REQUEST_OVERWRITTEN);
            }
            atomic_store_int(&mon->check_maintenance_flag, MAINTENANCE_FLAG_CHECK);
        }
    }
    else
    {
        /* Set the bit directly */
        server_set_status_nolock(server, bit);
        written = true;
    }

    return written;
}
/**
 * Clear a status bit in the server under a lock. This ensures synchronization
 * with the server monitor thread. Calling this inside the monitor will likely
 * cause a deadlock. If the server is monitored, only clear the pending bit.
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
bool mxs::server_clear_status(SERVER* srv, int bit, string* errmsg_out)
{
    Server* server = static_cast<Server*>(srv);
    bool written = false;
    MXS_MONITOR* mon = monitor_server_in_use(server);
    std::lock_guard<std::mutex> guard(server->m_lock);

    if (mon && mon->state == MONITOR_STATE_RUNNING)
    {
        // See server_set_status().
        if (bit & ~SERVER_MAINT)
        {
            MXS_ERROR(ERR_CANNOT_MODIFY);
            if (errmsg_out)
            {
                *errmsg_out = ERR_CANNOT_MODIFY;
            }
        }
        else if (bit & SERVER_MAINT)
        {
            // Warn if the previous request hasn't been read.
            int previous_request = atomic_exchange_int(&server->maint_request, MAINTENANCE_OFF);
            written = true;
            if (previous_request != MAINTENANCE_NO_CHANGE)
            {
                MXS_WARNING(WRN_REQUEST_OVERWRITTEN);
            }
            atomic_store_int(&mon->check_maintenance_flag, MAINTENANCE_FLAG_CHECK);
        }
    }
    else
    {
        /* Clear bit directly */
        server_clear_status_nolock(server, bit);
        written = true;
    }

    return written;
}

bool server_is_mxs_service(const SERVER* server)
{
    bool rval = false;

    /** Do a coarse check for local server pointing to a MaxScale service */
    if (strcmp(server->address, "127.0.0.1") == 0
        || strcmp(server->address, "::1") == 0
        || strcmp(server->address, "localhost") == 0
        || strcmp(server->address, "localhost.localdomain") == 0)
    {
        if (service_port_is_used(server->port))
        {
            rval = true;
        }
    }

    return rval;
}

static json_t* server_json_attributes(const SERVER* server)
{
    /** Resource attributes */
    json_t* attr = json_object();

    /** Store server parameters in attributes */
    json_t* params = json_object();

    const MXS_MODULE* mod = get_module(server->protocol, MODULE_PROTOCOL);
    config_add_module_params_json(ParamAdaptor(server->parameters),
                                  {CN_TYPE},
                                  config_server_params,
                                  mod->parameters,
                                  params);

    // Add weighting parameters that weren't added by config_add_module_params_json
    for (SERVER_PARAM* p = server->parameters; p; p = p->next)
    {
        if (!json_object_get(params, p->name))
        {
            json_object_set_new(params, p->name, json_string(p->value));
        }
    }

    json_object_set_new(attr, CN_PARAMETERS, params);

    /** Store general information about the server state */
    char* stat = server_status(server);
    json_object_set_new(attr, CN_STATE, json_string(stat));
    MXS_FREE(stat);

    json_object_set_new(attr, CN_VERSION_STRING, json_string(server->version_string));

    json_object_set_new(attr, "node_id", json_integer(server->node_id));
    json_object_set_new(attr, "master_id", json_integer(server->master_id));

    const char* event_name = mon_get_event_name((mxs_monitor_event_t)server->last_event);
    time_t t = maxscale_started() + MXS_CLOCK_TO_SEC(server->triggered_at);
    json_object_set_new(attr, "last_event", json_string(event_name));
    json_object_set_new(attr, "triggered_at", json_string(http_to_date(t).c_str()));

    if (server->rlag >= 0)
    {
        json_object_set_new(attr, "replication_lag", json_integer(server->rlag));
    }

    if (server->node_ts > 0)
    {
        struct tm result;
        char timebuf[30];
        time_t tim = server->node_ts;
        asctime_r(localtime_r(&tim, &result), timebuf);
        trim(timebuf);

        json_object_set_new(attr, "last_heartbeat", json_string(timebuf));
    }

    /** Store statistics */
    json_t* stats = json_object();

    json_object_set_new(stats, "connections", json_integer(server->stats.n_current));
    json_object_set_new(stats, "total_connections", json_integer(server->stats.n_connections));
    json_object_set_new(stats, "persistent_connections", json_integer(server->stats.n_persistent));
    json_object_set_new(stats, "active_operations", json_integer(server->stats.n_current_ops));
    json_object_set_new(stats, "routed_packets", json_integer(server->stats.packets));

    json_object_set_new(attr, "statistics", stats);

    return attr;
}

static json_t* server_to_json_data(const SERVER* server, const char* host)
{
    json_t* rval = json_object();

    /** Add resource identifiers */
    json_object_set_new(rval, CN_ID, json_string(server->name));
    json_object_set_new(rval, CN_TYPE, json_string(CN_SERVERS));

    /** Relationships */
    json_t* rel = json_object();
    json_t* service_rel = service_relations_to_server(server, host);
    json_t* monitor_rel = monitor_relations_to_server(server, host);

    if (service_rel)
    {
        json_object_set_new(rel, CN_SERVICES, service_rel);
    }

    if (monitor_rel)
    {
        json_object_set_new(rel, CN_MONITORS, monitor_rel);
    }

    json_object_set_new(rval, CN_RELATIONSHIPS, rel);
    /** Attributes */
    json_object_set_new(rval, CN_ATTRIBUTES, server_json_attributes(server));
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_SERVERS, server->name));

    return rval;
}

json_t* server_to_json(const SERVER* server, const char* host)
{
    string self = MXS_JSON_API_SERVERS;
    self += server->name;
    return mxs_json_resource(host, self.c_str(), server_to_json_data(server, host));
}

json_t* server_list_to_json(const char* host)
{
    json_t* data = json_array();
    Guard guard(server_lock);

    for (Server* server : all_servers)
    {
        if (server_is_active(server))
        {
            json_array_append_new(data, server_to_json_data(server, host));
        }
    }

    return mxs_json_resource(host, MXS_JSON_API_SERVERS, data);
}

bool server_set_disk_space_threshold(SERVER* server, const char* disk_space_threshold)
{
    bool rv = false;

    MxsDiskSpaceThreshold dst;

    rv = config_parse_disk_space_threshold(&dst, disk_space_threshold);

    if (rv)
    {
        if (!server->disk_space_threshold)
        {
            server->disk_space_threshold = new(std::nothrow) MxsDiskSpaceThreshold;
        }

        if (server->disk_space_threshold)
        {
            server->disk_space_threshold->swap(dst);
        }
        else
        {
            rv = false;
        }
    }

    return rv;
}

namespace
{
std::mutex ave_write_mutex;
}

void server_add_response_average(SERVER* srv, double ave, int num_samples)
{
    Server* server = static_cast<Server*>(srv);


    std::lock_guard<std::mutex> guard(ave_write_mutex);
    server->response_time_add(ave, num_samples);
}

int server_response_time_num_samples(const SERVER* srv)
{
    const Server* server = static_cast<const Server*>(srv);
    return server->response_time_num_samples();
}

double server_response_time_average(const SERVER* srv)
{
    const Server* server = static_cast<const Server*>(srv);
    return server->response_time_average();
}

/** Apply backend average and adjust sample_max, which determines the weight of a new average
 *  applied to EMAverage.
 *  Sample max is raised if the server is fast, aggresively lowered if the incoming average is clearly
 *  lower than the EMA, else just lowered a bit. The normal increase and decrease, drifting, of the max
 *  is done to follow the speed of a server. The important part is the lowering of max, to allow for a
 *  server that is speeding up to be adjusted and used.
 *
 *  Three new magic numbers to replace the sample max magic number...
 *
 */
void Server::response_time_add(double ave, int num_samples)
{
    constexpr double drift {1.1};
    int current_max = m_response_time.sample_max();
    int new_max {0};

    // This server handles more samples than EMA max.
    // Increasing max allows all servers to be fairly compared.
    if (num_samples >= current_max)
    {
        new_max = num_samples * drift;
    }
    // This server is experiencing high load of some kind,
    // lower max to give more weight to the samples.
    else if (m_response_time.average() / ave > 2)
    {
        new_max = current_max * 0.5;
    }
    // Let the max slowly trickle down to keep
    // the sample max close to reality.
    else
    {
        new_max = current_max / drift;
    }

    m_response_time.set_sample_max(new_max);
    m_response_time.add(ave, num_samples);
}
