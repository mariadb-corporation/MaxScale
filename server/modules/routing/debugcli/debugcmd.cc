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
 * @file debugcmd.c  - The debug CLI command line interpreter
 *
 * The command interpreter for the dbug user interface. The command
 * structure is such that there are a numerb of commands, notably
 * show and a set of subcommands, the things to show in this case.
 *
 * Each subcommand has a handler function defined for it that is passeed
 * the DCB to use to print the output of the commands and up to 3 arguments
 * as numeric values.
 *
 * There are two "built in" commands, the help command and the quit
 * command.
 */
#include <maxscale/cdefs.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslog.h>
#include <telnetd.h>

#include <maxscale/adminusers.h>
#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/buffer.h>
#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/filter.h>
#include <maxscale/housekeeper.h>
#include <maxscale/log_manager.h>
#include <maxscale/maxscale.h>
#include <maxscale/modulecmd.h>
#include <maxscale/router.h>
#include <maxscale/server.hh>
#include <maxscale/service.h>
#include <maxscale/spinlock.h>
#include <maxscale/users.h>
#include <maxscale/utils.h>
#include <maxscale/version.h>
#include <maxscale/routingworker.h>

#include <debugcli.h>

#include "../../../core/internal/config.h"
#include "../../../core/internal/config_runtime.h"
#include "../../../core/internal/maxscale.h"
#include "../../../core/internal/modules.h"
#include "../../../core/internal/monitor.h"
#include "../../../core/internal/poll.h"
#include "../../../core/internal/session.h"

#define MAXARGS 14

#define ARG_TYPE_NONE           0
#define ARG_TYPE_ADDRESS        1
#define ARG_TYPE_STRING         2
#define ARG_TYPE_SERVICE        3
#define ARG_TYPE_SERVER         4
#define ARG_TYPE_DBUSERS        5
#define ARG_TYPE_SESSION        6
#define ARG_TYPE_DCB            7
#define ARG_TYPE_MONITOR        8
#define ARG_TYPE_FILTER         9
#define ARG_TYPE_NUMERIC        10
#define ARG_TYPE_OBJECT_NAME    11 // A string where whitespace is replaced with hyphens

/**
 * The subcommand structure
 *
 * These are the options that may be passed to a command
 */
typedef void (*FN  )(DCB*);
typedef void (*FN1 )(DCB*, unsigned long);
typedef void (*FN2 )(DCB*, unsigned long, unsigned long);
typedef void (*FN3 )(DCB*, unsigned long, unsigned long, unsigned long);
typedef void (*FN4 )(DCB*, unsigned long, unsigned long, unsigned long, unsigned long);
typedef void (*FN5 )(DCB*, unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
typedef void (*FN6 )(DCB*,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long);
typedef void (*FN7 )(DCB*,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long);
typedef void (*FN8 )(DCB*,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long);
typedef void (*FN9 )(DCB*,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long, unsigned long);
typedef void (*FN10)(DCB*,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long);
typedef void (*FN11)(DCB*,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long);
typedef void (*FN12)(DCB*,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long);
typedef void (*FN13)(DCB*,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long);
typedef void (*FN14)(DCB*,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long, unsigned long);

struct subcommand
{
    const char *arg1;
    int         argc_min;
    int         argc_max;
    void      (*fn)(DCB*);
    const char *help;
    const char *devhelp;
    int         arg_types[MAXARGS];
};

#define EMPTY_OPTION

static void telnetdShowUsers(DCB *);
static void show_log_throttling(DCB *);

static void showVersion(DCB *dcb)
{
    dcb_printf(dcb, "%s\n", MAXSCALE_VERSION);
}

/**
 * The subcommands of the show command
 */
struct subcommand showoptions[] =
{
#if defined(BUFFER_TRACE)
    {
        "buffers",    0, (FN)dprintAllBuffers,
        "Show all buffers with backtrace",
        "Show all buffers with backtrace",
        {0}
    },
#endif
    {
        "dcbs", 0, 0, (FN)dprintAllDCBs,
        "Show all DCBs",
        "Usage: show dcbs",
        {0}
    },
    {
        "dbusers", 1, 1, (FN)service_print_users,
        "[deprecated] Show user statistics",
        "See `show authenticators`",
        {ARG_TYPE_SERVICE}
    },
    {
        "authenticators", 1, 1, (FN)service_print_users,
        "Show authenticator diagnostics for a service",
        "Usage: show authenticators SERVICE\n"
        "\n"
        "Parameters:\n"
        "SERVICE Service to inspect\n"
        "\n"
        "Example : show authenticators my-service",
        {ARG_TYPE_SERVICE}
    },
    {
        "epoll", 0, 0, (FN)dprintPollStats,
        "Show the polling system statistics",
        "Usage: show epoll",
        {0}
    },
    {
        "eventstats", 0, 0, (FN)dShowEventStats,
        "Show event queue statistics",
        "Usage: show eventstats",
        {0}
    },
    {
        "filter", 1, 1, (FN)dprintFilter,
        "Show filter details",
        "Usage: show filter FILTER\n"
        "\n"
        "Parameters:\n"
        "FILTER Filter to show\n"
        "\n"
        "Example: show filter my-filter",
        {ARG_TYPE_FILTER}
    },
    {
        "filters", 0, 0, (FN)dprintAllFilters,
        "Show all filters",
        "Usage: show filters",
        {0}
    },
    {
        "log_throttling", 0, 0, (FN)show_log_throttling,
        "Show the current log throttling setting (count, window (ms), suppression (ms))",
        "Usage: show log_throttling",
        {0}
    },
    {
        "modules", 0, 0, (FN)dprintAllModules,
        "Show all currently loaded modules",
        "Usage: show modules",
        {0}
    },
    {
        "monitor", 1, 1, (FN)monitor_show,
        "Show monitor details",
        "Usage: show monitor MONITOR\n"
        "\n"
        "Parameters:\n"
        "MONITOR Monitor to show\n"
        "\n"
        "Example: show monitor \"Cluster Monitor\"",
        {ARG_TYPE_MONITOR}
    },
    {
        "monitors", 0, 0, (FN)monitor_show_all,
        "Show all monitors",
        "Usage: show monitors",
        {0}
    },
    {
        "persistent", 1, 1, (FN)dprintPersistentDCBs,
        "Show the persistent connection pool of a server",
        "Usage: show persistent SERVER\n"
        "\n"
        "Parameters:\n"
        "SERVER Server to show\n"
        "\n"
        "Example: show persistent db-server-1",
        {ARG_TYPE_SERVER}
    },
    {
        "server", 1, 1, (FN)dprintServer,
        "Show server details",
        "Usage: show server SERVER\n"
        "\n"
        "Parameters:\n"
        "SERVER Server to show\n"
        "\n"
        "Example: show server db-server-1",
        {ARG_TYPE_SERVER}
    },
    {
        "servers", 0, 0, (FN)dprintAllServers,
        "Show all servers",
        "Usage: show servers",
        {0}
    },
    {
        "serversjson", 0, 0, (FN)dprintAllServersJson,
        "Show all servers in JSON",
        "Usage: show serversjson",
        {0}
    },
    {
        "services", 0, 0, (FN)dprintAllServices,
        "Show all configured services in MaxScale",
        "Usage: show services",
        {0}
    },
    {
        "service", 1, 1, (FN)dprintService,
        "Show a single service in MaxScale",
        "Usage: show service SERVICE\n"
        "\n"
        "Parameters:\n"
        "SERVICE Service to show\n"
        "\n"
        "Example: show service my-service",
        {ARG_TYPE_SERVICE}
    },
    {
        "session", 1, 1, (FN)dprintSession,
        "Show session details",
        "Usage: show session SESSION\n"
        "\n"
        "Parameters:\n"
        "SESSION Session ID of the session to show\n"
        "\n"
        "Example: show session 5",
        {ARG_TYPE_SESSION}
    },
    {
        "sessions", 0, 0, (FN)dprintAllSessions,
        "Show all active sessions in MaxScale",
        "Usage: show sessions",
        {0}
    },
    {
        "tasks", 0, 0, (FN)hkshow_tasks,
        "Show all active housekeeper tasks in MaxScale",
        "Usage: show tasks",
        {0}
    },
    {
        "threads", 0, 0, (FN)dShowThreads,
        "Show the status of the worker threads in MaxScale",
        "Usage: show threads",
        {0}
    },
    {
        "users", 0, 0, (FN)telnetdShowUsers,
        "Show enabled Linux accounts",
        "Usage: show users",
        {0}
    },
    {
        "version", 0, 0, (FN)showVersion,
        "Show the MaxScale version number",
        "Usage: show version",
        {0}
    },
    { EMPTY_OPTION}
};

bool listfuncs_cb(const MODULECMD *cmd, void *data)
{
    DCB *dcb = (DCB*)data;

    dcb_printf(dcb, "Command:\t%s %s\n", cmd->domain, cmd->identifier);
    dcb_printf(dcb, "Description:\t%s\n", cmd->description);
    dcb_printf(dcb, "Parameters:\t");

    for (int i = 0; i < cmd->arg_count_max; i++)
    {
        modulecmd_arg_type_t *type = &cmd->arg_types[i];
        dcb_printf(dcb, "%s%s",
                   modulecmd_argtype_to_str(&cmd->arg_types[i]),
                   i < cmd->arg_count_max - 1 ? " " : "");
    }

    dcb_printf(dcb, "\n\n");

    for (int i = 0; i < cmd->arg_count_max; i++)
    {
        modulecmd_arg_type_t *type = &cmd->arg_types[i];
        dcb_printf(dcb, "    %s - %s\n",
                   modulecmd_argtype_to_str(&cmd->arg_types[i]),
                   cmd->arg_types[i].description);
    }

    dcb_printf(dcb, "\n");

    return true;
}

void dListCommands(DCB *dcb, const char *domain, const char *ident)
{
    modulecmd_foreach(domain, ident, listfuncs_cb, dcb);
}

/**
 * The subcommands of the list command
 */
struct subcommand listoptions[] =
{
    {
        "clients", 0, 0, (FN)dListClients,
        "List all the client connections to MaxScale",
        "Usage: list clients",
        {0}
    },
    {
        "dcbs", 0, 0, (FN)dListDCBs,
        "List all active connections within MaxScale",
        "Usage: list dcbs",
        {0}
    },
    {
        "filters", 0, 0, (FN)dListFilters,
        "List all filters",
        "Usage: list filters",
        {0}
    },
    {
        "listeners", 0, 0, (FN)dListListeners,
        "List all listeners",
        "Usage: list listeners",
        {0}
    },
    {
        "modules", 0, 0, (FN)dprintAllModules,
        "List all currently loaded modules",
        "Usage: list modules",
        {0}
    },
    {
        "monitors", 0, 0, (FN)monitor_list,
        "List all monitors",
        "Usage: list monitors",
        {0}
    },
    {
        "services", 0, 0, (FN)dListServices,
        "List all services",
        "Usage: list services",
        {0}
    },
    {
        "servers", 0, 0, (FN)dListServers,
        "List all servers",
        "Usage: list servers",
        {0}
    },
    {
        "sessions", 0, 0, (FN)dListSessions,
        "List all the active sessions within MaxScale",
        "Usage: list sessions",
        {0}
    },
    {
        "threads", 0, 0, (FN)dShowThreads,
        "List the status of the polling threads in MaxScale",
        "Usage: list threads",
        {0}
    },
    {
        "commands", 0, 2, (FN)dListCommands,
        "List registered commands",
        "Usage: list commands [MODULE] [COMMAND]\n"
        "\n"
        "Parameters:\n"
        "MODULE  Regular expressions for filtering module names\n"
        "COMMAND Regular expressions for filtering module command names\n"
        "\n"
        "Example: list commands my-module my-command",
        {ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME}
    },
    { EMPTY_OPTION}
};

static void shutdown_server()
{
    maxscale_shutdown();
}

static void shutdown_service(DCB *dcb, SERVICE *service);
static void shutdown_monitor(DCB *dcb, MXS_MONITOR *monitor);

static void
shutdown_listener(DCB *dcb, SERVICE *service, const char *name)
{
    if (serviceStopListener(service, name))
    {
        dcb_printf(dcb, "Stopped listener '%s'\n", name);
    }
    else
    {
        dcb_printf(dcb, "Failed to stop listener '%s'\n", name);
    }
}

/**
 * The subcommands of the shutdown command
 */
struct subcommand shutdownoptions[] =
{
    {
        "maxscale",
        0, 0,
        (FN)shutdown_server,
        "Initiate a controlled shutdown of MaxScale",
        "Usage: shutdown maxscale",
        {0}
    },
    {
        "monitor",
        1, 1,
        (FN)shutdown_monitor,
        "Stop a monitor",
        "Usage: shutdown monitor MONITOR\n"
        "\n"
        "Parameters:\n"
        "MONITOR Monitor to stop\n"
        "\n"
        "Example: shutdown monitor db-cluster-monitor",
        {ARG_TYPE_MONITOR}
    },
    {
        "service",
        1, 1,
        (FN)shutdown_service,
        "Stop a service",
        "Usage: shutdown service SERVICE\n"
        "\n"
        "Parameters:\n"
        "SERVICE Service to stop\n"
        "\n"
        "Example: shutdown service \"Sales Database\"",
        {ARG_TYPE_SERVICE}
    },
    {
        "listener",
        2, 2,
        (FN)shutdown_listener,
        "Stop a listener",
        "Usage: shutdown listener SERVICE LISTENER\n"
        "\n"
        "Parameters:\n"
        "SERVICE  Service where LISTENER points to\n"
        "LISTENER The listener to stop\n"
        "\n"
        "Example: shutdown listener \"RW Service\" \"RW Listener\"",
        {ARG_TYPE_SERVICE, ARG_TYPE_OBJECT_NAME}
    },
    {
        EMPTY_OPTION
    }
};

static void sync_logs(DCB *dcb)
{
    if (mxs_log_flush_sync() == 0)
    {
        dcb_printf(dcb, "Logs flushed to disk\n");
    }
    else
    {
        dcb_printf(dcb, "Failed to flush logs to disk. Read the error log for "
                   "more details.\n");
    }
}

struct subcommand syncoptions[] =
{
    {
        "logs",
        0, 0,
        (FN)sync_logs,
        "Flush log files to disk",
        "Usage: flush logs",
        {0}
    },
    {
        EMPTY_OPTION
    }
};

static void restart_service(DCB *dcb, SERVICE *service);
static void restart_monitor(DCB *dcb, MXS_MONITOR *monitor);

static void
restart_listener(DCB *dcb, SERVICE *service, const char *name)
{
    if (serviceStartListener(service, name))
    {
        dcb_printf(dcb, "Restarted listener '%s'\n", name);
    }
    else
    {
        dcb_printf(dcb, "Failed to restart listener '%s'\n", name);
    }
}

/**
 * The subcommands of the restart command
 */
struct subcommand restartoptions[] =
{
    {
        "monitor", 1, 1, (FN)restart_monitor,
        "Restart a monitor",
        "Usage: restart monitor NAME\n"
        "\n"
        "Parameters:\n"
        "NAME Monitor to restart\n"
        "\n"
        "Example: restart monitor db-cluster-monitor",
        {ARG_TYPE_MONITOR}
    },
    {
        "service", 1, 1, (FN)restart_service,
        "Restart a service",
        "Usage: restart service NAME\n"
        "\n"
        "Parameters:\n"
        "NAME Service to restart\n"
        "\n"
        "Example: restart service \"Sales Database\"",
        {ARG_TYPE_SERVICE}
    },
    {
        "listener", 2, 2, (FN)restart_listener,
        "Restart a listener",
        "Usage: restart listener NAME\n"
        "\n"
        "Parameters:\n"
        "NAME Listener to restart\n"
        "\n"
        "Example: restart listener \"RW Service\" \"RW Listener\"",
        {ARG_TYPE_SERVICE, ARG_TYPE_OBJECT_NAME}
    },
    { EMPTY_OPTION }
};

static void set_server(DCB *dcb, SERVER *server, char *bit);
static void set_pollsleep(DCB *dcb, int);
static void set_nbpoll(DCB *dcb, int);
static void set_log_throttling(DCB *dcb, int count, int window_ms, int suppress_ms);
/**
 * The subcommands of the set command
 */
struct subcommand setoptions[] =
{
    {
        "server", 2, 2, (FN)set_server,
        "Set the status of a server",
        "Usage: set server NAME STATUS\n"
        "\n"
        "Parameters:\n"
        "NAME   Server name\n"
        "STATUS The status to set\n"
        "\n"
        "Example: set server dbnode4 master",
        {ARG_TYPE_SERVER, ARG_TYPE_OBJECT_NAME}
    },
    {
        "pollsleep", 1, 1, (FN)set_pollsleep,
        "Set poll sleep period",
        "Deprecated in 2.3",
        {ARG_TYPE_NUMERIC}
    },
    {
        "nbpolls", 1, 1, (FN)set_nbpoll,
        "Set non-blocking polls",
        "Deprecated in 2.3",
        {ARG_TYPE_NUMERIC}
    },
    {
        "log_throttling", 3, 3, (FN)set_log_throttling,
        "Set the log throttling configuration",
        "Usage: set log_throttling COUNT WINDOW SUPPRESS\n"
        "\n"
        "Parameters:\n"
        "COUNT    Number of messages to log before throttling\n"
        "WINDOW   The time window in milliseconds where COUNT messages can be logged\n"
        "SUPPRESS The log suppression in milliseconds once COUNT messages have been logged\n"
        "\n"
        "Example: set log_throttling 5 1000 25000",
        {ARG_TYPE_NUMERIC, ARG_TYPE_NUMERIC, ARG_TYPE_NUMERIC}
    },
    { EMPTY_OPTION }
};

static void clear_server(DCB *dcb, SERVER *server, char *bit);
/**
 * The subcommands of the clear command
 */
struct subcommand clearoptions[] =
{
    {
        "server", 2, 2, (FN)clear_server,
        "Clear server status",
        "Usage: clear server NAME STATUS\n"
        "\n"
        "Parameters:\n"
        "NAME   Server name\n"
        "STATUS The status to clear\n"
        "\n"
        "Example: clear server dbnode2 master",
        {ARG_TYPE_SERVER, ARG_TYPE_OBJECT_NAME}
    },
    { EMPTY_OPTION }
};

static void reload_dbusers(DCB *dcb, SERVICE *service);

/**
 * The subcommands of the reload command
 */
struct subcommand reloadoptions[] =
{
    {
        "dbusers", 1, 1, (FN)reload_dbusers,
        "Reload the database users for a service",
        "Usage: reload dbusers SERVICE\n"
        "\n"
        "Parameters:\n"
        "SERVICE Reload database users for this service\n"
        "\n"
        "Example: reload dbusers \"splitter service\"",
        {ARG_TYPE_SERVICE}
    },
    { EMPTY_OPTION }
};

static void enable_log_priority(DCB *, char *);
static void disable_log_priority(DCB *, char *);
static void enable_sess_log_priority(DCB *dcb, char *arg1, char *arg2);
static void disable_sess_log_priority(DCB *dcb, char *arg1, char *arg2);
static void enable_service_root(DCB *dcb, SERVICE *service);
static void disable_service_root(DCB *dcb, SERVICE *service);
static void enable_syslog();
static void disable_syslog();
static void enable_maxlog();
static void disable_maxlog();
static void enable_account(DCB *, char *user);
static void enable_admin_account(DCB *, char *user);
static void disable_account(DCB *, char *user);

/**
 *  * The subcommands of the enable command
 *   */
struct subcommand enableoptions[] =
{
    {
        "log-priority",
        1, 1,
        (FN)enable_log_priority,
        "Enable a logging priority",
        "Usage: enable log-priority PRIORITY\n"
        "\n"
        "Parameters:"
        "PRIORITY One of 'err', 'warning', 'notice','info' or 'debug'\n"
        "\n"
        "Example: enable log-priority info",
        {ARG_TYPE_OBJECT_NAME}
    },
    {
        "sessionlog-priority",
        2, 2,
        (FN)enable_sess_log_priority,
        "[Deprecated] Enable a logging priority for a session",
        "This command is deprecated",
        {ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME}
    },
    {
        "root",
        1, 1,
        (FN)enable_service_root,
        "Enable root user access to a service",
        "Usage: enable root SERVICE\n"
        "\n"
        "Parameters:\n"
        "SERVICE Service where root user is enabled\n"
        "\n"
        "Example: enable root my-service",
        {ARG_TYPE_SERVICE}
    },
    {
        "syslog",
        0, 0,
        (FN)enable_syslog,
        "Enable syslog logging",
        "Usage: enable syslog",
        {0}
    },
    {
        "maxlog",
        0, 0,
        (FN)enable_maxlog,
        "Enable MaxScale logging",
        "Usage: enable maxlog",
        {0}
    },
    {
        "account",
        1, 1,
        (FN)enable_admin_account,
        "Activate a Linux user account for administrative MaxAdmin use",
        "Usage: enable account USER\n"
        "\n"
        "Parameters:\n"
        "USER The user account to enable\n"
        "\n"
        "Example: enable account alice",
        {ARG_TYPE_OBJECT_NAME}
    },
    {
        "readonly-account",
        1, 1,
        (FN)enable_account,
        "Activate a Linux user account for read-only MaxAdmin use",
        "Usage: enable account USER\n"
        "\n"
        "Parameters:\n"
        "USER The user account to enable\n"
        "\n"
        "Example: enable account alice",
        {ARG_TYPE_OBJECT_NAME}
    },
    {
        EMPTY_OPTION
    }
};



/**
 *  * The subcommands of the disable command
 *   */
struct subcommand disableoptions[] =
{
    {
        "log-priority",
        1, 1,
        (FN)disable_log_priority,
        "Disable a logging priority",
        "Usage: disable log-priority PRIORITY\n"
        "\n"
        "Parameters:"
        "PRIORITY One of 'err', 'warning', 'notice','info' or 'debug'\n"
        "\n"
        "Example: disable log-priority info",
        {ARG_TYPE_OBJECT_NAME}
    },
    {
        "sessionlog-priority",
        2, 2,
        (FN)disable_sess_log_priority,
        "[Deprecated] Disable a logging priority for a particular session",
        "This command is deprecated",
        {ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME}
    },
    {
        "root",
        1, 1,
        (FN)disable_service_root,
        "Disable root access",
        "Usage: disable root SERVICE\n"
        "\n"
        "Parameters:\n"
        "SERVICE Service where root user is disabled\n"
        "\n"
        "Example: disable root my-service",
        {ARG_TYPE_SERVICE}
    },
    {
        "syslog",
        0, 0,
        (FN)disable_syslog,
        "Disable syslog logging",
        "Usage: disable syslog",
        {0}
    },
    {
        "maxlog",
        0, 0,
        (FN)disable_maxlog,
        "Disable MaxScale logging",
        "Usage: disable maxlog",
        {0}
    },
    {
        "account",
        1, 1,
        (FN)disable_account,
        "Disable Linux user",
        "Usage: disable account USER\n"
        "\n"
        "Parameters:\n"
        "USER The user account to disable\n"
        "\n"
        "Example: disable account alice",
        {ARG_TYPE_OBJECT_NAME}
    },
    {
        EMPTY_OPTION
    }
};

static void inet_add_user(DCB *, char *user, char *password);
static void inet_add_admin_user(DCB *, char *user, char *password);

static void cmd_AddServer(DCB *dcb, SERVER *server, char *v1, char *v2, char *v3,
                          char *v4, char *v5, char *v6, char *v7, char *v8, char *v9,
                          char *v10, char *v11)
{
    char *values[11] = {v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11};
    const int items = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < items && values[i]; i++)
    {
        if (runtime_link_server(server, values[i]))
        {
            dcb_printf(dcb, "Added server '%s' to '%s'\n", server->name, values[i]);
        }
        else
        {
            dcb_printf(dcb, "Could not add server '%s' to object '%s'. See error log for more details.\n",
                       server->name, values[i]);
        }
    }
}

/**
 * The subcommands of the ping command.
 */
void ping_workers(DCB* dcb)
{
    int n = mxs_rworker_broadcast_message(MXS_WORKER_MSG_PING, 0, 0);

    dcb_printf(dcb, "Broadcasted ping message to %d workers.\n", n);
}

struct subcommand pingoptions[] =
{
    {
        "workers", 0, 0, (FN)ping_workers,
        "Ping Workers",
        "Ping Workers",
        {ARG_TYPE_NONE}
    },
    { EMPTY_OPTION }
};

/**
 * The subcommands of the add command
 */
struct subcommand addoptions[] =
{
    {
        "user", 2, 2, (FN)inet_add_admin_user,
        "Add an administrative account for using maxadmin over the network",
        "Usage: add user USER PASSWORD\n"
        "\n"
        "Parameters:\n"
        "USER     User to add\n"
        "PASSWORD Password for the user\n"
        "\n"
        "Example: add user bob somepass",
        {ARG_TYPE_OBJECT_NAME, ARG_TYPE_STRING}
    },
    {
        "readonly-user", 2, 2, (FN)inet_add_user,
        "Add a read-only account for using maxadmin over the network",
        "Usage: add user USER PASSWORD\n"
        "\n"
        "Parameters:\n"
        "USER     User to add\n"
        "PASSWORD Password for the user\n"
        "\n"
        "Example: add user bob somepass",
        {ARG_TYPE_OBJECT_NAME, ARG_TYPE_STRING}
    },
    {
        "server", 2, 12, (FN)cmd_AddServer,
        "Add a new server to a service",
        "Usage: add server SERVER TARGET...\n"
        "\n"
        "Parameters:\n"
        "SERVER  The server that is added to TARGET\n"
        "TARGET  List of service and/or monitor names separated by spaces\n"
        "\n"
        "A server can be assigned to a maximum of 11 objects in one command\n"
        "\n"
        "Example: add server my-db my-service \"Cluster Monitor\"",
        {
            ARG_TYPE_SERVER, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME,
            ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME,
            ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME
        }
    },
    { EMPTY_OPTION}
};


static void telnetdRemoveUser(DCB *, char *user);

static void cmd_RemoveServer(DCB *dcb, SERVER *server, char *v1, char *v2, char *v3,
                             char *v4, char *v5, char *v6, char *v7, char *v8, char *v9,
                             char *v10, char *v11)
{
    char *values[11] = {v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11};
    const int items = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < items && values[i]; i++)
    {
        if (runtime_unlink_server(server, values[i]))
        {
            dcb_printf(dcb, "Removed server '%s' from '%s'\n", server->name, values[i]);
        }
        else
        {
            dcb_printf(dcb, "No service or monitor with the name '%s'\n", values[i]);
        }
    }
}

/**
 * The subcommands of the remove command
 */
struct subcommand removeoptions[] =
{
    {
        "user",
        1, 1,
        (FN)telnetdRemoveUser,
        "Remove account for using maxadmin over the network",
        "Usage: remove user USER\n"
        "\n"
        "Parameters:\n"
        "USER     User to remove\n"
        "\n"
        "Example: remove user bob",
        {ARG_TYPE_STRING}
    },
    {
        "server", 2, 12, (FN)cmd_RemoveServer,
        "Remove a server from a service or a monitor",
        "Usage: remove server SERVER TARGET...\n"
        "\n"
        "Parameters:\n"
        "SERVER  The server that is removed from TARGET\n"
        "TARGET  List of service and/or monitor names separated by spaces\n"
        "\n"
        "A server can be removed from a maximum of 11 objects in one command\n"
        "\n"
        "Example: remove server my-db my-service \"Cluster Monitor\"",
        {
            ARG_TYPE_SERVER, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME,
            ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME,
            ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME
        }
    },
    {
        EMPTY_OPTION
    }
};

/**
 * User command to flush a single logfile
 *
 * @param pdcb          The stream to write output to
 * @param logname       The name of the log
 */
static void
flushlog(DCB *pdcb, char *logname)
{
    bool unrecognized = false;
    bool deprecated = false;

    if (!strcasecmp(logname, "error"))
    {
        deprecated = true;
    }
    else if (!strcasecmp(logname, "message"))
    {
        deprecated = true;
    }
    else if (!strcasecmp(logname, "trace"))
    {
        deprecated = true;
    }
    else if (!strcasecmp(logname, "debug"))
    {
        deprecated = true;
    }
    else if (!strcasecmp(logname, "maxscale"))
    {
        ; // nop
    }
    else
    {
        unrecognized = true;
    }

    if (unrecognized)
    {
        dcb_printf(pdcb, "Unexpected logfile name '%s', expected: 'maxscale'.\n", logname);
    }
    else
    {
        mxs_log_rotate();

        if (deprecated)
        {
            dcb_printf(pdcb,
                       "'%s' is deprecated, currently there is only one log 'maxscale', "
                       "which was rotated.\n", logname);
        }
    }
}

/**
 * User command to flush all logfiles
 *
 * @param pdcb          The stream to write output to
 */
static void
flushlogs(DCB *pdcb)
{
    mxs_log_rotate();
}


/**
 * The subcommands of the flush command
 */
struct subcommand flushoptions[] =
{
    {
        "log",
        1, 1,
        (FN)flushlog,
        "Flush the content of a log file and reopen it",
        "Usage: flush log",
        {ARG_TYPE_STRING}
    },
    {
        "logs",
        0, 0,
        (FN)flushlogs,
        "Flush the content of a log file and reopen it",
        "Usage: flush logs",
        {0}
    },
    {
        EMPTY_OPTION
    }
};

/** This is used to prevent concurrent creation or removal of servers */
static SPINLOCK server_mod_lock = SPINLOCK_INIT;

/**
 * Create a new server
 *
 * @param dcb Client DCB
 * @param name Server name
 * @param address Server network address
 * @param port Server port
 * @param protocol Protocol, NULL for default (MySQLBackend)
 * @param authenticator Authenticator module, NULL for default (MySQLBackendAuth)
 */
static void createServer(DCB *dcb, char *name, char *address, char *port,
                         char *protocol, char *authenticator)
{
    spinlock_acquire(&server_mod_lock);

    if (server_find_by_unique_name(name) == NULL)
    {
        if (runtime_create_server(name, address, port, protocol, authenticator))
        {
            dcb_printf(dcb, "Created server '%s'\n", name);
        }
        else
        {
            dcb_printf(dcb, "Failed to create new server, see log file for more details\n");
        }
    }
    else
    {
        dcb_printf(dcb, "Server '%s' already exists.\n", name);
    }

    spinlock_release(&server_mod_lock);
}

static void createListener(DCB *dcb, SERVICE *service, char *name, char *address,
                           char *port, char *protocol, char *authenticator,
                           char *authenticator_options, char *key, char *cert,
                           char *ca, char *version, char *depth, char *verify)
{
    if (runtime_create_listener(service, name, address, port, protocol,
                                authenticator, authenticator_options,
                                key, cert, ca, version, depth, verify))
    {
        dcb_printf(dcb, "Listener '%s' created\n", name);
    }
    else
    {
        dcb_printf(dcb, "Failed to create listener '%s', see log for more details\n", name);
    }
}

static void createMonitor(DCB *dcb, const char *name, const char *module)
{
    if (monitor_find(name))
    {
        dcb_printf(dcb, "Monitor '%s' already exists\n", name);
    }
    else if (runtime_create_monitor(name, module))
    {
        dcb_printf(dcb, "Created monitor '%s'\n", name);
    }
    else
    {
        dcb_printf(dcb, "Failed to create monitor '%s', see log for more details\n", name);
    }
}

struct subcommand createoptions[] =
{
    {
        "server", 2, 5, (FN)createServer,
        "Create a new server",
        "Usage: create server NAME HOST [PORT] [PROTOCOL] [AUTHENTICATOR]\n"
        "\n"
        "Parameters:\n"
        "NAME          Server name\n"
        "HOST          Server host address\n"
        "PORT          Server port (default 3306)\n"
        "PROTOCOL      Server protocol (default MySQLBackend)\n"
        "AUTHENTICATOR Authenticator module name (default MySQLAuth)\n"
        "\n"
        "The first two parameters are required, the others are optional.\n"
        "\n"
        "Example: create server my-db-1 192.168.0.102 3306",
        {
            ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME,
            ARG_TYPE_OBJECT_NAME
        }
    },
    {
        "listener", 2, 13, (FN)createListener,
        "Create a new listener for a service",
        "Usage: create listener SERVICE NAME [HOST] [PORT] [PROTOCOL] [AUTHENTICATOR] [OPTIONS]\n"
        "                       [SSL_KEY] [SSL_CERT] [SSL_CA] [SSL_VERSION] [SSL_VERIFY_DEPTH]\n"
        "                       [SSL_VERIFY_PEER_CERTIFICATE]\n"
        "\n"
        "Parameters\n"
        "SERVICE       Service where this listener is added\n"
        "NAME          Listener name\n"
        "HOST          Listener host address (default [::])\n"
        "PORT          Listener port (default 3306)\n"
        "PROTOCOL      Listener protocol (default MySQLClient)\n"
        "AUTHENTICATOR Authenticator module name (default MySQLAuth)\n"
        "OPTIONS       Options for the authenticator module\n"
        "SSL_KEY       Path to SSL private key\n"
        "SSL_CERT      Path to SSL certificate\n"
        "SSL_CA        Path to CA certificate\n"
        "SSL_VERSION   SSL version (default MAX)\n"
        "SSL_VERIFY_DEPTH Certificate verification depth\n"
        "SSL_VERIFY_PEER_CERTIFICATE Verify peer certificate\n"
        "\n"
        "The first two parameters are required, the others are optional.\n"
        "Any of the optional parameters can also have the value 'default'\n"
        "which will be replaced with the default value.\n"
        "\n"
        "Example: create listener my-service my-new-listener 192.168.0.101 4006",
        {
            ARG_TYPE_SERVICE, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME,
            ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME,
            ARG_TYPE_STRING, // Rest of the arguments are paths which can contain spaces
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING,
        }
    },
    {
        "monitor", 2, 2, (FN)createMonitor,
        "Create a new monitor",
        "Usage: create monitor NAME MODULE\n"
        "\n"
        "Parameters:\n"
        "NAME    Monitor name\n"
        "MODULE  Monitor module\n"
        "\n"
        "Example: create monitor my-monitor mysqlmon",
        {
            ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME
        }
    },
    {
        EMPTY_OPTION
    }
};

static void destroyServer(DCB *dcb, SERVER *server)
{
    /** Do this so that we don't directly access the server. Currently, the
     * destruction of a server does not free any memory and the server stays
     * valid. */
    char name[strlen(server->name) + 1];
    strcpy(name, server->name);

    if (runtime_destroy_server(server))
    {
        dcb_printf(dcb, "Destroyed server '%s'\n", name);
    }
    else
    {
        dcb_printf(dcb, "Failed to destroy server '%s', see log file for more details\n", name);
    }
}

static void destroyListener(DCB *dcb, SERVICE *service, const char *name)
{
    if (runtime_destroy_listener(service, name))
    {
        dcb_printf(dcb, "Destroyed listener '%s'\n", name);
    }
    else
    {
        dcb_printf(dcb, "Failed to destroy listener '%s', see log file for more details\n", name);
    }
}


static void destroyMonitor(DCB *dcb, MXS_MONITOR *monitor)
{
    char name[strlen(monitor->name) + 1];
    strcpy(name, monitor->name);

    if (runtime_destroy_monitor(monitor))
    {
        dcb_printf(dcb, "Destroyed monitor '%s'\n", name);
    }
    else
    {
        dcb_printf(dcb, "Failed to destroy monitor '%s', see log file for more details\n", name);
    }
}

struct subcommand destroyoptions[] =
{
    {
        "server", 1, 1, (FN)destroyServer,
        "Destroy a server",
        "Usage: destroy server NAME\n"
        "\n"
        "Parameters:\n"
        "NAME Server to destroy\n"
        "\n"
        "Example: destroy server my-db-1",
        {ARG_TYPE_SERVER}
    },
    {
        "listener", 2, 2, (FN)destroyListener,
        "Destroy a listener",
        "Usage: destroy listener SERVICE NAME\n"
        "\n"
        "Parameters:\n"
        "NAME Listener to destroy\n"
        "\n"
        "The listener is stopped and it will be removed on the next restart of MaxScale\n"
        "\n"
        "Example: destroy listener my-listener",
        {ARG_TYPE_SERVICE, ARG_TYPE_OBJECT_NAME}
    },
    {
        "monitor", 1, 1, (FN)destroyMonitor,
        "Destroy a monitor",
        "Usage: destroy monitor NAME\n"
        "\n"
        "Parameters:\n"
        "NAME Monitor to destroy\n"
        "\n"
        "The monitor is stopped and it will be removed on the next restart of MaxScale\n"
        "\n"
        "Example: destroy monitor my-monitor",
        {ARG_TYPE_MONITOR}
    },
    {
        EMPTY_OPTION
    }
};

/**
 * @brief Process multiple alter operations at once
 *
 * This is a somewhat ugly way to handle multiple key-value changes in one operation
 * with one function. This could be handled with a variadic function but the
 * required complexity would probably negate any benefits.
 */
static void alterServer(DCB *dcb, SERVER *server, char *v1, char *v2, char *v3,
                        char *v4, char *v5, char *v6, char *v7, char *v8, char *v9,
                        char *v10, char *v11, char *v12, char *v13)
{
    char *values[] = {v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13};
    const int items = sizeof(values) / sizeof(values[0]);
    CONFIG_CONTEXT *obj = NULL;
    char *ssl_key = NULL;
    char *ssl_cert = NULL;
    char *ssl_ca = NULL;
    char *ssl_version = NULL;
    char *ssl_depth = NULL;
    char *ssl_verify = NULL;
    bool enable = false;

    for (int i = 0; i < items && values[i]; i++)
    {
        char *key = values[i];
        char *value = strchr(key, '=');

        if (value)
        {
            *value++ = '\0';

            if (config_is_ssl_parameter(key))
            {
                if (strcmp("ssl_cert", key) == 0)
                {
                    ssl_cert = value;
                }
                else if (strcmp("ssl_ca_cert", key) == 0)
                {
                    ssl_ca = value;
                }
                else if (strcmp("ssl_key", key) == 0)
                {
                    ssl_key = value;
                }
                else if (strcmp("ssl_version", key) == 0)
                {
                    ssl_version = value;
                }
                else if (strcmp("ssl_cert_verify_depth", key) == 0)
                {
                    ssl_depth = value;
                }
                else if (strcmp("ssl_verify_peer_certificate", key) == 0)
                {
                    ssl_verify = value;
                }
                else
                {
                    enable = strcmp("ssl", key) == 0 && strcmp(value, "required") == 0;
                    /** Must be 'ssl' */
                }
            }
            else if (!runtime_alter_server(server, key, value))
            {
                dcb_printf(dcb, "Error: Bad key-value parameter: %s=%s\n", key, value);
            }
        }
        else
        {
            dcb_printf(dcb, "Error: not a key-value parameter: %s\n", values[i]);
        }
    }

    if (enable || ssl_ca)
    {
        if (enable && ssl_ca)
        {
            /** We have SSL parameters, try to process them */
            if (!runtime_enable_server_ssl(server, ssl_key, ssl_cert, ssl_ca,
                                           ssl_version, ssl_depth, ssl_verify))
            {
                dcb_printf(dcb, "Enabling SSL for server '%s' failed, see log "
                           "for more details.\n", server->name);
            }
        }
        else
        {
            dcb_printf(dcb, "Error: SSL configuration requires the following parameters:\n"
                       "ssl=required ssl_ca_cert=PATH\n");
        }
    }
}

static void alterMonitor(DCB *dcb, MXS_MONITOR *monitor, char *v1, char *v2, char *v3,
                         char *v4, char *v5, char *v6, char *v7, char *v8, char *v9,
                         char *v10, char *v11)
{
    char *values[11] = {v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11};
    const int items = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < items && values[i]; i++)
    {
        char *key = values[i];
        char *value = strchr(key, '=');

        if (value)
        {
            *value++ = '\0';

            if (!runtime_alter_monitor(monitor, key, value))
            {
                dcb_printf(dcb, "Error: Bad key-value parameter: %s=%s\n", key, value);
            }
        }
        else
        {
            dcb_printf(dcb, "Error: not a key-value parameter: %s\n", values[i]);
        }
    }

}

static void alterService(DCB *dcb, SERVICE *service, char *v1, char *v2, char *v3,
                         char *v4, char *v5, char *v6, char *v7, char *v8, char *v9,
                         char *v10, char *v11)
{
    char *values[11] = {v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11};
    const int items = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < items && values[i]; i++)
    {
        char *key = values[i];
        char *value = strchr(key, '=');

        if (value)
        {
            *value++ = '\0';

            if (!runtime_alter_service(service, key, value))
            {
                dcb_printf(dcb, "Error: Bad key-value parameter: %s=%s\n", key, value);
            }
        }
        else
        {
            dcb_printf(dcb, "Error: not a key-value parameter: %s\n", values[i]);
        }
    }
}

static void alterMaxScale(DCB *dcb, char *v1, char *v2, char *v3,
                          char *v4, char *v5, char *v6, char *v7, char *v8, char *v9,
                          char *v10, char *v11)
{
    char *values[11] = {v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11};
    const int items = sizeof(values) / sizeof(values[0]);

    for (int i = 0; i < items && values[i]; i++)
    {
        char *key = values[i];
        char *value = strchr(key, '=');

        if (value)
        {
            *value++ = '\0';

            if (!runtime_alter_maxscale(key, value))
            {
                dcb_printf(dcb, "Error: Bad key-value parameter: %s=%s\n", key, value);
            }
        }
        else
        {
            dcb_printf(dcb, "Error: not a key-value parameter: %s\n", values[i]);
        }
    }
}

struct subcommand alteroptions[] =
{
    {
        "server", 2, 14, (FN)alterServer,
        "Alter server parameters",
        "Usage: alter server NAME KEY=VALUE ...\n"
        "\n"
        "Parameters:\n"
        "NAME      Server name\n"
        "KEY=VALUE List of `key=value` pairs separated by spaces\n"
        "\n"
        "This will alter an existing parameter of a server. The accepted values for KEY are:\n"
        "\n"
        "address                     Server address\n"
        "port                        Server port\n"
        "monitoruser                 Monitor user for this server\n"
        "monitorpw                   Monitor password for this server\n"
        "ssl                         Enable SSL, value must be 'required'\n"
        "ssl_key                     Path to SSL private key\n"
        "ssl_cert                    Path to SSL certificate\n"
        "ssl_ca_cert                 Path to SSL CA certificate\n"
        "ssl_version                 SSL version\n"
        "ssl_cert_verify_depth       Certificate verification depth\n"
        "ssl_verify_peer_certificate Peer certificate verification\n"
        "persistpoolmax              Persisted connection pool size\n"
        "persistmaxtime              Persisted connection maximum idle time\n"
        "\n"
        "To configure SSL for a newly created server, the 'ssl', 'ssl_cert',\n"
        "'ssl_key' and 'ssl_ca_cert' parameters must be given at the same time.\n"
        "\n"
        "Example: alter server my-db-1 address=192.168.0.202 port=3307",
        {
            ARG_TYPE_SERVER, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING
        }
    },
    {
        "monitor", 2, 12, (FN)alterMonitor,
        "Alter monitor parameters",
        "Usage: alter monitor NAME KEY=VALUE ...\n"
        "\n"
        "Parameters:\n"
        "NAME      Monitor name\n"
        "KEY=VALUE List of `key=value` pairs separated by spaces\n"
        "\n"
        "All monitors support the following values for KEY:\n"
        "user                     Username used when connecting to servers\n"
        "password                 Password used when connecting to servers\n"
        "monitor_interval         Monitoring interval in milliseconds\n"
        "backend_connect_timeout  Server connection timeout in seconds\n"
        "backend_write_timeout    Server write timeout in seconds\n"
        "backend_read_timeout     Server read timeout in seconds\n"
        "backend_connect_attempts Number of re-connection attempts\n"
        "journal_max_age          Maximum age of server state journal\n"
        "script_timeout           Timeout in seconds for monitor scripts\n"
        "\n"
        "This will alter an existing parameter of a monitor. To remove parameters,\n"
        "pass an empty value for a key e.g. 'maxadmin alter monitor my-monitor my-key='\n"
        "\n"
        "Example: alter monitor my-monitor user=maxuser password=maxpwd",
        {
            ARG_TYPE_MONITOR, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING
        }
    },
    {
        "service", 2, 12, (FN)alterService,
        "Alter service parameters",
        "Usage: alter service NAME KEY=VALUE ...\n"
        "\n"
        "Parameters:\n"
        "NAME      Service name\n"
        "KEY=VALUE List of `key=value` pairs separated by spaces\n"
        "\n"
        "All services support the following values for KEY:\n"
        "user                          Username used when connecting to servers\n"
        "password                      Password used when connecting to servers\n"
        "enable_root_user              Allow root user access through this service\n"
        "max_retry_interval            Maximum restart retry interval\n"
        "max_connections               Maximum connection limit\n"
        "connection_timeout            Client idle timeout in seconds\n"
        "auth_all_servers              Retrieve authentication data from all servers\n"
        "strip_db_esc                  Strip escape characters from database names\n"
        "localhost_match_wildcard_host Match wildcard host to 'localhost' address\n"
        "version_string                The version string given to client connections\n"
        "weightby                      Weighting parameter name\n"
        "log_auth_warnings             Log authentication warnings\n"
        "retry_on_failure              Retry service start on failure\n"
        "\n"
        "Example: alter service my-service user=maxuser password=maxpwd",
        {
            ARG_TYPE_SERVICE, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING
        }
    },
    {
        "maxscale", 1, 11, (FN)alterMaxScale,
        "Alter maxscale parameters",
        "Usage: alter maxscale KEY=VALUE ...\n"
        "\n"
        "Parameters:\n"
        "KEY=VALUE List of `key=value` pairs separated by spaces\n"
        "\n"
        "The following configuration values can be altered:\n"
        "auth_connect_timeout         Connection timeout for permission checks\n"
        "auth_read_timeout            Read timeout for permission checks\n"
        "auth_write_timeout           Write timeout for permission checks\n"
        "admin_auth                   Enable admin interface authentication\n"
        "admin_log_auth_failures      Log admin interface authentication failures\n"
        "\n"
        "Example: alter maxscale auth_connect_timeout=10",
        {
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING
        }
    },
    {
        EMPTY_OPTION
    }
};

static void callModuleCommand(DCB *dcb, char *domain, char *id, char *v3,
                              char *v4, char *v5, char *v6, char *v7, char *v8, char *v9,
                              char *v10, char *v11, char *v12)
{
    const void *values[11] = {v3, v4, v5, v6, v7, v8, v9, v10, v11, v12};
    const int valuelen = sizeof(values) / sizeof(values[0]);
    int numargs = 0;

    while (numargs < valuelen && values[numargs])
    {
        numargs++;
    }

    const MODULECMD *cmd = modulecmd_find_command(domain, id);

    if (cmd)
    {
        MODULECMD_ARG *arg = modulecmd_arg_parse(cmd, numargs, values);

        if (arg)
        {
            json_t* output = NULL;

            bool succeeded = modulecmd_call_command(cmd, arg, &output);

            if (!succeeded && !output)
            {
                const char* s = modulecmd_get_error();
                ss_dassert(s);

                if (*s == 0)
                {
                    // No error had been set, so we add a default one.
                    modulecmd_set_error("%s", "Call to module command failed, see log file for more details.");
                }

                output = modulecmd_get_json_error();
            }

            if (output)
            {
                char* js = json_dumps(output, JSON_INDENT(4));
                dcb_printf(dcb, "%s\n", js);
                MXS_FREE(js);
            }

            json_decref(output);
            modulecmd_arg_free(arg);
        }
        else
        {
            dcb_printf(dcb, "Error: %s\n", modulecmd_get_error());
        }
    }
    else
    {
        dcb_printf(dcb, "Error: %s\n", modulecmd_get_error());
    }
}

struct subcommand calloptions[] =
{
    {
        "command", 2, 12, (FN)callModuleCommand,
        "Call module command",
        "Usage: call command MODULE COMMAND ARGS...\n"
        "\n"
        "Parameters:\n"
        "MODULE  The module name\n"
        "COMMAND The command to call\n"
        "ARGS... Arguments for the command\n"
        "\n"
        "To list all registered commands, run 'list commands'.\n"
        "\n"
        "Example: call command my-module my-command hello world!",
        {
            ARG_TYPE_OBJECT_NAME, ARG_TYPE_OBJECT_NAME, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING,
            ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING, ARG_TYPE_STRING
        }
    },
    {
        EMPTY_OPTION
    }
};

/**
 * The debug command table
 */
static struct
{
    const char         *cmd;
    struct  subcommand *options;
} cmds[] =
{
    { "add",        addoptions },
    { "remove",     removeoptions },
    { "create",     createoptions },
    { "destroy",    destroyoptions },
    { "alter",      alteroptions },
    { "set",        setoptions },
    { "clear",      clearoptions },
    { "disable",    disableoptions },
    { "enable",     enableoptions },
    { "flush",      flushoptions },
    { "list",       listoptions },
    { "reload",     reloadoptions },
    { "restart",    restartoptions },
    { "shutdown",   shutdownoptions },
    { "show",       showoptions },
    { "sync",       syncoptions },
    { "call",       calloptions },
    { "ping",       pingoptions },
    { NULL,         NULL    }
};

static bool command_requires_admin_privileges(const char* cmd)
{
    return strcmp(cmd, "list") != 0 && strcmp(cmd, "show") != 0;
}

/**
 * Convert a string argument to a numeric, observing prefixes
 * for number bases, e.g. 0x for hex, 0 for octal
 *
 * @param mode          The CLI mode
 * @param arg           The string representation of the argument
 * @param arg_type      The target type for the argument
 * @return The argument as a long integer
 */
static unsigned long
convert_arg(char *arg, int arg_type)
{
    unsigned long rval = 0;

    switch (arg_type)
    {
    case ARG_TYPE_ADDRESS:
        rval = (unsigned long)strtol(arg, NULL, 0);
        break;

    case ARG_TYPE_STRING:
        rval = (unsigned long)arg;
        break;

    case ARG_TYPE_OBJECT_NAME:
        fix_section_name(arg);
        rval = (unsigned long)arg;
        break;

    case ARG_TYPE_SERVICE:
        fix_section_name(arg);
        rval = (unsigned long)service_find(arg);
        break;

    case ARG_TYPE_SERVER:
        fix_section_name(arg);
        rval = (unsigned long)server_find_by_unique_name(arg);
        break;

    case ARG_TYPE_SESSION:
        rval = (unsigned long)session_get_by_id(strtoul(arg, NULL, 0));
        break;

    case ARG_TYPE_MONITOR:
        fix_section_name(arg);
        rval = (unsigned long)monitor_find(arg);
        break;

    case ARG_TYPE_FILTER:
        fix_section_name(arg);
        rval = (unsigned long)filter_def_find(arg);
        break;

    case ARG_TYPE_NUMERIC:

        for (int i = 0; arg[i]; i++)
        {
            if (isdigit(arg[i]))
            {
                break;
            }
        }
        rval = atoi(arg);
    }

    return rval;
}

static void free_arg(int arg_type, void *value)
{
    switch (arg_type)
    {
    case ARG_TYPE_SESSION:
        session_put_ref(static_cast<MXS_SESSION*>(value));
        break;

    default:
        break;
    }
}

static bool user_is_authorized(DCB* dcb)
{
    bool rval = true;

    if (strcmp(dcb->remote, "localhost") == 0)
    {
        if (!admin_user_is_unix_admin(dcb->user))
        {
            rval = false;
        }
    }
    else
    {
        if (!admin_user_is_inet_admin(dcb->user))
        {
            rval = false;
        }
    }

    return rval;
}

static SPINLOCK debugcmd_lock = SPINLOCK_INIT;

static const char item_separator[] =
    "----------------------------------------------------------------------------\n";

/**
 * We have a complete line from the user, lookup the commands and execute them
 *
 * Commands are tokenised based on white space and then the first
 * word is checked againts the cmds table. If a match is found the
 * second word is compared to the different options for that command.
 *
 * Commands may also take up to 3 additional arguments, these are all
 * assumed to the numeric values and will be converted before being passed
 * to the handler function for the command.
 *
 * @param cli           The CLI_SESSION
 * @return      Returns 0 if the interpreter should exit
 */
int
execute_cmd(CLI_SESSION *cli)
{
    DCB           *dcb = cli->session->client_dcb;
    int            argc, i, j, found = 0;
    char          *args[MAXARGS + 4];
    int            in_quotes = 0, escape_next = 0;
    char          *ptr, *lptr;
    bool           in_space = false;
    int            nskip = 0;

    args[0] = trim_leading(cli->cmdbuf);
    ptr = args[0];
    lptr = ptr;
    i = 1;
    /*
     * Break the command line into a number of words. Whitespace is used
     * to delimit words and may be escaped by use of the \ character or
     * the use of double quotes.
     * The array args contains the broken down words, one per index.
     */

    while (*ptr && i <= MAXARGS + 2)
    {
        if (escape_next)
        {
            *lptr++ = *ptr++;
            escape_next = 0;
        }
        else if (*ptr == '\\')
        {
            escape_next = 1;
            ptr++;
        }
        else if (in_quotes == 0 && ((in_space = *ptr == ' ') || *ptr == '\t' || *ptr == '\r' || *ptr == '\n'))
        {

            *lptr = 0;
            lptr += nskip;
            nskip = 0;

            if (!in_space)
            {
                break;
            }

            args[i++] = ptr + 1;
            ptr++;
            lptr++;
        }
        else if (*ptr == '\"' && in_quotes == 0)
        {
            in_quotes = 1;
            ptr++;
            nskip++;
        }
        else if (*ptr == '\"' && in_quotes == 1)
        {
            in_quotes = 0;
            ptr++;
            nskip++;
        }
        else
        {
            *lptr++ = *ptr++;
        }
    }
    *lptr = 0;
    args[i] = NULL;

    if (args[0] == NULL || *args[0] == 0)
    {
        return 1;
    }

    argc = i - 2;   /* The number of extra arguments to commands */

    spinlock_acquire(&debugcmd_lock);

    if (!strcasecmp(args[0], "help"))
    {
        if (args[1] == NULL || *args[1] == 0)
        {
            found = 1;
            dcb_printf(dcb, "Available commands:\n");
            for (i = 0; cmds[i].cmd; i++)
            {
                dcb_printf(dcb, "%s:\n", cmds[i].cmd);

                for (j = 0; cmds[i].options[j].arg1; j++)
                {
                    dcb_printf(dcb, "    %s %s - %s\n", cmds[i].cmd,
                               cmds[i].options[j].arg1, cmds[i].options[j].help);
                }
                dcb_printf(dcb, "\n");
            }
            dcb_printf(dcb, "\nType `help COMMAND` to see details of each command.\n");
            dcb_printf(dcb, "Where commands require names as arguments and these names contain\n");
            dcb_printf(dcb, "whitespace either the \\ character may be used to escape the whitespace\n");
            dcb_printf(dcb, "or the name may be enclosed in double quotes \".\n\n");
        }
        else
        {
            for (i = 0; cmds[i].cmd; i++)
            {
                if (!strcasecmp(args[1], cmds[i].cmd))
                {
                    found = 1;
                    dcb_printf(dcb, "Available options to the `%s` command:\n", cmds[i].cmd);
                    for (j = 0; cmds[i].options[j].arg1; j++)
                    {
                        if (j != 0)
                        {
                            dcb_printf(dcb, item_separator);
                        }

                        dcb_printf(dcb, "\n%s %s - %s\n\n%s\n\n", cmds[i].cmd, cmds[i].options[j].arg1,
                                   cmds[i].options[j].help, cmds[i].options[j].devhelp);

                    }
                }
            }
            if (found == 0)
            {
                dcb_printf(dcb, "No command %s to offer help with\n", args[1]);
            }
        }
        found = 1;
    }
    else if (strcasecmp(args[0], "quit") && argc >= 0)
    {
        for (i = 0; cmds[i].cmd; i++)
        {
            if (strcasecmp(args[0], cmds[i].cmd) == 0)
            {
                for (j = 0; cmds[i].options[j].arg1; j++)
                {
                    if (strcasecmp(args[1], cmds[i].options[j].arg1) == 0)
                    {
                        found = 1; /**< command and sub-command match */

                        if (command_requires_admin_privileges(cmds[i].cmd) &&
                            !user_is_authorized(dcb))
                        {
                            dcb_printf(dcb, "Access denied, administrative privileges required.\n");
                            break;
                        }

                        if (cmds[i].options[j].argc_min == cmds[i].options[j].argc_max &&
                            argc != cmds[i].options[j].argc_min)
                        {
                            /** Wrong number of arguments */
                            dcb_printf(dcb, "Incorrect number of arguments: %s %s expects %d arguments\n",
                                       cmds[i].cmd, cmds[i].options[j].arg1,
                                       cmds[i].options[j].argc_min);
                        }
                        else if (argc < cmds[i].options[j].argc_min)
                        {
                            /** Not enough arguments */
                            dcb_printf(dcb, "Incorrect number of arguments: %s %s expects at least %d arguments\n",
                                       cmds[i].cmd, cmds[i].options[j].arg1,
                                       cmds[i].options[j].argc_min);
                        }
                        else if (argc > cmds[i].options[j].argc_max)
                        {
                            /** Too many arguments */
                            dcb_printf(dcb, "Incorrect number of arguments: %s %s expects at most %d arguments\n",
                                       cmds[i].cmd, cmds[i].options[j].arg1,
                                       cmds[i].options[j].argc_max);
                        }
                        else
                        {
                            unsigned long arg_list[MAXARGS] = {};
                            bool ok = true;

                            for (int k = 0; k < cmds[i].options[j].argc_max && k < argc; k++)
                            {
                                arg_list[k] = convert_arg(args[k + 2], cmds[i].options[j].arg_types[k]);
                                if (arg_list[k] == 0)
                                {
                                    dcb_printf(dcb, "Invalid argument: %s\n", args[k + 2]);
                                    ok = false;
                                }
                            }

                            if (ok)
                            {
                                switch (cmds[i].options[j].argc_max)
                                {
                                case 0:
                                    cmds[i].options[j].fn(dcb);
                                    break;
                                case 1:
                                    ((FN1)cmds[i].options[j].fn)(dcb, arg_list[0]);
                                    break;
                                case 2:
                                    ((FN2)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1]);
                                    break;
                                case 3:
                                    ((FN3)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2]);
                                    break;
                                case 4:
                                    ((FN4)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                 arg_list[3]);
                                    break;
                                case 5:
                                    ((FN5)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                 arg_list[3], arg_list[4]);
                                     break;
                                case 6:
                                     ((FN6)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                  arg_list[3], arg_list[4], arg_list[5]);
                                     break;
                                case 7:
                                     ((FN7)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                  arg_list[3], arg_list[4], arg_list[5],
                                                                  arg_list[6]);
                                    break;
                                case 8:
                                     ((FN8)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                  arg_list[3], arg_list[4], arg_list[5],
                                                                  arg_list[6], arg_list[7]);
                                    break;
                                case 9:
                                     ((FN9)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                  arg_list[3], arg_list[4], arg_list[5],
                                                                  arg_list[6], arg_list[7], arg_list[8]);
                                    break;
                                case 10:
                                     ((FN10)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                   arg_list[3], arg_list[4], arg_list[5],
                                                                   arg_list[6], arg_list[7], arg_list[8],
                                                                   arg_list[9]);
                                    break;
                                case 11:
                                     ((FN11)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                   arg_list[3], arg_list[4], arg_list[5],
                                                                   arg_list[6], arg_list[7], arg_list[8],
                                                                   arg_list[9], arg_list[10]);
                                    break;
                                case 12:
                                     ((FN12)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                   arg_list[3], arg_list[4], arg_list[5],
                                                                   arg_list[6], arg_list[7], arg_list[8],
                                                                   arg_list[9], arg_list[10], arg_list[11]);
                                    break;
                                case 13:
                                    ((FN13)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                  arg_list[3], arg_list[4], arg_list[5],
                                                                  arg_list[6], arg_list[7], arg_list[8],
                                                                  arg_list[9], arg_list[10], arg_list[11],
                                                                  arg_list[12]);
                                    break;
                                case 14:
                                    ((FN14)cmds[i].options[j].fn)(dcb, arg_list[0], arg_list[1], arg_list[2],
                                                                  arg_list[3], arg_list[4], arg_list[5],
                                                                  arg_list[6], arg_list[7], arg_list[8],
                                                                  arg_list[9], arg_list[10], arg_list[11],
                                                                  arg_list[12], arg_list[13]);
                                    break;
                                default:
                                    dcb_printf(dcb, "Error: Maximum argument count is %d.\n", MAXARGS);
                                    ss_info_dassert(!true, "Command has too many arguments");
                                    break;
                                }
                            }

                            for (int k = 0; k < cmds[i].options[j].argc_max && k < argc; k++)
                            {
                                free_arg(cmds[i].options[j].arg_types[k], (void*)arg_list[k]);
                            }
                        }
                    }
                }
                if (!found)
                {
                    dcb_printf(dcb,
                               "Unknown or missing option for the %s command. Valid sub-commands are:\n",
                               cmds[i].cmd);
                    for (j = 0; cmds[i].options[j].arg1; j++)
                    {
                        dcb_printf(dcb, "    %-10s %s\n", cmds[i].options[j].arg1,
                                   cmds[i].options[j].help);
                    }
                    found = 1;
                }
            }
        }
    }
    else if (argc == -1)
    {
        dcb_printf(dcb,
                   "Commands must consist of at least two words. Type help for a list of commands\n");
        found = 1;
    }
    if (!found)
    {
        dcb_printf(dcb,
                   "Command '%s' not known, type help for a list of available commands\n", args[0]);
    }

    spinlock_release(&debugcmd_lock);

    memset(cli->cmdbuf, 0, CMDBUFLEN);

    return 1;
}

/**
 * Debug command to stop a service
 *
 * @param dcb           The DCB to print any output to
 * @param service       The service to shutdown
 */
static void
shutdown_service(DCB *dcb, SERVICE *service)
{
    serviceStop(service);
}

/**
 * Debug command to restart a stopped service
 *
 * @param dcb           The DCB to print any output to
 * @param service       The service to restart
 */
static void
restart_service(DCB *dcb, SERVICE *service)
{
    serviceStart(service);
}

/**
 * Set the status bit of a server
 *
 * @param dcb           DCB to send output to
 * @param server        The server to set the status of
 * @param bit           String representation of the status bit
 */
static void
set_server(DCB *dcb, SERVER *server, char *bit)
{
    unsigned int bitvalue;

    if ((bitvalue = server_map_status(bit)) != 0)
    {
        std::string errmsg;
        if (!mxs::server_set_status(server, bitvalue, &errmsg))
        {
            dcb_printf(dcb, "%s\n", errmsg.c_str());
        }
    }
    else
    {
        dcb_printf(dcb, "Unknown status bit %s\n", bit);
    }
}


/**
 * Clear the status bit of a server
 *
 * @param dcb           DCB to send output to
 * @param server        The server to set the status of
 * @param bit           String representation of the status bit
 */
static void
clear_server(DCB *dcb, SERVER *server, char *bit)
{
    unsigned int bitvalue;

    if ((bitvalue = server_map_status(bit)) != 0)
    {
        std::string errmsg;
        if (!mxs::server_clear_status(server, bitvalue, &errmsg))
        {
            dcb_printf(dcb, "%s", errmsg.c_str());
        }
    }
    else
    {
        dcb_printf(dcb, "Unknown status bit %s\n", bit);
    }
}

/**
 * Reload the authenticaton data from the backend database of a service.
 *
 * @param dcb           DCB to send output
 * @param service       The service to update
 */
static void
reload_dbusers(DCB *dcb, SERVICE *service)
{
    if (service_refresh_users(service) == 0)
    {
        dcb_printf(dcb, "Reloaded database users for service %s.\n", service->name);
    }
    else
    {
        dcb_printf(dcb, "Error: Failed to reloaded database users for service %s.\n", service->name);
    }
}

/**
 * Add a new remote (insecure, over the network) maxscale admin user
 *
 * @param dcb  The DCB for messages
 * @param user The user name
 * @param user The user password
 */
static void do_inet_add_user(DCB *dcb, char *user, char *password, enum user_account_type type)
{
    const char *err;

    if (admin_inet_user_exists(user))
    {
        dcb_printf(dcb, "Account %s for remote (network) usage already exists.\n", user);
        return;
    }

    if ((err = admin_add_inet_user(user, password, type)) == NULL)
    {
        dcb_printf(dcb, "Account %s for remote (network) usage has been successfully added.\n", user);
    }
    else
    {
        dcb_printf(dcb, "Failed to add new remote account %s: %s.\n", user, err);
    }
}

static void inet_add_user(DCB *dcb, char *user, char *password)
{
    if (admin_have_admin())
    {
        do_inet_add_user(dcb, user, password, USER_ACCOUNT_BASIC);
    }
    else
    {
        dcb_printf(dcb, "No admin user created, create an admin account first\n"
                   "by executing `add admin USER PASSWORD`\n");
    }
}

static void inet_add_admin_user(DCB *dcb, char *user, char *password)
{
    do_inet_add_user(dcb, user, password, USER_ACCOUNT_ADMIN);
}

/**
 * Remove a remote (insecure, over the network) maxscale admin user
 *
 * @param dcb  The DCB for messages
 * @param user The user name
 * @param user The user password
 */
static void telnetdRemoveUser(DCB *dcb, char *user)
{
    const char* err;

    if (!admin_inet_user_exists(user))
    {
        dcb_printf(dcb, "Account '%s' for remote usage does not exist.\n", user);
    }
    else if (admin_is_last_admin(user))
    {
        dcb_printf(dcb, "Cannot remove the last admin account '%s'.\n", user);
    }
    else if ((err = admin_remove_inet_user(user)))
    {
        dcb_printf(dcb, "Failed to remove remote account '%s': %s\n", user, err);
    }
    else
    {
        dcb_printf(dcb, "Account '%s' for remote usage has been successfully removed.\n", user);
    }
}


/**
 * Print the adminsitration users
 *
 * @param dcb   The DCB to print the user data to
 */
static void
telnetdShowUsers(DCB *dcb)
{
    dcb_PrintAdminUsers(dcb);
}

/**
 * Print the log throttling state
 *
 * @param dcb The DCB to print the state to.
 */
static void
show_log_throttling(DCB *dcb)
{
    MXS_LOG_THROTTLING t;
    mxs_log_get_throttling(&t);

    dcb_printf(dcb, "%lu %lu %lu\n", t.count, t.window_ms, t.suppress_ms);
}

/**
 * Command to shutdown a running monitor
 *
 * @param dcb   The DCB to use to print messages
 * @param monitor       The monitor to shutdown
 */
static void
shutdown_monitor(DCB *dcb, MXS_MONITOR *monitor)
{
    monitor_stop(monitor);
}

/**
 * Command to restart a stopped monitor
 *
 * @param dcb   The DCB to use to print messages
 * @param monitor       The monitor to restart
 */
static void
restart_monitor(DCB *dcb, MXS_MONITOR *monitor)
{
    monitor_start(monitor, monitor->parameters);
}

/**
 * Enable root access to a service
 *
 * @param dcb           Connection to user interface
 * @param service       The service
 */
static void
enable_service_root(DCB *dcb, SERVICE *service)
{
    serviceEnableRootUser(service, 1);
}

/**
 * Disable root access to a service
 *
 * @param dcb           Connection to user interface
 * @param service       The service
 */
static void
disable_service_root(DCB *dcb, SERVICE *service)
{
    serviceEnableRootUser(service, 0);
}

struct log_action_entry
{
    const char* name;
    int priority;
    const char* replacement;
};

struct log_priority_entry
{
    const char* name;
    int priority;
};

static int compare_log_priority_entries(const void* l, const void* r)
{
    const struct log_priority_entry* l_entry = (const struct log_priority_entry*) l;
    const struct log_priority_entry* r_entry = (const struct log_priority_entry*) r;

    return strcmp(l_entry->name, r_entry->name);
}

static int string_to_priority(const char* name)
{
    static const struct log_priority_entry LOG_PRIORITY_ENTRIES[] =
    {
        // NOTE: If you make changes to this array, ensure that it remains alphabetically ordered.
        { "debug",   LOG_DEBUG },
        { "info",    LOG_INFO },
        { "notice",  LOG_NOTICE },
        { "warning", LOG_WARNING },
    };

    const size_t N_LOG_PRIORITY_ENTRIES = sizeof(LOG_PRIORITY_ENTRIES) / sizeof(LOG_PRIORITY_ENTRIES[0]);

    struct log_priority_entry key = { name, -1 };
    void* value = bsearch(&key,
                                 LOG_PRIORITY_ENTRIES,
                                 N_LOG_PRIORITY_ENTRIES,
                                 sizeof(struct log_priority_entry),
                                 compare_log_priority_entries);

    struct log_priority_entry* result = static_cast<struct log_priority_entry*>(value);

    return result ? result->priority : -1;
}

/**
 * Enables a log priority for a single session
 * @param session The session in question
 * @param dcb Client DCB
 * @param type Which log to enable
 */
static void enable_sess_log_priority(DCB *dcb, char *arg1, char *arg2)
{
    MXS_WARNING("'enable sessionlog-priority' is deprecated.");
}

/**
 * Disable a log priority for a single session
 * @param session The session in question
 * @param dcb Client DCB
 * @param type Which log to enable
 */
static void disable_sess_log_priority(DCB *dcb, char *arg1, char *arg2)
{
    MXS_WARNING("'disable sessionlog-priority' is deprecated.");
}

/**
 * The log-priority enable action
 */

static void enable_log_priority(DCB *dcb, char *arg1)
{
    int priority = string_to_priority(arg1);

    if (priority != -1)
    {
        mxs_log_set_priority_enabled(priority, true);

#if !defined(SS_DEBUG)
        if (priority == LOG_DEBUG)
        {
            dcb_printf(dcb,
                       "Enabling '%s' has no effect, as MaxScale has been built in release mode.\n", arg1);
        }
#endif
    }
    else
    {
        dcb_printf(dcb, "'%s' is not a supported log priority.\n", arg1);
    }
}

/**
 * The log-priority disable action
 */

static void disable_log_priority(DCB *dcb, char *arg1)
{
    int priority = string_to_priority(arg1);

    if (priority != -1)
    {
        mxs_log_set_priority_enabled(priority, false);
    }
    else
    {
        dcb_printf(dcb, "'%s' is not a supported log priority.\n", arg1);
    }
}

/**
 * Set the duration of the sleep passed to the poll wait
 *
 * @param       dcb             DCB for output
 * @param       sleeptime       Sleep time in milliseconds
 */
static void
set_pollsleep(DCB *dcb, int sleeptime)
{
    // DEPRECATED in 2.3, remove in 2.4.
    dcb_printf(dcb, "The configuration parameter 'pollsleep' has been deprecated in 2.3.");
    poll_set_maxwait(sleeptime);
}

/**
 * Set the number of non-blockign spins to make
 *
 * @param       dcb             DCB for output
 * @param       nb              Number of spins
 */
static void
set_nbpoll(DCB *dcb, int nb)
{
    // DEPRECATED in 2.3, remove in 2.4.
    dcb_printf(dcb, "The configuration parameter 'nbpoll' has been deprecated in 2.3.");
    poll_set_nonblocking_polls(nb);
}

static void
set_log_throttling(DCB *dcb, int count, int window_ms, int suppress_ms)
{
    if ((count >= 0) || (window_ms >= 0) || (suppress_ms >= 0))
    {
        MXS_LOG_THROTTLING t = { static_cast<size_t>(count),
                                 static_cast<size_t>(window_ms),
                                 static_cast<size_t>(suppress_ms) };

        mxs_log_set_throttling(&t);
    }
    else
    {
        dcb_printf(dcb,
                   "set log_throttling expect 3 integers X Y Z, equal to or larger than 0, "
                   "where the X denotes how many times particular message may be logged "
                   "during a period of Y milliseconds before it is suppressed for Z milliseconds.");
    }
}

/**
 * Enable syslog logging.
 */
static void
enable_syslog()
{
    mxs_log_set_syslog_enabled(true);
}

/**
 * Disable syslog logging.
 */
static void
disable_syslog()
{
    mxs_log_set_syslog_enabled(false);
}

/**
 * Enable maxlog logging.
 */
static void
enable_maxlog()
{
    mxs_log_set_maxlog_enabled(true);
}

/**
 * Disable maxlog logging.
 */
static void
disable_maxlog()
{
    mxs_log_set_maxlog_enabled(false);
}

/**
 * Enable a Linux account
 *
 * @param dcb  The DCB for messages
 * @param user The Linux user name
 */
static void do_enable_account(DCB *dcb, char *user, enum user_account_type type)
{
    const char *err;

    if (admin_linux_account_enabled(user))
    {
        dcb_printf(dcb, "The Linux user %s has already been enabled.\n", user);
        return;
    }

    if ((err = admin_enable_linux_account(user, type)) == NULL)
    {
        dcb_printf(dcb, "The Linux user %s has successfully been enabled.\n", user);
    }
    else
    {
        dcb_printf(dcb, "Failed to enable the Linux user %s: %s\n", user, err);
    }
}

static void enable_account(DCB *dcb, char *user)
{
    if (admin_have_admin())
    {
        do_enable_account(dcb, user, USER_ACCOUNT_BASIC);
    }
    else
    {
        dcb_printf(dcb, "No admin user created, create an admin account first\n"
                   "by executing `enable admin-account USER PASSWORD`\n");
    }
}

/**
 * Enable a Linux account
 *
 * @param dcb  The DCB for messages
 * @param user The Linux user name
 */
static void enable_admin_account(DCB *dcb, char *user)
{
    do_enable_account(dcb, user, USER_ACCOUNT_ADMIN);
}

/**
 * Disable a Linux account
 *
 * @param dcb   The DCB for messages
 * @param user  The Linux user name
 */
static void
disable_account(DCB *dcb, char *user)
{
    const char* err;

    if (!admin_linux_account_enabled(user))
    {
        dcb_printf(dcb, "The Linux user '%s' has not been enabled.\n", user);
        return;
    }
    else if (admin_is_last_admin(user))
    {
        dcb_printf(dcb, "Cannot remove the last admin account '%s'.\n", user);
    }
    else if ((err = admin_disable_linux_account(user)))
    {
        dcb_printf(dcb, "Failed to disable the Linux user '%s': %s\n", user, err);
    }
    else
    {
        dcb_printf(dcb, "The Linux user '%s' has successfully been disabled.\n", user);
    }
}
