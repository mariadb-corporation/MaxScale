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
 * @file maxinfo_parse.c - Parse the limited set of SQL that the MaxScale
 * information schema can use
 *
 * @verbatim
 * Revision History
 *
 * Date     Who           Description
 * 17/02/15 Mark Riddoch  Initial implementation
 *
 * @endverbatim
 */

#include "maxinfo.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <maxscale/alloc.h>
#include <maxbase/atomic.h>
#include <maxscale/config.hh>
#include <maxscale/dcb.hh>
#include <maxscale/maxscale.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxscale/resultset.hh>
#include <maxscale/router.hh>
#include <maxscale/server.hh>
#include <maxscale/service.hh>
#include <maxscale/version.h>

#include "../../../core/internal/maxscale.hh"
#include "../../../core/internal/modules.hh"
#include "../../../core/internal/monitor.hh"
#include "../../../core/internal/poll.hh"
#include "../../../core/internal/session.hh"
#include "../../../core/internal/server.hh"
#include "../../../core/internal/service.hh"

static void exec_show(DCB* dcb, MAXINFO_TREE* tree);
static void exec_select(DCB* dcb, MAXINFO_TREE* tree);
static void exec_show_variables(DCB* dcb, MAXINFO_TREE* filter);
static void exec_show_status(DCB* dcb, MAXINFO_TREE* filter);
static int  maxinfo_pattern_match(const char* pattern, const char* str);
static void exec_flush(DCB* dcb, MAXINFO_TREE* tree);
static void exec_set(DCB* dcb, MAXINFO_TREE* tree);
static void exec_clear(DCB* dcb, MAXINFO_TREE* tree);
static void exec_shutdown(DCB* dcb, MAXINFO_TREE* tree);
static void exec_restart(DCB* dcb, MAXINFO_TREE* tree);
void        maxinfo_send_ok(DCB* dcb);
/**
 * Execute a parse tree and return the result set or runtime error
 *
 * @param dcb   The DCB that connects to the client
 * @param tree  The parse tree for the query
 */
void maxinfo_execute(DCB* dcb, MAXINFO_TREE* tree)
{
    switch (tree->op)
    {
    case MAXOP_SHOW:
        exec_show(dcb, tree);
        break;

    case MAXOP_SELECT:
        exec_select(dcb, tree);
        break;

    case MAXOP_FLUSH:
        exec_flush(dcb, tree);
        break;

    case MAXOP_SET:
        exec_set(dcb, tree);
        break;

    case MAXOP_CLEAR:
        exec_clear(dcb, tree);
        break;

    case MAXOP_SHUTDOWN:
        exec_shutdown(dcb, tree);
        break;

    case MAXOP_RESTART:
        exec_restart(dcb, tree);
        break;

    case MAXOP_TABLE:
    case MAXOP_COLUMNS:
    case MAXOP_LITERAL:
    case MAXOP_PREDICATE:
    case MAXOP_LIKE:
    case MAXOP_EQUAL:
    default:
        maxinfo_send_error(dcb, 0, "Unexpected operator in parse tree");
    }
}

/**
 * Fetch the list of services and stream as a result set
 *
 * @param dcb   DCB to which to stream result set
 * @param tree  Potential like clause (currently unused)
 */
static void exec_show_services(DCB* dcb, MAXINFO_TREE* tree)
{
    serviceGetList()->write(dcb);
}

/**
 * Fetch the list of listeners and stream as a result set
 *
 * @param dcb   DCB to which to stream result set
 * @param tree  Potential like clause (currently unused)
 */
static void exec_show_listeners(DCB* dcb, MAXINFO_TREE* tree)
{
    serviceGetListenerList()->write(dcb);
}

/**
 * Fetch the list of sessions and stream as a result set
 *
 * @param dcb   DCB to which to stream result set
 * @param tree  Potential like clause (currently unused)
 */
static void exec_show_sessions(DCB* dcb, MAXINFO_TREE* tree)
{
    sessionGetList()->write(dcb);
}

/**
 * Fetch the list of client sessions and stream as a result set
 *
 * @param dcb   DCB to which to stream result set
 * @param tree  Potential like clause (currently unused)
 */
static void exec_show_clients(DCB* dcb, MAXINFO_TREE* tree)
{
    sessionGetList()->write(dcb);
}

/**
 * Fetch the list of servers and stream as a result set
 *
 * @param dcb   DCB to which to stream result set
 * @param tree  Potential like clause (currently unused)
 */
static void exec_show_servers(DCB* dcb, MAXINFO_TREE* tree)
{
    Server::getList()->write(dcb);
}

/**
 * Fetch the list of modules and stream as a result set
 *
 * @param dcb   DCB to which to stream result set
 * @param tree  Potential like clause (currently unused)
 */
static void exec_show_modules(DCB* dcb, MAXINFO_TREE* tree)
{
    moduleGetList()->write(dcb);
}

/**
 * Fetch the list of monitors and stream as a result set
 *
 * @param dcb   DCB to which to stream result set
 * @param tree  Potential like clause (currently unused)
 */
static void exec_show_monitors(DCB* dcb, MAXINFO_TREE* tree)
{
    monitor_get_list()->write(dcb);
}

/**
 * Fetch the event times data
 *
 * @param dcb   DCB to which to stream result set
 * @param tree  Potential like clause (currently unused)
 */
static void exec_show_eventTimes(DCB* dcb, MAXINFO_TREE* tree)
{
    eventTimesGetList()->write(dcb);
}

/**
 * The table of show commands that are supported
 */
static struct
{
    const char* name;
    void        (* func)(DCB*, MAXINFO_TREE*);
} show_commands[] =
{
    {"variables",  exec_show_variables },
    {"status",     exec_show_status    },
    {"services",   exec_show_services  },
    {"listeners",  exec_show_listeners },
    {"sessions",   exec_show_sessions  },
    {"clients",    exec_show_clients   },
    {"servers",    exec_show_servers   },
    {"modules",    exec_show_modules   },
    {"monitors",   exec_show_monitors  },
    {"eventTimes", exec_show_eventTimes},
    {NULL,         NULL                }
};

/**
 * Execute a show command parse tree and return the result set or runtime error
 *
 * @param dcb   The DCB that connects to the client
 * @param tree  The parse tree for the query
 */
static void exec_show(DCB* dcb, MAXINFO_TREE* tree)
{
    int i;
    char errmsg[120];

    for (i = 0; show_commands[i].name; i++)
    {
        if (strcasecmp(show_commands[i].name, tree->value) == 0)
        {
            (*show_commands[i].func)(dcb, tree->right);
            return;
        }
    }
    if (strlen(tree->value) > 80)   // Prevent buffer overrun
    {
        tree->value[80] = 0;
    }
    sprintf(errmsg, "Unsupported show command '%s'", tree->value);
    maxinfo_send_error(dcb, 0, errmsg);
    MXS_NOTICE("%s", errmsg);
}

/**
 * Flush all logs to disk and rotate them.
 * @param dcb   The DCB that connects to the client
 * @param tree  The parse tree for the query
 */
void exec_flush_logs(DCB* dcb, MAXINFO_TREE* tree)
{
    mxs_log_rotate();
    maxinfo_send_ok(dcb);
}

/**
 * The table of flush commands that are supported
 */
static struct
{
    const char* name;
    void        (* func)(DCB*, MAXINFO_TREE*);
} flush_commands[] =
{
    {"logs", exec_flush_logs},
    {NULL,   NULL           }
};

/**
 * Execute a flush command parse tree and return the result set or runtime error
 *
 * @param dcb   The DCB that connects to the client
 * @param tree  The parse tree for the query
 */
static void exec_flush(DCB* dcb, MAXINFO_TREE* tree)
{
    int i;
    char errmsg[120];

    sprintf(errmsg, "Unsupported flush command '%s'", tree->value);
    if (!tree)
    {
        maxinfo_send_error(dcb, 0, errmsg);
        MXS_ERROR("%s", errmsg);
        return;
    }
    for (i = 0; flush_commands[i].name; i++)
    {
        if (strcasecmp(flush_commands[i].name, tree->value) == 0)
        {
            (*flush_commands[i].func)(dcb, tree->right);
            return;
        }
    }
    if (strlen(tree->value) > 80)   // Prevent buffer overrun
    {
        tree->value[80] = 0;
    }
    maxinfo_send_error(dcb, 0, errmsg);
    MXS_ERROR("%s", errmsg);
}

/**
 * Set the server status.
 * @param dcb Client DCB
 * @param tree Parse tree
 */
void exec_set_server(DCB* dcb, MAXINFO_TREE* tree)
{
    auto server = Server::find_by_unique_name(tree->value);
    char errmsg[120];

    if (server)
    {
        int status = SERVER::status_from_string(tree->right->value);
        if (status != 0)
        {
            std::string errmsgs;
            if (mxs::server_set_status(server, status, &errmsgs))
            {
                maxinfo_send_ok(dcb);
            }
            else
            {
                maxinfo_send_error(dcb, 0, errmsgs.c_str());
            }
        }
        else
        {
            if (strlen(tree->right->value) > 80)    // Prevent buffer overrun
            {
                tree->right->value[80] = 0;
            }
            sprintf(errmsg, "Invalid argument '%s'", tree->right->value);
            maxinfo_send_error(dcb, 0, errmsg);
        }
    }
    else
    {
        if (strlen(tree->value) > 80)   // Prevent buffer overrun
        {
            tree->value[80] = 0;
        }
        sprintf(errmsg, "Invalid argument '%s'", tree->value);
        maxinfo_send_error(dcb, 0, errmsg);
    }
}

/**
 * The table of set commands that are supported
 */
static struct
{
    const char* name;
    void        (* func)(DCB*, MAXINFO_TREE*);
} set_commands[] =
{
    {"server", exec_set_server},
    {NULL,     NULL           }
};

/**
 * Execute a set  command parse tree and return the result set or runtime error
 *
 * @param dcb   The DCB that connects to the client
 * @param tree  The parse tree for the query
 */
static void exec_set(DCB* dcb, MAXINFO_TREE* tree)
{
    int i;
    char errmsg[120];

    for (i = 0; set_commands[i].name; i++)
    {
        if (strcasecmp(set_commands[i].name, tree->value) == 0)
        {
            (*set_commands[i].func)(dcb, tree->right);
            return;
        }
    }
    if (strlen(tree->value) > 80)   // Prevent buffer overrun
    {
        tree->value[80] = 0;
    }
    sprintf(errmsg, "Unsupported set command '%s'", tree->value);
    maxinfo_send_error(dcb, 0, errmsg);
    MXS_ERROR("%s", errmsg);
}

/**
 * Clear the server status.
 * @param dcb Client DCB
 * @param tree Parse tree
 */
void exec_clear_server(DCB* dcb, MAXINFO_TREE* tree)
{
    auto server = Server::find_by_unique_name(tree->value);
    char errmsg[120];

    if (server)
    {
        int status = SERVER::status_from_string(tree->right->value);
        if (status != 0)
        {
            std::string errmsgs;
            if (mxs::server_clear_status(server, status, &errmsgs))
            {
                maxinfo_send_ok(dcb);
            }
            else
            {
                maxinfo_send_error(dcb, 0, errmsgs.c_str());
            }
        }
        else
        {
            if (strlen(tree->right->value) > 80)    // Prevent buffer overrun
            {
                tree->right->value[80] = 0;
            }
            sprintf(errmsg, "Invalid argument '%s'", tree->right->value);
            maxinfo_send_error(dcb, 0, errmsg);
        }
    }
    else
    {
        if (strlen(tree->value) > 80)   // Prevent buffer overrun
        {
            tree->value[80] = 0;
        }
        sprintf(errmsg, "Invalid argument '%s'", tree->value);
        maxinfo_send_error(dcb, 0, errmsg);
    }
}

/**
 * The table of clear commands that are supported
 */
static struct
{
    const char* name;
    void        (* func)(DCB*, MAXINFO_TREE*);
} clear_commands[] =
{
    {"server", exec_clear_server},
    {NULL,     NULL             }
};

/**
 * Execute a clear command parse tree and return the result set or runtime error
 *
 * @param dcb   The DCB that connects to the client
 * @param tree  The parse tree for the query
 */
static void exec_clear(DCB* dcb, MAXINFO_TREE* tree)
{
    int i;
    char errmsg[120];

    for (i = 0; clear_commands[i].name; i++)
    {
        if (strcasecmp(clear_commands[i].name, tree->value) == 0)
        {
            (*clear_commands[i].func)(dcb, tree->right);
            return;
        }
    }
    if (strlen(tree->value) > 80)   // Prevent buffer overrun
    {
        tree->value[80] = 0;
    }
    sprintf(errmsg, "Unsupported clear command '%s'", tree->value);
    maxinfo_send_error(dcb, 0, errmsg);
    MXS_ERROR("%s", errmsg);
}

/**
 * MaxScale shutdown
 * @param dcb Client DCB
 * @param tree Parse tree
 */
void exec_shutdown_maxscale(DCB* dcb, MAXINFO_TREE* tree)
{
    maxscale_shutdown();
    maxinfo_send_ok(dcb);
}

/**
 * Stop a monitor
 * @param dcb Client DCB
 * @param tree Parse tree
 */
void exec_shutdown_monitor(DCB* dcb, MAXINFO_TREE* tree)
{
    char errmsg[120];
    if (tree && tree->value)
    {
        Monitor* monitor = monitor_find(tree->value);
        if (monitor)
        {
            monitor_stop(monitor);
            maxinfo_send_ok(dcb);
        }
        else
        {
            if (strlen(tree->value) > 80)   // Prevent buffer overrun
            {
                tree->value[80] = 0;
            }
            sprintf(errmsg, "Invalid argument '%s'", tree->value);
            maxinfo_send_error(dcb, 0, errmsg);
        }
    }
    else
    {
        sprintf(errmsg, "Missing argument for 'SHUTDOWN MONITOR'");
        maxinfo_send_error(dcb, 0, errmsg);
    }
}

/**
 * Stop a service
 * @param dcb Client DCB
 * @param tree Parse tree
 */
void exec_shutdown_service(DCB* dcb, MAXINFO_TREE* tree)
{
    char errmsg[120];
    if (tree && tree->value)
    {
        SERVICE* service = service_find(tree->value);
        if (service)
        {
            serviceStop(service);
            maxinfo_send_ok(dcb);
        }
        else
        {
            if (strlen(tree->value) > 80)   // Prevent buffer overrun
            {
                tree->value[80] = 0;
            }
            sprintf(errmsg, "Invalid argument '%s'", tree->value);
            maxinfo_send_error(dcb, 0, errmsg);
        }
    }
    else
    {
        sprintf(errmsg, "Missing argument for 'SHUTDOWN SERVICE'");
        maxinfo_send_error(dcb, 0, errmsg);
    }
}

/**
 * The table of shutdown commands that are supported
 */
static struct
{
    const char* name;
    void        (* func)(DCB*, MAXINFO_TREE*);
} shutdown_commands[] =
{
    {"maxscale", exec_shutdown_maxscale},
    {"monitor",  exec_shutdown_monitor },
    {"service",  exec_shutdown_service },
    {NULL,       NULL                  }
};

/**
 * Execute a shutdown command parse tree and return OK or runtime error
 *
 * @param dcb   The DCB that connects to the client
 * @param tree  The parse tree for the query
 */
static void exec_shutdown(DCB* dcb, MAXINFO_TREE* tree)
{
    int i;
    char errmsg[120];

    for (i = 0; shutdown_commands[i].name; i++)
    {
        if (strcasecmp(shutdown_commands[i].name, tree->value) == 0)
        {
            (*shutdown_commands[i].func)(dcb, tree->right);
            return;
        }
    }
    if (strlen(tree->value) > 80)   // Prevent buffer overrun
    {
        tree->value[80] = 0;
    }
    sprintf(errmsg, "Unsupported shutdown command '%s'", tree->value);
    maxinfo_send_error(dcb, 0, errmsg);
    MXS_ERROR("%s", errmsg);
}

/**
 * Restart a monitor
 * @param dcb Client DCB
 * @param tree Parse tree
 */
void exec_restart_monitor(DCB* dcb, MAXINFO_TREE* tree)
{
    char errmsg[120];
    if (tree && tree->value)
    {
        Monitor* monitor = monitor_find(tree->value);
        if (monitor)
        {
            monitor_start(monitor, monitor->parameters);
            maxinfo_send_ok(dcb);
        }
        else
        {
            if (strlen(tree->value) > 80)   // Prevent buffer overrun
            {
                tree->value[80] = 0;
            }
            sprintf(errmsg, "Invalid argument '%s'", tree->value);
            maxinfo_send_error(dcb, 0, errmsg);
        }
    }
    else
    {
        sprintf(errmsg, "Missing argument for 'RESTART MONITOR'");
        maxinfo_send_error(dcb, 0, errmsg);
    }
}

/**
 * Restart a service
 * @param dcb Client DCB
 * @param tree Parse tree
 */
void exec_restart_service(DCB* dcb, MAXINFO_TREE* tree)
{
    char errmsg[120];
    if (tree && tree->value)
    {
        SERVICE* service = service_find(tree->value);
        if (service)
        {
            serviceStart(service);
            maxinfo_send_ok(dcb);
        }
        else
        {
            if (strlen(tree->value) > 80)   // Prevent buffer overrun
            {
                tree->value[80] = 0;
            }
            sprintf(errmsg, "Invalid argument '%s'", tree->value);
            maxinfo_send_error(dcb, 0, errmsg);
        }
    }
    else
    {
        sprintf(errmsg, "Missing argument for 'RESTART SERVICE'");
        maxinfo_send_error(dcb, 0, errmsg);
    }
}

/**
 * The table of restart commands that are supported
 */
static struct
{
    const char* name;
    void        (* func)(DCB*, MAXINFO_TREE*);
} restart_commands[] =
{
    {"monitor", exec_restart_monitor},
    {"service", exec_restart_service},
    {NULL,      NULL                }
};

/**
 * Execute a restart command parse tree and return OK or runtime error
 *
 * @param dcb   The DCB that connects to the client
 * @param tree  The parse tree for the query
 */
static void exec_restart(DCB* dcb, MAXINFO_TREE* tree)
{
    int i;
    char errmsg[120];

    for (i = 0; restart_commands[i].name; i++)
    {
        if (strcasecmp(restart_commands[i].name, tree->value) == 0)
        {
            (*restart_commands[i].func)(dcb, tree->right);
            return;
        }
    }
    if (strlen(tree->value) > 80)   // Prevent buffer overrun
    {
        tree->value[80] = 0;
    }
    sprintf(errmsg, "Unsupported restart command '%s'", tree->value);
    maxinfo_send_error(dcb, 0, errmsg);
    MXS_ERROR("%s", errmsg);
}

/**
 * Return the current MaxScale version
 *
 * @return The version string for MaxScale
 */
static char* getVersion()
{
    return const_cast<char*>(MAXSCALE_VERSION);
}

static const char* versionComment = "MariaDB MaxScale";
/**
 * Return the current MaxScale version
 *
 * @return The version string for MaxScale
 */
static char* getVersionComment()
{
    return const_cast<char*>(versionComment);
}

/**
 * Return the current MaxScale Home Directory
 *
 * @return The version string for MaxScale
 */
static char* getMaxScaleHome()
{
    return getenv("MAXSCALE_HOME");
}

/* The various methods to fetch the variables */
#define VT_STRING 1
#define VT_INT    2

typedef void* (* STATSFUNC)();
/**
 * Variables that may be sent in a show variables
 */
static struct
{
    const char* name;
    int         type;
    STATSFUNC   func;
} variables[] =
{
    {"version",            VT_STRING, (STATSFUNC)getVersion            },
    {"version_comment",    VT_STRING, (STATSFUNC)getVersionComment     },
    {"basedir",            VT_STRING, (STATSFUNC)getMaxScaleHome       },
    {"MAXSCALE_VERSION",   VT_STRING, (STATSFUNC)getVersion            },
    {"MAXSCALE_THREADS",   VT_INT,    (STATSFUNC)config_threadcount    },
    {"MAXSCALE_NBPOLLS",   VT_INT,    (STATSFUNC)config_nbpolls        },
    {"MAXSCALE_POLLSLEEP", VT_INT,    (STATSFUNC)config_pollsleep      },
    {"MAXSCALE_UPTIME",    VT_INT,    (STATSFUNC)maxscale_uptime       },
    {"MAXSCALE_SESSIONS",  VT_INT,    (STATSFUNC)serviceSessionCountAll},
    {NULL,                 0,         NULL                             }
};

std::string value_to_string(int type, STATSFUNC func)
{
    std::string value;

    if (type == VT_STRING)
    {
        if (char* str = (char*)func())
        {
            value = str;
        }
    }
    else
    {
        value = std::to_string((int64_t)func());
    }

    return value;
}

/**
 * Callback function to populate rows of the show variable
 * command
 *
 * @param data  The context point
 * @return  The next row or NULL if end of rows
 */
static void variable_row(std::unique_ptr<ResultSet>& set, const char* like)
{
    for (int i = 0; variables[i].name; i++)
    {
        if (!like || !maxinfo_pattern_match(like, variables[i].name))
        {
            set->add_row({variables[i].name, value_to_string(variables[i].type, variables[i].func)});
        }
    }
}

/**
 * Execute a show variables command applying an optional filter
 *
 * @param dcb     The DCB connected to the client
 * @param filter  A potential like clause or NULL
 */
static void exec_show_variables(DCB* dcb, MAXINFO_TREE* filter)
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Variable_name", "Value"});
    variable_row(set, filter ? filter->value : NULL);
    set->write(dcb);
}

/**
 * Return the show variables output a a result set
 *
 * @return Variables as a result set
 */
std::unique_ptr<ResultSet> maxinfo_variables()
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Variable_name", "Value"});
    variable_row(set, NULL);
    return set;
}

/**
 * Interface to dcb_count_by_usage for all dcbs
 */
static int maxinfo_all_dcbs()
{
    return dcb_count_by_usage(DCB_USAGE_ALL);
}

/**
 * Interface to dcb_count_by_usage for client dcbs
 */
static int maxinfo_client_dcbs()
{
    return dcb_count_by_usage(DCB_USAGE_CLIENT);
}

/**
 * Interface to dcb_count_by_usage for listener dcbs
 */
static int maxinfo_listener_dcbs()
{
    return dcb_count_by_usage(DCB_USAGE_LISTENER);
}

/**
 * Interface to dcb_count_by_usage for backend dcbs
 */
static int maxinfo_backend_dcbs()
{
    return dcb_count_by_usage(DCB_USAGE_BACKEND);
}

/**
 * Interface to dcb_count_by_usage for internal dcbs
 */
static int maxinfo_internal_dcbs()
{
    return dcb_count_by_usage(DCB_USAGE_INTERNAL);
}

/**
 * Interface to poll stats for reads
 */
static int64_t maxinfo_read_events()
{
    return poll_get_stat(POLL_STAT_READ);
}

/**
 * Interface to poll stats for writes
 */
static int64_t maxinfo_write_events()
{
    return poll_get_stat(POLL_STAT_WRITE);
}

/**
 * Interface to poll stats for errors
 */
static int64_t maxinfo_error_events()
{
    return poll_get_stat(POLL_STAT_ERROR);
}

/**
 * Interface to poll stats for hangup
 */
static int64_t maxinfo_hangup_events()
{
    return poll_get_stat(POLL_STAT_HANGUP);
}

/**
 * Interface to poll stats for accepts
 */
static int64_t maxinfo_accept_events()
{
    return poll_get_stat(POLL_STAT_ACCEPT);
}

/**
 * Interface to poll stats for event queue length
 */
static int64_t maxinfo_avg_event_queue_length()
{
    return poll_get_stat(POLL_STAT_EVQ_AVG);
}

/**
 * Interface to poll stats for max event queue length
 */
static int64_t maxinfo_max_event_queue_length()
{
    return poll_get_stat(POLL_STAT_EVQ_MAX);
}

/**
 * Interface to poll stats for max queue time
 */
static int64_t maxinfo_max_event_queue_time()
{
    return poll_get_stat(POLL_STAT_MAX_QTIME);
}

/**
 * Interface to poll stats for max event execution time
 */
static int64_t maxinfo_max_event_exec_time()
{
    return poll_get_stat(POLL_STAT_MAX_EXECTIME);
}

/**
 * Variables that may be sent in a show status
 */
static struct
{
    const char* name;
    int         type;
    STATSFUNC   func;
} status[] =
{
    {"Uptime",                    VT_INT, (STATSFUNC)maxscale_uptime               },
    {"Uptime_since_flush_status", VT_INT, (STATSFUNC)maxscale_uptime               },
    {"Threads_created",           VT_INT, (STATSFUNC)config_threadcount            },
    {"Threads_running",           VT_INT, (STATSFUNC)config_threadcount            },
    {"Threadpool_threads",        VT_INT, (STATSFUNC)config_threadcount            },
    {"Threads_connected",         VT_INT, (STATSFUNC)serviceSessionCountAll        },
    {"Connections",               VT_INT, (STATSFUNC)maxinfo_all_dcbs              },
    {"Client_connections",        VT_INT, (STATSFUNC)maxinfo_client_dcbs           },
    {"Backend_connections",       VT_INT, (STATSFUNC)maxinfo_backend_dcbs          },
    {"Listeners",                 VT_INT, (STATSFUNC)maxinfo_listener_dcbs         },
    {"Internal_descriptors",      VT_INT, (STATSFUNC)maxinfo_internal_dcbs         },
    {"Read_events",               VT_INT, (STATSFUNC)maxinfo_read_events           },
    {"Write_events",              VT_INT, (STATSFUNC)maxinfo_write_events          },
    {"Hangup_events",             VT_INT, (STATSFUNC)maxinfo_hangup_events         },
    {"Error_events",              VT_INT, (STATSFUNC)maxinfo_error_events          },
    {"Accept_events",             VT_INT, (STATSFUNC)maxinfo_accept_events         },
    {"Avg_event_queue_length",    VT_INT, (STATSFUNC)maxinfo_avg_event_queue_length},
    {"Max_event_queue_length",    VT_INT, (STATSFUNC)maxinfo_max_event_queue_length},
    {"Max_event_queue_time",      VT_INT, (STATSFUNC)maxinfo_max_event_queue_time  },
    {"Max_event_execution_time",  VT_INT, (STATSFUNC)maxinfo_max_event_exec_time   },
    {NULL,                        0,      NULL                                     }
};

/**
 * Callback function to populate rows of the show variable
 * command
 *
 * @param data  The context point
 * @return  The next row or NULL if end of rows
 */
static void status_row(std::unique_ptr<ResultSet>& set, const char* like)
{
    for (int i = 0; status[i].name; i++)
    {
        if (!like || !maxinfo_pattern_match(like, status[i].name))
        {
            set->add_row({status[i].name, value_to_string(status[i].type, status[i].func)});
        }
    }
}

/**
 * Execute a show status command applying an optional filter
 *
 * @param dcb       The DCB connected to the client
 * @param filter    A potential like clause or NULL
 */
static void exec_show_status(DCB* dcb, MAXINFO_TREE* filter)
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Variable_name", "Value"});
    status_row(set, filter ? filter->value : NULL);
    set->write(dcb);
}

/**
 * Return the show status data as a result set
 *
 * @return The show status data as a result set
 */
std::unique_ptr<ResultSet> maxinfo_status()
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Variable_name", "Value"});
    status_row(set, NULL);
    return set;
}

/**
 * Execute a select command parse tree and return the result set
 * or runtime error
 *
 * @param dcb   The DCB that connects to the client
 * @param tree  The parse tree for the query
 */
static void exec_select(DCB* dcb, MAXINFO_TREE* tree)
{
    maxinfo_send_error(dcb, 0, "Select not yet implemented");
}

/**
 * Perform a "like" pattern match. Only works for leading and trailing %
 *
 * @param pattern   Pattern to match
 * @param str       String to match against pattern
 * @return      Zero on match
 */
static int maxinfo_pattern_match(const char* pattern, const char* str)
{
    int anchor = 0, len, trailing;
    const char* fixed;

    if (*pattern != '%')
    {
        fixed = pattern;
        anchor = 1;
    }
    else
    {
        fixed = &pattern[1];
    }
    len = strlen(fixed);
    if (fixed[len - 1] == '%')
    {
        trailing = 1;
    }
    else
    {
        trailing = 0;
    }
    if (anchor == 1 && trailing == 0)   // No wildcard
    {
        return strcasecmp(pattern, str);
    }
    else if (anchor == 1)
    {
        return strncasecmp(str, pattern, len - trailing);
    }
    else
    {
        char* portion = static_cast<char*>(MXS_MALLOC(len + 1));
        MXS_ABORT_IF_NULL(portion);
        int rval;
        strncpy(portion, fixed, len - trailing);
        portion[len - trailing] = 0;
        rval = (strcasestr(str, portion) != NULL ? 0 : 1);
        MXS_FREE(portion);
        return rval;
    }
}

/**
 * Send an OK packet to the client.
 * @param dcb The DCB that connects to the client
 */
void maxinfo_send_ok(DCB* dcb)
{
    static const char ok_packet[] =
    {
        0x07, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00
    };

    GWBUF* buffer = gwbuf_alloc(sizeof(ok_packet));

    if (buffer)
    {
        memcpy(buffer->start, ok_packet, sizeof(ok_packet));
        dcb->func.write(dcb, buffer);
    }
}
