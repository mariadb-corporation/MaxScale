/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file server.c  - A representation of a backend server within the gateway.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string>

#include <maxscale/config.h>
#include <maxscale/service.h>
#include <maxscale/session.h>
#include <maxscale/server.h>
#include <maxscale/spinlock.h>
#include <maxscale/dcb.h>
#include <maxscale/poll.h>
#include <maxscale/log_manager.h>
#include <maxscale/ssl.h>
#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include <maxscale/utils.h>
#include <maxscale/semaphore.hh>
#include <maxscale/json_api.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/http.hh>
#include <maxscale/maxscale.h>

#include "internal/monitor.h"
#include "internal/poll.h"
#include "internal/workertask.hh"
#include "internal/worker.hh"

using maxscale::Semaphore;
using maxscale::Worker;
using maxscale::WorkerTask;

using std::string;

/** The latin1 charset */
#define SERVER_DEFAULT_CHARSET 0x08

const char CN_MONITORPW[]          = "monitorpw";
const char CN_MONITORUSER[]        = "monitoruser";
const char CN_PERSISTMAXTIME[]     = "persistmaxtime";
const char CN_PERSISTPOOLMAX[]     = "persistpoolmax";
const char CN_PROXY_PROTOCOL[]     = "proxy_protocol";

static SPINLOCK server_spin = SPINLOCK_INIT;
static SERVER *allServers = NULL;

static void spin_reporter(void *, char *, int);
static void server_parameter_free(SERVER_PARAM *tofree);


SERVER* server_alloc(const char *name, const char *address, unsigned short port,
                     const char *protocol, const char *authenticator, const char *auth_options)
{
    if (authenticator == NULL && (authenticator = get_default_authenticator(protocol)) == NULL)
    {
        MXS_ERROR("No authenticator defined for server '%s' and no default "
                  "authenticator for protocol '%s'.", name, protocol);
        return NULL;
    }

    void *auth_instance = NULL;

    if (!authenticator_init(&auth_instance, authenticator, auth_options))
    {
        MXS_ERROR("Failed to initialize authenticator module '%s' for server '%s' ",
                  authenticator, name);
        return NULL;
    }

    char *my_auth_options = NULL;

    if (auth_options && (my_auth_options = MXS_STRDUP(auth_options)) == NULL)
    {
        return NULL;
    }

    int nthr = config_threadcount();
    SERVER *server = (SERVER *)MXS_CALLOC(1, sizeof(SERVER));
    char *my_name = MXS_STRDUP(name);
    char *my_protocol = MXS_STRDUP(protocol);
    char *my_authenticator = MXS_STRDUP(authenticator);
    DCB **persistent = (DCB**)MXS_CALLOC(nthr, sizeof(*persistent));

    if (!server || !my_name || !my_protocol || !my_authenticator || !persistent)
    {
        MXS_FREE(server);
        MXS_FREE(my_name);
        MXS_FREE(persistent);
        MXS_FREE(my_protocol);
        MXS_FREE(my_authenticator);
        return NULL;
    }

    if (snprintf(server->name, sizeof(server->name), "%s", address) > (int)sizeof(server->name))
    {
        MXS_WARNING("Truncated server address '%s' to the maximum size of %lu characters.",
                    address, sizeof(server->name));
    }

#if defined(SS_DEBUG)
    server->server_chk_top = CHK_NUM_SERVER;
    server->server_chk_tail = CHK_NUM_SERVER;
#endif
    server->unique_name = my_name;
    server->protocol = my_protocol;
    server->authenticator = my_authenticator;
    server->auth_instance = auth_instance;
    server->auth_options = my_auth_options;
    server->port = port;
    server->status = SERVER_RUNNING;
    server->status_pending = SERVER_RUNNING;
    server->node_id = -1;
    server->rlag = MAX_RLAG_UNDEFINED;
    server->master_id = -1;
    server->depth = -1;
    server->parameters = NULL;
    spinlock_init(&server->lock);
    server->persistent = persistent;
    server->persistmax = 0;
    server->persistmaxtime = 0;
    server->persistpoolmax = 0;
    server->monuser[0] = '\0';
    server->monpw[0] = '\0';
    server->is_active = true;
    server->charset = SERVER_DEFAULT_CHARSET;
    server->proxy_protocol = false;

    // Set last event to server_up as the server is in Running state on startup
    server->last_event = SERVER_UP_EVENT;
    server->triggered_at = 0;

    // Log all warnings once
    memset(&server->log_warning, 1, sizeof(server->log_warning));

    spinlock_acquire(&server_spin);
    server->next = allServers;
    allServers = server;
    spinlock_release(&server_spin);

    return server;
}


/**
 * Deallocate the specified server
 *
 * @param server        The service to deallocate
 * @return Returns true if the server was freed
 */
int
server_free(SERVER *tofreeserver)
{
    SERVER *server;

    /* First of all remove from the linked list */
    spinlock_acquire(&server_spin);
    if (allServers == tofreeserver)
    {
        allServers = tofreeserver->next;
    }
    else
    {
        server = allServers;
        while (server && server->next != tofreeserver)
        {
            server = server->next;
        }
        if (server)
        {
            server->next = tofreeserver->next;
        }
    }
    spinlock_release(&server_spin);

    /* Clean up session and free the memory */
    MXS_FREE(tofreeserver->protocol);
    MXS_FREE(tofreeserver->unique_name);
    server_parameter_free(tofreeserver->parameters);

    if (tofreeserver->persistent)
    {
        int nthr = config_threadcount();

        for (int i = 0; i < nthr; i++)
        {
            dcb_persistent_clean_count(tofreeserver->persistent[i], i, true);
        }
    }
    MXS_FREE(tofreeserver);
    return 1;
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
DCB* server_get_persistent(SERVER *server, const char *user, const char* ip, const char *protocol, int id)
{
    DCB *dcb, *previous = NULL;

    if (server->persistent[id]
        && dcb_persistent_clean_count(server->persistent[id], id, false)
        && server->persistent[id] // Check after cleaning
        && (server->status & SERVER_RUNNING))
    {
        dcb = server->persistent[id];
        while (dcb)
        {
            if (dcb->user
                && dcb->protoname
                && dcb->remote
                && ip
                && !dcb-> dcb_errhandle_called
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
                atomic_add(&server->stats.n_persistent, -1);
                atomic_add(&server->stats.n_current, 1);
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
                          dcb-> dcb_errhandle_called ? "true" : "false");
            }
            previous = dcb;
            dcb = dcb->nextpersistent;
        }
    }
    return NULL;
}

static inline SERVER* next_active_server(SERVER *server)
{
    while (server && !server->is_active)
    {
        server = server->next;
    }

    return server;
}

/**
 * @brief Find a server with the specified name
 *
 * @param name Name of the server
 * @return The server or NULL if not found
 */
SERVER * server_find_by_unique_name(const char *name)
{
    spinlock_acquire(&server_spin);
    SERVER *server = next_active_server(allServers);

    while (server)
    {
        if (server->unique_name && strcmp(server->unique_name, name) == 0)
        {
            break;
        }
        server = next_active_server(server->next);
    }

    spinlock_release(&server_spin);

    return server;
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
int server_find_by_unique_names(char **server_names, int size, SERVER*** output)
{
    ss_dassert(server_names && (size > 0));

    SERVER **results = (SERVER**)MXS_CALLOC(size, sizeof(SERVER*));
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
SERVER *
server_find(const char *servname, unsigned short port)
{
    spinlock_acquire(&server_spin);
    SERVER *server = next_active_server(allServers);

    while (server)
    {
        if (strcmp(server->name, servname) == 0 && server->port == port)
        {
            break;
        }
        server = next_active_server(server->next);
    }

    spinlock_release(&server_spin);

    return server;
}

/**
 * Print details of an individual server
 *
 * @param server        Server to print
 */
void
printServer(const SERVER *server)
{
    printf("Server %p\n", server);
    printf("\tServer:                       %s\n", server->name);
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
void
printAllServers()
{
    spinlock_acquire(&server_spin);
    SERVER *server = next_active_server(allServers);

    while (server)
    {
        printServer(server);
        server = next_active_server(server->next);
    }

    spinlock_release(&server_spin);
}

/**
 * Print all servers to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active servers within the gateway
 */
void
dprintAllServers(DCB *dcb)
{
    spinlock_acquire(&server_spin);
    SERVER *server = next_active_server(allServers);

    while (server)
    {
        dprintServer(dcb, server);
        server = next_active_server(server->next);
    }

    spinlock_release(&server_spin);
}

/**
 * Print all servers in Json format to a DCB
 */
void
dprintAllServersJson(DCB *dcb)
{
    json_t* all_servers = server_list_to_json("");
    char* dump = json_dumps(all_servers, JSON_INDENT(4));
    dcb_printf(dcb, "%s", dump);
    MXS_FREE(dump);
    json_decref(all_servers);
}

/**
 * A class for cleaning up persistent connections
 */
class CleanupTask : public WorkerTask
{
public:
    CleanupTask(const SERVER* server):
        m_server(server)
    {
    }

    void execute(Worker& worker)
    {
        int thread_id = worker.get_current_id();
        dcb_persistent_clean_count(m_server->persistent[thread_id], thread_id, false);
    }

private:
    const SERVER* m_server; /**< Server to clean up */
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
    Worker::execute_concurrently(task);
}

/**
 * Print server details to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active servers within the gateway
 */
void
dprintServer(DCB *dcb, const SERVER *server)
{
    if (!SERVER_IS_ACTIVE(server))
    {
        return;
    }

    dcb_printf(dcb, "Server %p (%s)\n", server, server->unique_name);
    dcb_printf(dcb, "\tServer:                              %s\n", server->name);
    char* stat = server_status(server);
    dcb_printf(dcb, "\tStatus:                              %s\n", stat);
    MXS_FREE(stat);
    dcb_printf(dcb, "\tProtocol:                            %s\n", server->protocol);
    dcb_printf(dcb, "\tPort:                                %d\n", server->port);
    dcb_printf(dcb, "\tServer Version:                      %s\n", server->version_string);
    dcb_printf(dcb, "\tNode Id:                             %ld\n", server->node_id);
    dcb_printf(dcb, "\tMaster Id:                           %ld\n", server->master_id);
    dcb_printf(dcb, "\tLast event:                          %s\n",
               mon_get_event_name((mxs_monitor_event_t)server->last_event));
    time_t t = maxscale_started() + HB_TO_SEC(server->triggered_at);
    dcb_printf(dcb, "\tTriggered at:                        %s\n", http_to_date(t).c_str());

    if (server->slaves)
    {
        int i;
        dcb_printf(dcb, "\tSlave Ids:                           ");
        for (i = 0; server->slaves[i]; i++)
        {
            if (i == 0)
            {
                dcb_printf(dcb, "%li", server->slaves[i]);
            }
            else
            {
                dcb_printf(dcb, ", %li ", server->slaves[i]);
            }
        }
        dcb_printf(dcb, "\n");
    }
    dcb_printf(dcb, "\tRepl Depth:                          %d\n", server->depth);
    if (SERVER_IS_SLAVE(server) || SERVER_IS_RELAY_SERVER(server))
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
        dcb_printf(dcb, "\tLast Repl Heartbeat:                 %s",
                   asctime_r(localtime_r((time_t *)(&server->node_ts), &result), buf));
    }
    SERVER_PARAM *param;
    if ((param = server->parameters))
    {
        dcb_printf(dcb, "\tServer Parameters:\n");
        while (param)
        {
            if (param->active)
            {
                dcb_printf(dcb, "\t                                       %s\t%s\n",
                           param->name, param->value);
            }
            param = param->next;
        }
    }
    dcb_printf(dcb, "\tNumber of connections:               %d\n", server->stats.n_connections);
    dcb_printf(dcb, "\tCurrent no. of conns:                %d\n", server->stats.n_current);
    dcb_printf(dcb, "\tCurrent no. of operations:           %d\n", server->stats.n_current_ops);
    dcb_printf(dcb, "\tNumber of routed packets:            %lu\n", server->stats.packets);
    if (server->persistpoolmax)
    {
        dcb_printf(dcb, "\tPersistent pool size:                %d\n", server->stats.n_persistent);
        cleanup_persistent_connections(server);
        dcb_printf(dcb, "\tPersistent measured pool size:       %d\n", server->stats.n_persistent);
        dcb_printf(dcb, "\tPersistent actual size max:          %d\n", server->persistmax);
        dcb_printf(dcb, "\tPersistent pool size limit:          %ld\n", server->persistpoolmax);
        dcb_printf(dcb, "\tPersistent max time (secs):          %ld\n", server->persistmaxtime);
        dcb_printf(dcb, "\tConnections taken from pool:         %lu\n", server->stats.n_from_pool);
        double d =  (double)server->stats.n_from_pool / (double)(server->stats.n_connections +
                                                                 server->stats.n_from_pool + 1);
        dcb_printf(dcb, "\tPool availability:                   %0.2lf%%\n", d * 100.0);
    }
    if (server->server_ssl)
    {
        SSL_LISTENER *l = server->server_ssl;
        dcb_printf(dcb, "\tSSL initialized:                     %s\n",
                   l->ssl_init_done ? "yes" : "no");
        dcb_printf(dcb, "\tSSL method type:                     %s\n",
                   ssl_method_type_to_string(l->ssl_method_type));
        dcb_printf(dcb, "\tSSL certificate verification depth:  %d\n", l->ssl_cert_verify_depth);
        dcb_printf(dcb, "\tSSL peer verification :  %s\n", l->ssl_verify_peer_certificate ? "true" : "false");
        dcb_printf(dcb, "\tSSL certificate:                     %s\n",
                   l->ssl_cert ? l->ssl_cert : "null");
        dcb_printf(dcb, "\tSSL key:                             %s\n",
                   l->ssl_key ? l->ssl_key : "null");
        dcb_printf(dcb, "\tSSL CA certificate:                  %s\n",
                   l->ssl_ca_cert ? l->ssl_ca_cert : "null");
    }
    if (server->proxy_protocol)
    {
        dcb_printf(dcb, "\tPROXY protocol:                      on.\n");
    }
}

/**
 * Display an entry from the spinlock statistics data
 *
 * @param       dcb     The DCB to print to
 * @param       desc    Description of the statistic
 * @param       value   The statistic value
 */
static void
spin_reporter(void *dcb, char *desc, int value)
{
    dcb_printf((DCB *)dcb, "\t\t%-40s  %d\n", desc, value);
}

/**
 * Diagnostic to print number of DCBs in persistent pool for a server
 *
 * @param       pdcb    DCB to print results to
 * @param       server  SERVER for which DCBs are to be printed
 */
void
dprintPersistentDCBs(DCB *pdcb, const SERVER *server)
{
    dcb_printf(pdcb, "Number of persistent DCBs: %d\n", server->stats.n_persistent);
}

/**
 * List all servers in a tabular form to a DCB
 *
 */
void
dListServers(DCB *dcb)
{
    spinlock_acquire(&server_spin);
    SERVER  *server = next_active_server(allServers);
    bool have_servers = false;

    if (server)
    {
        have_servers = true;
        dcb_printf(dcb, "Servers.\n");
        dcb_printf(dcb, "-------------------+-----------------+-------+-------------+--------------------\n");
        dcb_printf(dcb, "%-18s | %-15s | Port  | Connections | %-20s\n",
                   "Server", "Address", "Status");
        dcb_printf(dcb, "-------------------+-----------------+-------+-------------+--------------------\n");
    }

    while (server)
    {
        char *stat = server_status(server);
        dcb_printf(dcb, "%-18s | %-15s | %5d | %11d | %s\n",
                   server->unique_name, server->name,
                   server->port,
                   server->stats.n_current, stat);
        MXS_FREE(stat);
        server = next_active_server(server->next);
    }

    if (have_servers)
    {
        dcb_printf(dcb, "-------------------+-----------------+-------+-------------+--------------------\n");
    }
    spinlock_release(&server_spin);
}

/**
 * Convert a set of  server status flags to a string, the returned
 * string has been malloc'd and must be free'd by the caller
 *
 * @param server The server to return the status of
 * @return A string representation of the status flags
 */
char *
server_status(const SERVER *server)
{
    char    *status = NULL;

    if (NULL == server || (status = (char *)MXS_MALLOC(512)) == NULL)
    {
        return NULL;
    }

    uint64_t server_status = server->status;
    status[0] = 0;
    if (server_status & SERVER_MAINT)
    {
        strcat(status, "Maintenance, ");
    }
    if (server_status & SERVER_MASTER)
    {
        strcat(status, "Master, ");
    }
    if (server_status & SERVER_RELAY_MASTER)
    {
        strcat(status, "Relay Master, ");
    }
    if (server_status & SERVER_SLAVE)
    {
        strcat(status, "Slave, ");
    }
    if (server_status & SERVER_JOINED)
    {
        strcat(status, "Synced, ");
    }
    if (server_status & SERVER_NDB)
    {
        strcat(status, "NDB, ");
    }
    if (server_status & SERVER_SLAVE_OF_EXTERNAL_MASTER)
    {
        strcat(status, "Slave of External Server, ");
    }
    if (server_status & SERVER_MASTER_STICKINESS)
    {
        strcat(status, "Master Stickiness, ");
    }
    if (server_status & SERVER_AUTH_ERROR)
    {
        strcat(status, "Auth Error, ");
    }
    if (server_status & SERVER_RUNNING)
    {
        strcat(status, "Running");
    }
    else
    {
        strcat(status, "Down");
    }
    return status;
}

/**
 * Set a status bit in the server without locking
 *
 * @param server        The server to update
 * @param bit           The bit to set for the server
 */
void
server_set_status_nolock(SERVER *server, uint64_t bit)
{
    server->status |= bit;

    /** clear error logged flag before the next failure */
    if (SERVER_IS_MASTER(server))
    {
        server->master_err_is_logged = false;
    }
}

/**
 * Set one or more status bit(s) from a specified set, clearing any others
 * in the specified set
 *
 * @attention This function does no locking
 *
 * @param server        The server to update
 * @param bit           The bit to set for the server
 */
void
server_clear_set_status(SERVER *server, uint64_t specified_bits, uint64_t bits_to_set)
{
    /** clear error logged flag before the next failure */
    if ((bits_to_set & SERVER_MASTER) && ((server->status & SERVER_MASTER) == 0))
    {
        server->master_err_is_logged = false;
    }

    if ((server->status & specified_bits) != bits_to_set)
    {
        server->status = (server->status & ~specified_bits) | bits_to_set;
    }
}

/**
 * Clear a status bit in the server without locking
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
void
server_clear_status_nolock(SERVER *server, uint64_t bit)
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
void
server_transfer_status(SERVER *dest_server, const SERVER *source_server)
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
void
server_add_mon_user(SERVER *server, const char *user, const char *passwd)
{
    if (user != server->monuser &&
        snprintf(server->monuser, sizeof(server->monuser), "%s", user) > (int)sizeof(server->monuser))
    {
        MXS_WARNING("Truncated monitor user for server '%s', maximum username "
                    "length is %lu characters.", server->unique_name, sizeof(server->monuser));
    }

    if (passwd != server->monpw &&
        snprintf(server->monpw, sizeof(server->monpw), "%s", passwd) > (int)sizeof(server->monpw))
    {
        MXS_WARNING("Truncated monitor password for server '%s', maximum password "
                    "length is %lu characters.", server->unique_name, sizeof(server->monpw));
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
void
server_update_credentials(SERVER *server, const char *user, const char *passwd)
{
    if (user != NULL && passwd != NULL)
    {
        server_add_mon_user(server, user, passwd);
    }
}

static SERVER_PARAM* allocate_parameter(const char* name, const char* value)
{
    char *my_name = MXS_STRDUP(name);
    char *my_value = MXS_STRDUP(value);

    SERVER_PARAM *param = (SERVER_PARAM *)MXS_MALLOC(sizeof(SERVER_PARAM));

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

/**
 * Add a server parameter to a server.
 *
 * Server parameters may be used by routing to weight the load
 * balancing they apply to the server.
 *
 * @param       server  The server we are adding the parameter to
 * @param       name    The parameter name
 * @param       value   The parameter value
 */
void server_add_parameter(SERVER *server, const char *name, const char *value)
{
    SERVER_PARAM* param = allocate_parameter(name, value);

    if (param)
    {
        spinlock_acquire(&server->lock);
        param->next = server->parameters;
        server->parameters = param;
        spinlock_release(&server->lock);
    }
}

bool server_remove_parameter(SERVER *server, const char *name)
{
    bool rval = false;
    spinlock_acquire(&server->lock);

    for (SERVER_PARAM *p = server->parameters; p; p = p->next)
    {
        if (strcmp(p->name, name) == 0 && p->active)
        {
            p->active = false;
            rval = true;
            break;
        }
    }

    spinlock_release(&server->lock);
    return rval;
}

void server_update_parameter(SERVER *server, const char *name, const char *value)
{
    SERVER_PARAM* param = allocate_parameter(name, value);

    if (param)
    {
        spinlock_acquire(&server->lock);

        // Insert new value
        param->next = server->parameters;
        server->parameters = param;

        // Mark old value, if found, as inactive
        for (SERVER_PARAM *p = server->parameters->next; p; p = p->next)
        {
            if (strcmp(p->name, name) == 0 && p->active)
            {
                p->active = false;
                break;
            }
        }

        spinlock_release(&server->lock);
    }
}

/**
 * Free a list of server parameters
 * @param tofree Parameter list to free
 */
static void server_parameter_free(SERVER_PARAM *tofree)
{
    SERVER_PARAM *param;

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
size_t server_get_parameter_nolock(const SERVER *server, const char *name, char* out, size_t size)
{
    ss_dassert(SPINLOCK_IS_LOCKED(&server->lock));
    size_t len = 0;
    SERVER_PARAM *param = server->parameters;

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
size_t server_get_parameter(const SERVER *server, const char *name, char* out, size_t size)
{
    spinlock_acquire(&server->lock);
    size_t len = server_get_parameter_nolock(server, name, out, size);
    spinlock_release(&server->lock);
    return len;
}

/**
 * Provide a row to the result set that defines the set of servers
 *
 * @param set   The result set
 * @param data  The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
serverRowCallback(RESULTSET *set, void *data)
{
    int *rowno = (int *)data;
    int i = 0;
    char *stat = NULL, buf[20];
    RESULT_ROW *row = NULL;
    SERVER *server = NULL;

    spinlock_acquire(&server_spin);
    server = allServers;
    while (i < *rowno && server)
    {
        i++;
        server = server->next;
    }
    if (server == NULL)
    {
        spinlock_release(&server_spin);
        MXS_FREE(data);
        return NULL;
    }
    (*rowno)++;
    if (SERVER_IS_ACTIVE(server))
    {
        row = resultset_make_row(set);
        resultset_row_set(row, 0, server->unique_name);
        resultset_row_set(row, 1, server->name);
        sprintf(buf, "%d", server->port);
        resultset_row_set(row, 2, buf);
        sprintf(buf, "%d", server->stats.n_current);
        resultset_row_set(row, 3, buf);
        stat = server_status(server);
        resultset_row_set(row, 4, stat);
        MXS_FREE(stat);
    }
    spinlock_release(&server_spin);
    return row;
}

/**
 * Return a resultset that has the current set of servers in it
 *
 * @return A Result set
 */
RESULTSET *
serverGetList()
{
    RESULTSET *set;
    int *data;

    if ((data = (int *)MXS_MALLOC(sizeof(int))) == NULL)
    {
        return NULL;
    }
    *data = 0;
    if ((set = resultset_create(serverRowCallback, data)) == NULL)
    {
        MXS_FREE(data);
        return NULL;
    }
    resultset_add_column(set, "Server", 20, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Address", 15, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Port", 5, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Connections", 8, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Status", 20, COL_TYPE_VARCHAR);

    return set;
}

/*
 * Update the address value of a specific server
 *
 * @param server        The server to update
 * @param address       The new address
 *
 */
void
server_update_address(SERVER *server, const char *address)
{
    spinlock_acquire(&server_spin);
    if (server && address)
    {
        strcpy(server->name, address);
    }
    spinlock_release(&server_spin);
}

/*
 * Update the port value of a specific server
 *
 * @param server        The server to update
 * @param port          The new port value
 *
 */
void
server_update_port(SERVER *server, unsigned short port)
{
    spinlock_acquire(&server_spin);
    if (server && port > 0)
    {
        server->port = port;
    }
    spinlock_release(&server_spin);
}

static struct
{
    const char* str;
    uint64_t    bit;
} ServerBits[] =
{
    { "running",     SERVER_RUNNING },
    { "master",      SERVER_MASTER },
    { "slave",       SERVER_SLAVE },
    { "synced",      SERVER_JOINED },
    { "ndb",         SERVER_NDB },
    { "maintenance", SERVER_MAINT },
    { "maint",       SERVER_MAINT },
    { "stale",       SERVER_STALE_STATUS },
    { NULL,          0 }
};

/**
 * Map the server status bit
 *
 * @param str   String representation
 * @return bit value or 0 on error
 */
uint64_t
server_map_status(const char *str)
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

/**
 * Creates a server configuration at the location pointed by @c filename
 *
 * @param server Server to serialize into a configuration
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
static bool create_server_config(const SERVER *server, const char *filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing server '%s': %d, %s",
                  filename, server->unique_name, errno, mxs_strerror(errno));
        return false;
    }

    // TODO: Check for return values on all of the dprintf calls
    dprintf(file, "[%s]\n", server->unique_name);
    dprintf(file, "%s=server\n", CN_TYPE);
    dprintf(file, "%s=%s\n", CN_PROTOCOL, server->protocol);
    dprintf(file, "%s=%s\n", CN_ADDRESS, server->name);
    dprintf(file, "%s=%u\n", CN_PORT, server->port);
    dprintf(file, "%s=%s\n", CN_AUTHENTICATOR, server->authenticator);

    if (server->auth_options)
    {
        dprintf(file, "%s=%s\n", CN_AUTHENTICATOR_OPTIONS, server->auth_options);
    }

    if (*server->monpw && *server->monuser)
    {
        dprintf(file, "%s=%s\n", CN_MONITORUSER, server->monuser);
        dprintf(file, "%s=%s\n", CN_MONITORPW, server->monpw);
    }

    if (server->persistpoolmax)
    {
        dprintf(file, "%s=%ld\n", CN_PERSISTPOOLMAX, server->persistpoolmax);
    }

    if (server->persistmaxtime)
    {
        dprintf(file, "%s=%ld\n", CN_PERSISTMAXTIME, server->persistmaxtime);
    }

    if (server->proxy_protocol)
    {
        dprintf(file, "%s=on\n", CN_PROXY_PROTOCOL);
    }

    for (SERVER_PARAM *p = server->parameters; p; p = p->next)
    {
        if (p->active)
        {
            dprintf(file, "%s=%s\n", p->name, p->value);
        }
    }

    if (server->server_ssl)
    {
        write_ssl_config(file, server->server_ssl);
    }

    close(file);

    return true;
}

bool server_serialize(const SERVER *server)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf.tmp", get_config_persistdir(),
             server->unique_name);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary server configuration at '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
    }
    else if (create_server_config(server, filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char *dot = strrchr(final_filename, '.');
        ss_dassert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary server configuration at '%s': %d, %s",
                      filename, errno, mxs_strerror(errno));
        }
    }

    return rval;
}

SERVER* server_repurpose_destroyed(const char *name, const char *protocol,
                                   const char *authenticator, const char *auth_options,
                                   const char *address, const char *port)
{
    spinlock_acquire(&server_spin);
    SERVER *server = allServers;
    while (server)
    {
        CHK_SERVER(server);
        if (strcmp(server->unique_name, name) == 0 &&
            strcmp(server->protocol, protocol) == 0 &&
            strcmp(server->authenticator, authenticator) == 0)
        {
            if ((auth_options == NULL && server->auth_options == NULL) ||
                (auth_options && server->auth_options &&
                 strcmp(server->auth_options, auth_options) == 0))
            {
                snprintf(server->name, sizeof(server->name), "%s", address);
                server->port = atoi(port);
                server->is_active = true;
                break;
            }
        }
        server = server->next;
    }
    spinlock_release(&server_spin);

    return server;
}
/**
 * Set a status bit in the server under a lock. This ensures synchronization
 * with the server monitor thread. Calling this inside the monitor will likely
 * cause a deadlock. If the server is monitored, only set the pending bit.
 *
 * @param server        The server to update
 * @param bit           The bit to set for the server
 */
void server_set_status(SERVER *server, int bit)
{
    /* First check if the server is monitored. This isn't done under a lock
     * but the race condition cannot cause significant harm. Monitors are never
     * freed so the pointer stays valid.
     */
    MXS_MONITOR *mon = monitor_server_in_use(server);
    spinlock_acquire(&server->lock);
    if (mon && mon->state == MONITOR_STATE_RUNNING)
    {
        /* Set a pending status bit. It will be activated on the next monitor
         * loop. Also set a flag so the next loop happens sooner.
         */
        server->status_pending |= bit;
        mon->server_pending_changes = true;
    }
    else
    {
        /* Set the bit directly */
        server_set_status_nolock(server, bit);
    }
    spinlock_release(&server->lock);
}
/**
 * Clear a status bit in the server under a lock. This ensures synchronization
 * with the server monitor thread. Calling this inside the monitor will likely
 * cause a deadlock. If the server is monitored, only clear the pending bit.
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
void server_clear_status(SERVER *server, int bit)
{
    MXS_MONITOR *mon = monitor_server_in_use(server);
    spinlock_acquire(&server->lock);
    if (mon && mon->state == MONITOR_STATE_RUNNING)
    {
        /* Clear a pending status bit. It will be activated on the next monitor
         * loop. Also set a flag so the next loop happens sooner.
         */
        server->status_pending &= ~bit;
        mon->server_pending_changes = true;
    }
    else
    {
        /* Clear bit directly */
        server_clear_status_nolock(server, bit);
    }
    spinlock_release(&server->lock);
}

bool server_is_mxs_service(const SERVER *server)
{
    bool rval = false;

    /** Do a coarse check for local server pointing to a MaxScale service */
    if (strcmp(server->name, "127.0.0.1") == 0 ||
        strcmp(server->name, "::1") == 0 ||
        strcmp(server->name, "localhost") == 0 ||
        strcmp(server->name, "localhost.localdomain") == 0)
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

    json_object_set_new(params, CN_ADDRESS, json_string(server->name));
    json_object_set_new(params, CN_PORT, json_integer(server->port));
    json_object_set_new(params, CN_PROTOCOL, json_string(server->protocol));

    if (server->authenticator)
    {
        json_object_set_new(params, CN_AUTHENTICATOR, json_string(server->authenticator));
    }

    if (server->auth_options)
    {
        json_object_set_new(params, CN_AUTHENTICATOR_OPTIONS, json_string(server->auth_options));
    }

    if (*server->monuser)
    {
        json_object_set_new(params, CN_MONITORUSER, json_string(server->monuser));
    }

    if (*server->monpw)
    {
        json_object_set_new(params, CN_MONITORPW, json_string(server->monpw));
    }

    if (server->server_ssl)
    {
        json_object_set_new(params, CN_SSL_KEY, json_string(server->server_ssl->ssl_key));
        json_object_set_new(params, CN_SSL_CERT, json_string(server->server_ssl->ssl_cert));
        json_object_set_new(params, CN_SSL_CA_CERT, json_string(server->server_ssl->ssl_ca_cert));
        json_object_set_new(params, CN_SSL_CERT_VERIFY_DEPTH,
                            json_integer(server->server_ssl->ssl_cert_verify_depth));
        json_object_set_new(params, CN_SSL_VERSION,
                            json_string(ssl_method_type_to_string(server->server_ssl->ssl_method_type)));
    }

    for (SERVER_PARAM* p = server->parameters; p; p = p->next)
    {
        json_object_set_new(params, p->name, json_string(p->value));
    }

    json_object_set_new(attr, CN_PARAMETERS, params);

    /** Store general information about the server state */
    char* stat = server_status(server);
    json_object_set_new(attr, CN_STATE, json_string(stat));
    MXS_FREE(stat);

    json_object_set_new(attr, CN_VERSION_STRING, json_string(server->version_string));

    json_object_set_new(attr, "node_id", json_integer(server->node_id));
    json_object_set_new(attr, "master_id", json_integer(server->master_id));
    json_object_set_new(attr, "replication_depth", json_integer(server->depth));

    const char* event_name = mon_get_event_name((mxs_monitor_event_t)server->last_event);
    time_t t = maxscale_started() + HB_TO_SEC(server->triggered_at);
    json_object_set_new(attr, "last_event", json_string(event_name));
    json_object_set_new(attr, "triggered_at", json_string(http_to_date(t).c_str()));

    if (server->slaves)
    {
        json_t* slaves = json_array();

        for (int i = 0; server->slaves[i]; i++)
        {
            json_array_append_new(slaves, json_integer(server->slaves[i]));
        }

        json_object_set_new(attr, "slaves", slaves);
    }

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
    json_object_set_new(stats, "active_operations", json_integer(server->stats.n_current_ops));
    json_object_set_new(stats, "routed_packets", json_integer(server->stats.packets));

    json_object_set_new(attr, "statistics", stats);

    return attr;
}

static json_t* server_to_json_data(const SERVER* server, const char* host)
{
    json_t* rval = json_object();

    /** Add resource identifiers */
    json_object_set_new(rval, CN_ID, json_string(server->unique_name));
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
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_SERVERS, server->unique_name));

    return rval;
}

json_t* server_to_json(const SERVER* server, const char* host)
{
    string self = MXS_JSON_API_SERVERS;
    self += server->unique_name;
    return mxs_json_resource(host, self.c_str(), server_to_json_data(server, host));
}

json_t* server_list_to_json(const char* host)
{
    json_t* data = json_array();

    spinlock_acquire(&server_spin);

    for (SERVER* server = allServers; server; server = server->next)
    {
        if (SERVER_IS_ACTIVE(server))
        {
            json_array_append_new(data, server_to_json_data(server, host));
        }
    }

    spinlock_release(&server_spin);

    return mxs_json_resource(host, MXS_JSON_API_SERVERS, data);
}
