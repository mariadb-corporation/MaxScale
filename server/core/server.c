/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
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
#include <session.h>
#include <server.h>
#include <spinlock.h>
#include <dcb.h>
#include <poll.h>
#include <skygw_utils.h>
#include <log_manager.h>

static SPINLOCK server_spin = SPINLOCK_INIT;
static SERVER *allServers = NULL;

static void spin_reporter(void *, char *, int);

/**
 * Allocate a new server withn the gateway
 *
 *
 * @param servname      The server name
 * @param protocol      The protocol to use to connect to the server
 * @param port          The port to connect to
 *
 * @return              The newly created server or NULL if an error occured
 */
SERVER *
server_alloc(char *servname, char *protocol, unsigned short port)
{
    SERVER *server;

    if ((server = (SERVER *)calloc(1, sizeof(SERVER))) == NULL)
    {
        return NULL;
    }
#if defined(SS_DEBUG)
    server->server_chk_top = CHK_NUM_SERVER;
    server->server_chk_tail = CHK_NUM_SERVER;
#endif
    server->name = strndup(servname, MAX_SERVER_NAME_LEN);
    server->protocol = strdup(protocol);
    server->port = port;
    server->status = SERVER_RUNNING;
    server->node_id = -1;
    server->rlag = -2;
    server->master_id = -1;
    server->depth = -1;
    spinlock_init(&server->lock);
    server->persistent = NULL;
    server->persistmax = 0;
    spinlock_init(&server->persistlock);

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
    free(tofreeserver->name);
    free(tofreeserver->protocol);
    if (tofreeserver->unique_name)
    {
        free(tofreeserver->unique_name);
    }
    if (tofreeserver->server_string)
    {
        free(tofreeserver->server_string);
    }
    if (tofreeserver->persistent)
    {
        dcb_persistent_clean_count(tofreeserver->persistent, true);
    }
    free(tofreeserver);
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
server_get_persistent(SERVER *server, char *user, const char *protocol)
{
    DCB *dcb, *previous = NULL;

    if (server->persistent
        && dcb_persistent_clean_count(server->persistent, false)
        && server->persistent
        && (server->status & SERVER_RUNNING))
    {
        spinlock_acquire(&server->persistlock);
        dcb = server->persistent;
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
                    server->persistent = dcb->nextpersistent;
                }
                else
                {
                    previous->nextpersistent = dcb->nextpersistent;
                }
                free(dcb->user);
                dcb->user = NULL;
                spinlock_release(&server->persistlock);
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
        spinlock_release(&server->persistlock);
    }
    return NULL;
}

/**
 * Set a unique name for the server
 *
 * @param       server  The server to set the name on
 * @param       name    The unique name for the server
 */
void
server_set_unique_name(SERVER *server, char *name)
{
    server->unique_name = strdup(name);
}

/**
 * Find an existing server using the unique section name in
 * configuration file
 *
 * @param       servname        The Server name or address
 * @param       port            The server port
 * @return      The server or NULL if not found
 */
SERVER *
server_find_by_unique_name(char *name)
{
    SERVER *server;

    spinlock_acquire(&server_spin);
    server = allServers;
    while (server)
    {
        if (server->unique_name && strcmp(server->unique_name, name) == 0)
        {
            break;
        }
        server = server->next;
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
server_find(char *servname, unsigned short port)
{
    SERVER  *server;

    spinlock_acquire(&server_spin);
    server = allServers;
    while (server)
    {
        if (strcmp(server->name, servname) == 0 && server->port == port)
        {
            break;
        }
        server = server->next;
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
printServer(SERVER *server)
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
    SERVER *server;

    spinlock_acquire(&server_spin);
    server = allServers;
    while (server)
    {
        printServer(server);
        server = server->next;
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
    SERVER *server;
    char *stat;

    spinlock_acquire(&server_spin);
    server = allServers;
    while (server)
    {
        dcb_printf(dcb, "Server %p (%s)\n", server, server->unique_name);
        dcb_printf(dcb, "\tServer:                              %s\n",
                   server->name);
        stat = server_status(server);
        dcb_printf(dcb, "\tStatus:                              %s\n",
                   stat);
        free(stat);
        dcb_printf(dcb, "\tProtocol:                    %s\n",
                   server->protocol);
        dcb_printf(dcb, "\tPort:                                %d\n",
                   server->port);
        if (server->server_string)
            dcb_printf(dcb, "\tServer Version:\t\t\t%s\n",
                       server->server_string);
        dcb_printf(dcb, "\tNode Id:                     %d\n",
                   server->node_id);
        dcb_printf(dcb, "\tMaster Id:                   %d\n",
                   server->master_id);
        if (server->slaves)
        {
            int i;
            dcb_printf(dcb, "\tSlave Ids:                   ");
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
        dcb_printf(dcb, "\tRepl Depth:                  %d\n",
                   server->depth);
        if (SERVER_IS_SLAVE(server) || SERVER_IS_RELAY_SERVER(server))
        {
            if (server->rlag >= 0)
            {
                dcb_printf(dcb, "\tSlave delay:\t\t%d\n", server->rlag);
            }
        }
        if (server->node_ts > 0)
        {
            dcb_printf(dcb, "\tLast Repl Heartbeat:\t%lu\n", server->node_ts);
        }
        dcb_printf(dcb, "\tNumber of connections:               %d\n",
                   server->stats.n_connections);
        dcb_printf(dcb, "\tCurrent no. of conns:                %d\n",
                   server->stats.n_current);
        dcb_printf(dcb, "\tCurrent no. of operations:   %d\n",
                   server->stats.n_current_ops);
        if (server->persistpoolmax)
        {
            dcb_printf(dcb, "\tPersistent pool size:            %d\n",
                       server->stats.n_persistent);
            dcb_printf(dcb, "\tPersistent measured pool size:   %d\n",
                       dcb_persistent_clean_count(server->persistent, false));
            dcb_printf(dcb, "\tPersistent max size achieved:    %d\n",
                       server->persistmax);
            dcb_printf(dcb, "\tPersistent pool size limit:      %d\n",
                       server->persistpoolmax);
            dcb_printf(dcb, "\tPersistent max time (secs):          %d\n",
                       server->persistmaxtime);
        }
        server = server->next;
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
    SERVER *server;
    char *stat;
    int len = 0;
    int el = 1;

    spinlock_acquire(&server_spin);
    server = allServers;
    while (server)
    {
        server = server->next;
        len++;
    }
    server = allServers;
    dcb_printf(dcb, "[\n");
    while (server)
    {
        dcb_printf(dcb, "  {\n  \"server\": \"%s\",\n",
                   server->name);
        stat = server_status(server);
        dcb_printf(dcb, "    \"status\": \"%s\",\n",
                   stat);
        free(stat);
        dcb_printf(dcb, "    \"protocol\": \"%s\",\n",
                   server->protocol);
        dcb_printf(dcb, "    \"port\": \"%d\",\n",
                   server->port);
        if (server->server_string)
        {
            dcb_printf(dcb, "    \"version\": \"%s\",\n",
                       server->server_string);
        }
        dcb_printf(dcb, "    \"nodeId\": \"%d\",\n",
                   server->node_id);
        dcb_printf(dcb, "    \"masterId\": \"%d\",\n",
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
        server = server->next;
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
dprintServer(DCB *dcb, SERVER *server)
{
    char *stat;
    SERVER_PARAM *param;

    dcb_printf(dcb, "Server %p (%s)\n", server, server->unique_name);
    dcb_printf(dcb, "\tServer:                              %s\n", server->name);
    stat = server_status(server);
    dcb_printf(dcb, "\tStatus:                              %s\n", stat);
    free(stat);
    dcb_printf(dcb, "\tProtocol:                    %s\n", server->protocol);
    dcb_printf(dcb, "\tPort:                                %d\n", server->port);
    if (server->server_string)
    {
        dcb_printf(dcb, "\tServer Version:\t\t\t%s\n", server->server_string);
    }
    dcb_printf(dcb, "\tNode Id:                     %d\n", server->node_id);
    dcb_printf(dcb, "\tMaster Id:                   %d\n", server->master_id);
    if (server->slaves)
    {
        int i;
        dcb_printf(dcb, "\tSlave Ids:                   ");
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
    dcb_printf(dcb, "\tRepl Depth:                  %d\n", server->depth);
    if (SERVER_IS_SLAVE(server) || SERVER_IS_RELAY_SERVER(server))
    {
        if (server->rlag >= 0)
        {
            dcb_printf(dcb, "\tSlave delay:\t\t%d\n", server->rlag);
        }
    }
    if (server->node_ts > 0)
    {
        struct tm result;
        char buf[40];
        dcb_printf(dcb, "\tLast Repl Heartbeat:\t%s",
                   asctime_r(localtime_r((time_t *)(&server->node_ts), &result), buf));
    }
    if ((param = server->parameters) != NULL)
    {
        dcb_printf(dcb, "\tServer Parameters:\n");
        while (param)
        {
            dcb_printf(dcb, "\t\t%-20s\t%s\n", param->name,
                       param->value);
            param = param->next;
        }
    }
    dcb_printf(dcb, "\tNumber of connections:               %d\n",
               server->stats.n_connections);
    dcb_printf(dcb, "\tCurrent no. of conns:                %d\n",
               server->stats.n_current);
    dcb_printf(dcb, "\tCurrent no. of operations:   %d\n", server->stats.n_current_ops);
    if (server->persistpoolmax)
    {
        dcb_printf(dcb, "\tPersistent pool size:            %d\n",
                   server->stats.n_persistent);
        dcb_printf(dcb, "\tPersistent measured pool size:   %d\n",
                   dcb_persistent_clean_count(server->persistent, false));
        dcb_printf(dcb, "\tPersistent actual size max:            %d\n",
                   server->persistmax);
        dcb_printf(dcb, "\tPersistent pool size limit:            %d\n",
                   server->persistpoolmax);
        dcb_printf(dcb, "\tPersistent max time (secs):          %d\n",
                   server->persistmaxtime);
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
 * Diagnostic to print all DCBs in persistent pool for a server
 *
 * @param       pdcb    DCB to print results to
 * @param       server  SERVER for which DCBs are to be printed
 */
void
dprintPersistentDCBs(DCB *pdcb, SERVER *server)
{
    DCB *dcb;

    spinlock_acquire(&server->persistlock);
#if SPINLOCK_PROFILE
    dcb_printf(pdcb, "DCB List Spinlock Statistics:\n");
    spinlock_stats(&server->persistlock, spin_reporter, pdcb);
#endif
    dcb = server->persistent;
    while (dcb)
    {
        dprintOneDCB(pdcb, dcb);
        dcb = dcb->nextpersistent;
    }
    spinlock_release(&server->persistlock);
}

/**
 * List all servers in a tabular form to a DCB
 *
 */
void
dListServers(DCB *dcb)
{
    SERVER  *server;
    char    *stat;

    spinlock_acquire(&server_spin);
    server = allServers;
    if (server)
    {
        dcb_printf(dcb, "Servers.\n");
        dcb_printf(dcb, "-------------------+-----------------+-------+-------------+--------------------\n");
        dcb_printf(dcb, "%-18s | %-15s | Port  | Connections | %-20s\n",
                   "Server", "Address", "Status");
        dcb_printf(dcb, "-------------------+-----------------+-------+-------------+--------------------\n");
    }
    while (server)
    {
        stat = server_status(server);
        dcb_printf(dcb, "%-18s | %-15s | %5d | %11d | %s\n",
                   server->unique_name, server->name,
                   server->port,
                   server->stats.n_current, stat);
        free(stat);
        server = server->next;
    }
    if (allServers)
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
server_status(SERVER *server)
{
    char    *status = NULL;

    if (NULL == server || (status = (char *)malloc(256)) == NULL)
    {
        return NULL;
    }
    status[0] = 0;
    if (server->status & SERVER_MAINT)
    {
        strcat(status, "Maintenance, ");
    }
    if (server->status & SERVER_MASTER)
    {
        strcat(status, "Master, ");
    }
    if (server->status & SERVER_SLAVE)
    {
        strcat(status, "Slave, ");
    }
    if (server->status & SERVER_JOINED)
    {
        strcat(status, "Synced, ");
    }
    if (server->status & SERVER_NDB)
    {
        strcat(status, "NDB, ");
    }
    if (server->status & SERVER_SLAVE_OF_EXTERNAL_MASTER)
    {
        strcat(status, "Slave of External Server, ");
    }
    if (server->status & SERVER_STALE_STATUS)
    {
        strcat(status, "Stale Status, ");
    }
    if (server->status & SERVER_MASTER_STICKINESS)
    {
        strcat(status, "Master Stickiness, ");
    }
    if (server->status & SERVER_AUTH_ERROR)
    {
        strcat(status, "Auth Error, ");
    }
    if (server->status & SERVER_RUNNING)
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
 * Set a status bit in the server
 *
 * @param server        The server to update
 * @param bit           The bit to set for the server
 */
void
server_set_status(SERVER *server, int bit)
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
 * Clear a status bit in the server
 *
 * @param server        The server to update
 * @param bit           The bit to clear for the server
 */
void
server_clear_status(SERVER *server, int bit)
{
    server->status &= ~bit;
}

/**
 * Transfer status bitstring from one server to another
 *
 * @param dest_server       The server to be updated
 * @param source_server         The server to provide the new bit string
 */
void
server_transfer_status(SERVER *dest_server, SERVER *source_server)
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
serverAddMonUser(SERVER *server, char *user, char *passwd)
{
    server->monuser = strdup(user);
    server->monpw = strdup(passwd);
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
server_update(SERVER *server, char *protocol, char *user, char *passwd)
{
    if (!strcmp(server->protocol, protocol))
    {
        MXS_NOTICE("Update server protocol for server %s to protocol %s.",
                   server->name,
                   protocol);
        free(server->protocol);
        server->protocol = strdup(protocol);
    }

    if (user != NULL && passwd != NULL)
    {
        if (strcmp(server->monuser, user) == 0 ||
            strcmp(server->monpw, passwd) == 0)
        {
            MXS_NOTICE("Update server monitor credentials for server %s",
                       server->name);
            free(server->monuser);
            free(server->monpw);
            serverAddMonUser(server, user, passwd);
        }
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
void
serverAddParameter(SERVER *server, char *name, char *value)
{
    SERVER_PARAM    *param;

    if ((param = (SERVER_PARAM *)malloc(sizeof(SERVER_PARAM))) == NULL)
    {
        return;
    }
    if ((param->name = strdup(name)) == NULL)
    {
        free(param);
        return;
    }
    if ((param->value = strdup(value)) == NULL)
    {
        free(param->value);
        free(param);
        return;
    }

    param->next = server->parameters;
    server->parameters = param;
}

/**
 * Retrieve a parameter value from a server
 *
 * @param server        The server we are looking for a parameter of
 * @param name          The name of the parameter we require
 * @return      The parameter value or NULL if not found
 */
char *
serverGetParameter(SERVER *server, char *name)
{
    SERVER_PARAM *param = server->parameters;

    while (param)
    {
        if (strcmp(param->name, name) == 0)
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
    char *stat, buf[20];
    RESULT_ROW *row;
    SERVER *server;

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
        free(data);
        return NULL;
    }
    (*rowno)++;
    row = resultset_make_row(set);
    resultset_row_set(row, 0, server->unique_name);
    resultset_row_set(row, 1, server->name);
    sprintf(buf, "%d", server->port);
    resultset_row_set(row, 2, buf);
    sprintf(buf, "%d", server->stats.n_current);
    resultset_row_set(row, 3, buf);
    stat = server_status(server);
    resultset_row_set(row, 4, stat);
    free(stat);
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

    if ((data = (int *)malloc(sizeof(int))) == NULL)
    {
        return NULL;
    }
    *data = 0;
    if ((set = resultset_create(serverRowCallback, data)) == NULL)
    {
        free(data);
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
server_update_address(SERVER *server, char *address)
{
    spinlock_acquire(&server_spin);
    if (server && address)
    {
        if (server->name)
        {
            free(server->name);
        }
        server->name = strdup(address);
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
} ServerBits[] = {
    { "running",     SERVER_RUNNING },
    { "master",      SERVER_MASTER },
    { "slave",       SERVER_SLAVE },
    { "synced",      SERVER_JOINED },
    { "ndb",         SERVER_NDB },
    { "maintenance", SERVER_MAINT },
    { "maint",       SERVER_MAINT },
    { NULL,          0 }
};

/**
 * Map the server status bit
 *
 * @param str   String representation
 * @return bit value or 0 on error
 */
unsigned int
server_map_status(char *str)
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
    spinlock_acquire(&server->lock);
    free(server->server_string);
    if ((server->server_string = strdup(string)) == NULL)
    {
        MXS_ERROR("Memory allocation failed.");
        rval = false;
    }
    spinlock_release(&server->lock);
    return rval;
}
