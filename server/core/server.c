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

/**
 * @file server.c  - A representation of a backend server within the gateway.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 18/06/13     Mark Riddoch            Initial implementation
 * 17/05/14     Mark Riddoch            Addition of unique_name
 * 20/05/14     Massimiliano Pinto      Addition of server_string
 * 21/05/14     Massimiliano Pinto      Addition of node_id
 * 28/05/14     Massimiliano Pinto      Addition of rlagd and node_ts fields
 * 20/06/14     Massimiliano Pinto      Addition of master_id, depth, slaves fields
 * 26/06/14     Mark Riddoch            Addition of server parameters
 * 30/08/14     Massimiliano Pinto      Addition of new service status description
 * 30/10/14     Massimiliano Pinto      Addition of SERVER_MASTER_STICKINESS description
 * 01/06/15     Massimiliano Pinto      Addition of server_update_address/port
 * 19/06/15     Martin Brampton         Extra code for persistent connections
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
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

#include "maxscale/monitor.h"
#include "maxscale/poll.h"

/** The latin1 charset */
#define SERVER_DEFAULT_CHARSET 0x08

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
    DCB **persistent = MXS_CALLOC(nthr, sizeof(*persistent));

    if (!server || !my_name || !my_protocol || !my_authenticator || !persistent)
    {
        MXS_FREE(server);
        MXS_FREE(my_name);
        MXS_FREE(persistent);
        MXS_FREE(my_protocol);
        MXS_FREE(my_authenticator);
        return NULL;
    }

    if (snprintf(server->name, sizeof(server->name), "%s", address) > sizeof(server->name))
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
    server->server_string = NULL;
    spinlock_init(&server->lock);
    server->persistent = persistent;
    server->persistmax = 0;
    server->persistmaxtime = 0;
    server->persistpoolmax = 0;
    server->monuser[0] = '\0';
    server->monpw[0] = '\0';
    server->is_active = true;
    server->created_online = false;
    server->charset = SERVER_DEFAULT_CHARSET;

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
    MXS_FREE(tofreeserver->server_string);
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
 * @param       server      The server to set the name on
 * @param       user        The name of the user needing the connection
 * @param       protocol    The name of the protocol needed for the connection
 */
DCB *
server_get_persistent(SERVER *server, const char *user, const char *protocol, int id)
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
                && !dcb-> dcb_errhandle_called
                && !(dcb->flags & DCBF_HUNG)
                && 0 == strcmp(dcb->user, user)
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
 *
 * Designed to be called within a debugger session in order
 * to display all active servers within the gateway
 */
void
dprintAllServersJson(DCB *dcb)
{
    char *stat;
    int len = 0;
    int el = 1;

    spinlock_acquire(&server_spin);
    SERVER *server = next_active_server(allServers);
    while (server)
    {
        server = next_active_server(server->next);
        len++;
    }

    server = next_active_server(allServers);

    dcb_printf(dcb, "[\n");
    while (server)
    {
        dcb_printf(dcb, "  {\n  \"server\": \"%s\",\n",
                   server->name);
        stat = server_status(server);
        dcb_printf(dcb, "    \"status\": \"%s\",\n",
                   stat);
        MXS_FREE(stat);
        dcb_printf(dcb, "    \"protocol\": \"%s\",\n",
                   server->protocol);
        dcb_printf(dcb, "    \"port\": \"%d\",\n",
                   server->port);
        if (server->server_string)
        {
            dcb_printf(dcb, "    \"version\": \"%s\",\n",
                       server->server_string);
        }
        dcb_printf(dcb, "    \"nodeId\": \"%ld\",\n",
                   server->node_id);
        dcb_printf(dcb, "    \"masterId\": \"%ld\",\n",
                   server->master_id);
        if (server->slaves)
        {
            int i;
            dcb_printf(dcb, "    \"slaveIds\": [ ");
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
            dcb_printf(dcb, "],\n");
        }
        dcb_printf(dcb, "    \"replDepth\": \"%d\",\n",
                   server->depth);
        if (SERVER_IS_SLAVE(server) || SERVER_IS_RELAY_SERVER(server))
        {
            if (server->rlag >= 0)
            {
                dcb_printf(dcb, "    \"slaveDelay\": \"%d\",\n", server->rlag);
            }
        }
        if (server->node_ts > 0)
        {
            dcb_printf(dcb, "    \"lastReplHeartbeat\": \"%lu\",\n", server->node_ts);
        }
        dcb_printf(dcb, "    \"totalConnections\": \"%d\",\n",
                   server->stats.n_connections);
        dcb_printf(dcb, "    \"currentConnections\": \"%d\",\n",
                   server->stats.n_current);
        dcb_printf(dcb, "    \"currentOps\": \"%d\"\n",
                   server->stats.n_current_ops);
        if (el < len)
        {
            dcb_printf(dcb, "  },\n");
        }
        else
        {
            dcb_printf(dcb, "  }\n");
        }
        server = next_active_server(server->next);
        el++;
    }

    dcb_printf(dcb, "]\n");
    spinlock_release(&server_spin);
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
    if (server->server_string)
    {
        dcb_printf(dcb, "\tServer Version:                      %s\n", server->server_string);
    }
    dcb_printf(dcb, "\tNode Id:                             %ld\n", server->node_id);
    dcb_printf(dcb, "\tMaster Id:                           %ld\n", server->master_id);
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
    if (server->persistpoolmax)
    {
        dcb_printf(dcb, "\tPersistent pool size:                %d\n", server->stats.n_persistent);
        poll_send_message(POLL_MSG_CLEAN_PERSISTENT, (void*)server);
        dcb_printf(dcb, "\tPersistent measured pool size:       %d\n", server->stats.n_persistent);
        dcb_printf(dcb, "\tPersistent actual size max:          %d\n", server->persistmax);
        dcb_printf(dcb, "\tPersistent pool size limit:          %ld\n", server->persistpoolmax);
        dcb_printf(dcb, "\tPersistent max time (secs):          %ld\n", server->persistmaxtime);
        dcb_printf(dcb, "\tConnections taken from pool:         %lu\n", server->stats.n_from_pool);
        double d =  (double)server->stats.n_from_pool / (double)(server->stats.n_connections + server->stats.n_from_pool + 1);
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

    unsigned int server_status = server->status;
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
    if (server_status & SERVER_STALE_STATUS)
    {
        strcat(status, "Stale Status, ");
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
server_set_status_nolock(SERVER *server, int bit)
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
server_clear_set_status(SERVER *server, int specified_bits, int bits_to_set)
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
server_clear_status_nolock(SERVER *server, int bit)
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
        snprintf(server->monuser, sizeof(server->monuser), "%s", user) > sizeof(server->monuser))
    {
        MXS_WARNING("Truncated monitor user for server '%s', maximum username "
                    "length is %lu characters.", server->unique_name, sizeof(server->monuser));
    }

    if (passwd != server->monpw &&
        snprintf(server->monpw, sizeof(server->monpw), "%s", passwd) > sizeof(server->monpw))
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
    char *my_name = MXS_STRDUP(name);
    char *my_value = MXS_STRDUP(value);

    SERVER_PARAM *param = (SERVER_PARAM *)MXS_MALLOC(sizeof(SERVER_PARAM));

    if (!my_name || !my_value || !param)
    {
        MXS_FREE(my_name);
        MXS_FREE(my_value);
        MXS_FREE(param);
        return;
    }

    param->active = true;
    param->name = my_name;
    param->value = my_value;

    spinlock_acquire(&server->lock);
    param->next = server->parameters;
    server->parameters = param;
    spinlock_release(&server->lock);
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
 * Retrieve a parameter value from a server
 *
 * @param server        The server we are looking for a parameter of
 * @param name          The name of the parameter we require
 * @return      The parameter value or NULL if not found
 */
const char *
server_get_parameter(const SERVER *server, char *name)
{
    SERVER_PARAM *param = server->parameters;

    while (param)
    {
        if (strcmp(param->name, name) == 0 && param->active)
        {
            return param->value;
        }
        param = param->next;
    }
    return NULL;
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
    char            *str;
    unsigned int    bit;
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
unsigned int
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
 * @param server Server to update
 * @param string Version string
 * @return True if the assignment of the version string was successful, false if
 * memory allocation failed.
 */
bool server_set_version_string(SERVER* server, const char* string)
{
    bool rval = true;
    string = MXS_STRDUP(string);

    if (string)
    {
        char* old = server->server_string;
        server->server_string = (char*)string;
        MXS_FREE(old);
    }
    else
    {
        rval = false;
    }

    return rval;
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
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to open file '%s' when serializing server '%s': %d, %s",
                  filename, server->unique_name, errno, strerror_r(errno, errbuf, sizeof(errbuf)));
        return false;
    }

    // TODO: Check for return values on all of the dprintf calls
    dprintf(file, "[%s]\n", server->unique_name);
    dprintf(file, "type=server\n");
    dprintf(file, "protocol=%s\n", server->protocol);
    dprintf(file, "address=%s\n", server->name);
    dprintf(file, "port=%u\n", server->port);
    dprintf(file, "authenticator=%s\n", server->authenticator);

    if (server->auth_options)
    {
        dprintf(file, "authenticator_options=%s\n", server->auth_options);
    }

    if (*server->monpw && *server->monuser)
    {
        dprintf(file, "monitoruser=%s\n", server->monuser);
        dprintf(file, "monitorpw=%s\n", server->monpw);
    }

    if (server->persistpoolmax)
    {
        dprintf(file, "persistpoolmax=%ld\n", server->persistpoolmax);
    }

    if (server->persistmaxtime)
    {
        dprintf(file, "persistmaxtime=%ld\n", server->persistmaxtime);
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
        char err[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to remove temporary server configuration at '%s': %d, %s",
                  filename, errno, strerror_r(errno, err, sizeof(err)));
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
            char err[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Failed to rename temporary server configuration at '%s': %d, %s",
                      filename, errno, strerror_r(errno, err, sizeof(err)));
        }
    }

    return rval;
}

SERVER* server_find_destroyed(const char *name, const char *protocol,
                              const char *authenticator, const char *auth_options)
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
