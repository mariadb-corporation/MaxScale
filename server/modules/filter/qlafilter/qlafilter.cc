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
 * @file qlafilter.c - Quary Log All Filter
 *
 * QLA Filter - Query Log All. A simple query logging filter. All queries passing
 * through the filter are written to a text file.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 */

#define MXS_MODULE_NAME "qlafilter"

#include <maxscale/cppdefs.hh>

#include <cmath>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <sys/time.h>
#include <fstream>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/filter.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/pcre2.h>
#include <maxscale/service.h>
#include <maxscale/utils.h>
#include <maxscale/modulecmd.h>
#include <maxscale/json_api.h>

/* Date string buffer size */
#define QLA_DATE_BUFFER_SIZE 20

/* Log file save mode flags */
#define CONFIG_FILE_SESSION (1 << 0) // Default value, session specific files
#define CONFIG_FILE_UNIFIED (1 << 1) // One file shared by all sessions

/* Flags for controlling extra log entry contents */
enum log_options
{
    LOG_DATA_SERVICE    = (1 << 0),
    LOG_DATA_SESSION    = (1 << 1),
    LOG_DATA_DATE       = (1 << 2),
    LOG_DATA_USER       = (1 << 3),
    LOG_DATA_QUERY      = (1 << 4),
    LOG_DATA_REPLY_TIME = (1 << 5),
};

/* Default values for logged data */
#define LOG_DATA_DEFAULT "date,user,query"

/* The filter entry points */
static MXS_FILTER *createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *);
static MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session);
static void closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_DOWNSTREAM *downstream);
static void setUpstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_UPSTREAM *upstream);
static int routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static int clientReply(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue);
static void diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
static json_t* diagnostic_json(const MXS_FILTER *instance, const MXS_FILTER_SESSION *fsession);
static uint64_t getCapabilities(MXS_FILTER* instance);

/**
 * A instance structure, the assumption is that the option passed
 * to the filter is simply a base for the filename to which the queries
 * are logged.
 *
 * To this base a session number is attached such that each session will
 * have a unique name.
 */
typedef struct
{
    int sessions; /* The count of sessions */
    char *name;     /* Filter definition name */
    char *filebase; /* The filename base */
    char *source; /* The source of the client connection to filter on */
    char *user_name; /* The user name to filter on */
    char *match; /* Optional text to match against */
    pcre2_code* re_match; /* Compiled regex text */
    char *exclude; /* Optional text to match against for exclusion */
    pcre2_code* re_exclude; /* Compiled regex nomatch text */
    uint32_t ovec_size; /* PCRE2 match data ovector size */
    uint32_t log_mode_flags; /* Log file mode settings */
    uint32_t log_file_data_flags; /* What data is saved to the files */
    FILE *unified_fp; /* Unified log file. The pointer needs to be shared here
                       * to avoid garbled printing. */
    char *unified_filename; /* Filename of the unified log file */
    bool flush_writes; /* Flush log file after every write? */
    bool append;    /* Open files in append-mode? */
    char *query_newline; /* Character(s) used to replace a newline within a query */
    char *separator; /*  Character(s) used to separate elements */
    bool write_warning_given; /* Avoid repeatedly printing some errors/warnings. */
} QLA_INSTANCE;

/**
 * Helper struct for holding data before it's written to file.
 */
struct LOG_EVENT_DATA
{
    bool has_message;   // Does message data exist?
    GWBUF* query_clone; // Clone of the query buffer.
    char query_date[QLA_DATE_BUFFER_SIZE];  // Text representation of date.
    timespec begin_time; // Timer value at the moment of receiving query.
};

namespace
{
/**
 * Resets event data. Since QLA_SESSION is allocated with calloc, separate initialisation is not needed.
 *
 * @param event Event to reset
 */
void clear(LOG_EVENT_DATA& event)
{
    event.has_message = false;
    gwbuf_free(event.query_clone);
    event.query_clone = NULL;
    event.query_date[0] = '\0';
    event.begin_time = {0, 0};
}
}

/* The session structure for this QLA filter. */
typedef struct
{
    int active;
    MXS_UPSTREAM up;
    MXS_DOWNSTREAM down;
    char *filename;     /* The session-specific log file name */
    FILE *fp;           /* The session-specific log file */
    const char *remote; /* Client address */
    char *service;      /* The service name this filter is attached to. Not owned. */
    size_t ses_id;      /* The session this filter serves */
    const char *user;   /* The client */
    pcre2_match_data* match_data; /* Regex match data */
    LOG_EVENT_DATA event_data; /* Information about the latest event, required if logging execution time. */
} QLA_SESSION;

static FILE* open_log_file(QLA_INSTANCE *, uint32_t, const char *);
static int write_log_entry(FILE*, QLA_INSTANCE*, QLA_SESSION*, uint32_t,
                           const char*, const char*, size_t, int);
static bool cb_log(const MODULECMD_ARG *argv, json_t** output);

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0},
    {"extended",   PCRE2_EXTENDED},
    {NULL}
};

static const MXS_ENUM_VALUE log_type_values[] =
{
    {"session", CONFIG_FILE_SESSION},
    {"unified", CONFIG_FILE_UNIFIED},
    {NULL}
};

static const MXS_ENUM_VALUE log_data_values[] =
{
    {"service",     LOG_DATA_SERVICE},
    {"session",     LOG_DATA_SESSION},
    {"date",        LOG_DATA_DATE},
    {"user",        LOG_DATA_USER},
    {"query",       LOG_DATA_QUERY},
    {"reply_time",  LOG_DATA_REPLY_TIME},
    {NULL}
};

static const char PARAM_MATCH[] = "match";
static const char PARAM_EXCLUDE[] = "exclude";
static const char PARAM_USER[] = "user";
static const char PARAM_SOURCE[] = "source";
static const char PARAM_FILEBASE[] = "filebase";
static const char PARAM_OPTIONS[] = "options";
static const char PARAM_LOG_TYPE[] = "log_type";
static const char PARAM_LOG_DATA[] = "log_data";
static const char PARAM_FLUSH[] = "flush";
static const char PARAM_APPEND[] = "append";
static const char PARAM_NEWLINE[] = "newline_replacement";
static const char PARAM_SEPARATOR[] = "separator";

MXS_BEGIN_DECLS

/**
 * The module entry point routine.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    modulecmd_arg_type_t args[] =
    {
        {
            MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            "Filter to read logs from"
        },
        {
            MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL,
            "Start reading from this line"
        },
        {
            MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL,
            "Stop reading at this line (exclusive)"
        }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "log",
                               MODULECMD_TYPE_PASSIVE, cb_log, 3, args,
                               "Show unified log file as a JSON array");

    static MXS_FILTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        setDownstream,
        setUpstream,
        routeQuery,
        clientReply,
        diagnostic,
        diagnostic_json,
        getCapabilities,
        NULL, // No destroyInstance
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "A simple query logging filter",
        "V1.1.1",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {
                PARAM_MATCH,
                MXS_MODULE_PARAM_REGEX
            },
            {
                PARAM_EXCLUDE,
                MXS_MODULE_PARAM_REGEX
            },
            {
                PARAM_USER,
                MXS_MODULE_PARAM_STRING
            },
            {
                PARAM_SOURCE,
                MXS_MODULE_PARAM_STRING
            },
            {
                PARAM_FILEBASE,
                MXS_MODULE_PARAM_STRING,
                NULL,
                MXS_MODULE_OPT_REQUIRED
            },
            {
                PARAM_OPTIONS,
                MXS_MODULE_PARAM_ENUM,
                "ignorecase",
                MXS_MODULE_OPT_NONE,
                option_values
            },
            {
                PARAM_LOG_TYPE,
                MXS_MODULE_PARAM_ENUM,
                "session",
                MXS_MODULE_OPT_NONE,
                log_type_values
            },
            {
                PARAM_LOG_DATA,
                MXS_MODULE_PARAM_ENUM,
                LOG_DATA_DEFAULT,
                MXS_MODULE_OPT_NONE,
                log_data_values
            },
            {
                PARAM_NEWLINE,
                MXS_MODULE_PARAM_QUOTEDSTRING,
                "\" \"",
                MXS_MODULE_OPT_NONE
            },
            {
                PARAM_SEPARATOR,
                MXS_MODULE_PARAM_QUOTEDSTRING,
                ",",
                MXS_MODULE_OPT_NONE
            },
            {
                PARAM_FLUSH,
                MXS_MODULE_PARAM_BOOL,
                "false"
            },
            {
                PARAM_APPEND,
                MXS_MODULE_PARAM_BOOL,
                "false"
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

MXS_END_DECLS

/**
 * Create an instance of the filter for a particular service within MaxScale.
 *
 * @param name      The name of the instance (as defined in the config file)
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The new filter instance, or NULL on error
 */
static MXS_FILTER *
createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    QLA_INSTANCE *my_instance = (QLA_INSTANCE*) MXS_MALLOC(sizeof(QLA_INSTANCE));

    if (my_instance)
    {
        my_instance->name = MXS_STRDUP_A(name);
        my_instance->sessions = 0;
        my_instance->ovec_size = 0;
        my_instance->unified_fp = NULL;
        my_instance->unified_filename = NULL;
        my_instance->write_warning_given = false;

        my_instance->source = config_copy_string(params, PARAM_SOURCE);
        my_instance->user_name = config_copy_string(params, PARAM_USER);

        my_instance->filebase = MXS_STRDUP_A(config_get_string(params, PARAM_FILEBASE));
        my_instance->append = config_get_bool(params, PARAM_APPEND);
        my_instance->flush_writes = config_get_bool(params, PARAM_FLUSH);
        my_instance->log_file_data_flags = config_get_enum(params, PARAM_LOG_DATA, log_data_values);
        my_instance->log_mode_flags = config_get_enum(params, PARAM_LOG_TYPE, log_type_values);
        my_instance->query_newline = MXS_STRDUP_A(config_get_string(params, PARAM_NEWLINE));
        my_instance->separator = MXS_STRDUP_A(config_get_string(params, PARAM_SEPARATOR));

        my_instance->match = config_copy_string(params, PARAM_MATCH);
        my_instance->exclude = config_copy_string(params, PARAM_EXCLUDE);
        my_instance->re_exclude = NULL;
        my_instance->re_match = NULL;
        bool error = false;

        int cflags = config_get_enum(params, PARAM_OPTIONS, option_values);

        const char* keys[] = {PARAM_MATCH, PARAM_EXCLUDE};
        pcre2_code** code_arr[] = {&my_instance->re_match, &my_instance->re_exclude};
        if (!config_get_compiled_regexes(params, keys, sizeof(keys) / sizeof(char*),
                                         cflags, &my_instance->ovec_size, code_arr))
        {
            error = true;
        }

        // Try to open the unified log file
        if (!error && (my_instance->log_mode_flags & CONFIG_FILE_UNIFIED))
        {
            // First calculate filename length
            const char UNIFIED[] = ".unified";
            int namelen = strlen(my_instance->filebase) + sizeof(UNIFIED);
            char *filename = NULL;
            if ((filename = (char*)MXS_CALLOC(namelen, sizeof(char))) != NULL)
            {
                snprintf(filename, namelen, "%s.unified", my_instance->filebase);
                // Open the file. It is only closed at program exit
                my_instance->unified_fp = open_log_file(my_instance, my_instance->log_file_data_flags, filename);

                if (my_instance->unified_fp == NULL)
                {
                    MXS_FREE(filename);
                    MXS_ERROR("Opening output file for qla-filter failed due to %d, %s",
                              errno, mxs_strerror(errno));
                    error = true;
                }
                else
                {
                    my_instance->unified_filename = filename;
                }
            }
            else
            {
                error = true;
            }
        }

        if (error)
        {
            MXS_FREE(my_instance->name);
            MXS_FREE(my_instance->match);
            pcre2_code_free(my_instance->re_match);
            MXS_FREE(my_instance->exclude);
            pcre2_code_free(my_instance->re_exclude);
            if (my_instance->unified_fp != NULL)
            {
                fclose(my_instance->unified_fp);
            }
            MXS_FREE(my_instance->filebase);
            MXS_FREE(my_instance->source);
            MXS_FREE(my_instance->user_name);
            MXS_FREE(my_instance->query_newline);
            MXS_FREE(my_instance->separator);
            MXS_FREE(my_instance);
            my_instance = NULL;
        }
    }
    return (MXS_FILTER *) my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * Create the file to log to and open it.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION *
newSession(MXS_FILTER *instance, MXS_SESSION *session)
{
    QLA_INSTANCE *my_instance = (QLA_INSTANCE *) instance;
    QLA_SESSION *my_session;
    const char *remote, *userName;

    if ((my_session = (QLA_SESSION*)MXS_CALLOC(1, sizeof(QLA_SESSION))) != NULL)
    {
        my_session->fp = NULL;
        my_session->match_data = NULL;
        my_session->filename = (char *)MXS_MALLOC(strlen(my_instance->filebase) + 20);
        const uint32_t ovec_size = my_instance->ovec_size;
        if (ovec_size)
        {
            my_session->match_data = pcre2_match_data_create(ovec_size, NULL);
        }

        if (!my_session->filename || (ovec_size && !my_session->match_data))
        {
            MXS_FREE(my_session->filename);
            pcre2_match_data_free(my_session->match_data);
            MXS_FREE(my_session);
            return NULL;
        }
        my_session->active = 1;

        remote = session_get_remote(session);
        userName = session_get_user(session);
        ss_dassert(userName && remote);

        if ((my_instance->source && remote &&
             strcmp(remote, my_instance->source)) ||
            (my_instance->user_name && userName &&
             strcmp(userName, my_instance->user_name)))
        {
            my_session->active = 0;
        }

        my_session->user = userName;
        my_session->remote = remote;
        my_session->ses_id = session->ses_id;
        my_session->service = session->service->name;

        sprintf(my_session->filename, "%s.%lu",
                my_instance->filebase,
                my_session->ses_id);

        // Multiple sessions can try to update my_instance->sessions simultaneously
        atomic_add(&(my_instance->sessions), 1);

        // Only open the session file if the corresponding mode setting is used
        if (my_session->active && (my_instance->log_mode_flags & CONFIG_FILE_SESSION))
        {
            uint32_t data_flags = (my_instance->log_file_data_flags &
                                   ~LOG_DATA_SESSION); // No point printing "Session"
            my_session->fp = open_log_file(my_instance, data_flags, my_session->filename);

            if (my_session->fp == NULL)
            {
                MXS_ERROR("Opening output file for qla-filter failed due to %d, %s",
                          errno, mxs_strerror(errno));
                MXS_FREE(my_session->filename);
                pcre2_match_data_free(my_session->match_data);
                MXS_FREE(my_session);
                my_session = NULL;
            }
        }
    }
    return (MXS_FILTER_SESSION*)my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the QLA filter we simple close the file descriptor.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
    QLA_SESSION *my_session = (QLA_SESSION *) session;

    if (my_session->active && my_session->fp)
    {
        fclose(my_session->fp);
    }
    clear(my_session->event_data);
}

/**
 * Free the memory associated with the session
 *
 * @param instance  The filter instance
 * @param session   The filter session
 */
static void
freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
    QLA_SESSION *my_session = (QLA_SESSION *) session;

    MXS_FREE(my_session->filename);
    pcre2_match_data_free(my_session->match_data);
    MXS_FREE(session);
    return;
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param downstream    The downstream filter or router.
 */
static void
setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_DOWNSTREAM *downstream)
{
    QLA_SESSION *my_session = (QLA_SESSION *) session;

    my_session->down = *downstream;
}

/**
 * Set the upstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param upstream  The upstream filter or router.
 */
static void
setUpstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_UPSTREAM *upstream)
{
    QLA_SESSION *my_session = (QLA_SESSION *) session;
    my_session->up = *upstream;
}

/**
 * Write QLA log entry/entries to disk
 *
 * @param my_instance Filter instance
 * @param my_session Filter session
 * @param date_string Date string
 * @param query Query string, not 0-terminated
 * @param querylen Query string length
 * @param elapsed_ms Query execution time, in milliseconds
 */
void write_log_entries(QLA_INSTANCE* my_instance, QLA_SESSION* my_session,
                       const char* date_string, const char* query, int querylen, int elapsed_ms)
{
    bool write_error = false;
    if (my_instance->log_mode_flags & CONFIG_FILE_SESSION)
    {
        // In this case there is no need to write the session
        // number into the files.
        uint32_t data_flags = (my_instance->log_file_data_flags & ~LOG_DATA_SESSION);
        if (write_log_entry(my_session->fp, my_instance, my_session, data_flags,
                            date_string, query, querylen, elapsed_ms) < 0)
        {
            write_error = true;
        }
    }
    if (my_instance->log_mode_flags & CONFIG_FILE_UNIFIED)
    {
        uint32_t data_flags = my_instance->log_file_data_flags;
        if (write_log_entry(my_instance->unified_fp, my_instance, my_session, data_flags,
                            date_string, query, querylen, elapsed_ms) < 0)
        {
            write_error = true;
        }
    }
    if (write_error && !my_instance->write_warning_given)
    {
        MXS_ERROR("qla-filter '%s': Log file write failed. "
                  "Suppressing further similar warnings.",
                  my_instance->name);
        my_instance->write_warning_given = true;
    }
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int
routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
{
    QLA_INSTANCE *my_instance = (QLA_INSTANCE *) instance;
    QLA_SESSION *my_session = (QLA_SESSION *) session;
    char *query = NULL;
    int query_len = 0;

    if (my_session->active &&
        modutil_extract_SQL(queue, &query, &query_len) &&
        mxs_pcre2_check_match_exclude(my_instance->re_match, my_instance->re_exclude,
                                      my_session->match_data, query, query_len, MXS_MODULE_NAME))
    {
        const uint32_t data_flags = my_instance->log_file_data_flags;
        LOG_EVENT_DATA& event = my_session->event_data;
        if (data_flags & LOG_DATA_DATE)
        {
            // Print current date to a buffer. Use the buffer in the event data struct even if execution time
            // is not needed.
            const time_t utc_seconds = time(NULL);
            tm local_time;
            localtime_r(&utc_seconds, &local_time);
            strftime(event.query_date, QLA_DATE_BUFFER_SIZE, "%F %T", &local_time);
        }

        if (data_flags & LOG_DATA_REPLY_TIME)
        {
            // Have to measure reply time from server. Save query data for printing during clientReply.
            // If old event data exists, it is erased. This only happens if client sends a query before
            // receiving reply to previous query.
            if (event.has_message)
            {
                clear(event);
            }
            clock_gettime(CLOCK_MONOTONIC, &event.begin_time);
            if (data_flags & LOG_DATA_QUERY)
            {
                event.query_clone = gwbuf_clone(queue);
            }
            event.has_message = true;
        }
        else
        {
            // If execution times are not logged, write the log entry now.
            write_log_entries(my_instance, my_session, event.query_date, query, query_len, -1);
        }
    }
    /* Pass the query downstream */
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session, queue);
}

/**
 * The clientReply entry point. Required for measuring and printing query execution time.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int
clientReply(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
{
    QLA_INSTANCE *my_instance = (QLA_INSTANCE *) instance;
    QLA_SESSION *my_session = (QLA_SESSION *) session;
    LOG_EVENT_DATA& event = my_session->event_data;
    if (event.has_message)
    {
        const uint32_t data_flags = my_instance->log_file_data_flags;
        ss_dassert(data_flags & LOG_DATA_REPLY_TIME);
        char* query = NULL;
        int query_len = 0;
        if (data_flags & LOG_DATA_QUERY)
        {
            modutil_extract_SQL(event.query_clone, &query, &query_len);
        }
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now); // Gives time in seconds + nanoseconds
        // Calculate elapsed time in milliseconds.
        double elapsed_ms = 1E3 * (now.tv_sec - event.begin_time.tv_sec) +
                            (now.tv_nsec - event.begin_time.tv_nsec) / (double)1E6;
        write_log_entries(my_instance, my_session, event.query_date, query, query_len,
                          std::floor(elapsed_ms + 0.5));
        clear(event);
    }
    return my_session->up.clientReply(my_session->up.instance, my_session->up.session, queue);
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 * @param   dcb         The DCB for diagnostic output
 */
static void
diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb)
{
    QLA_INSTANCE *my_instance = (QLA_INSTANCE *) instance;
    QLA_SESSION *my_session = (QLA_SESSION *) fsession;

    if (my_session)
    {
        dcb_printf(dcb, "\t\tLogging to file            %s.\n",
                   my_session->filename);
    }
    if (my_instance->source)
    {
        dcb_printf(dcb, "\t\tLimit logging to connections from  %s\n",
                   my_instance->source);
    }
    if (my_instance->user_name)
    {
        dcb_printf(dcb, "\t\tLimit logging to user      %s\n",
                   my_instance->user_name);
    }
    if (my_instance->match)
    {
        dcb_printf(dcb, "\t\tInclude queries that match     %s\n",
                   my_instance->match);
    }
    if (my_instance->exclude)
    {
        dcb_printf(dcb, "\t\tExclude queries that match     %s\n",
                   my_instance->exclude);
    }
    dcb_printf(dcb, "\t\tColumn separator     %s\n",
               my_instance->separator);
    dcb_printf(dcb, "\t\tNewline replacement     %s\n",
               my_instance->query_newline);
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 */
static json_t* diagnostic_json(const MXS_FILTER *instance, const MXS_FILTER_SESSION *fsession)
{
    QLA_INSTANCE *my_instance = (QLA_INSTANCE*)instance;
    QLA_SESSION *my_session = (QLA_SESSION*)fsession;

    json_t* rval = json_object();

    if (my_session)
    {
        json_object_set_new(rval, "session_filename", json_string(my_session->filename));
    }

    if (my_instance->source)
    {
        json_object_set_new(rval, PARAM_SOURCE, json_string(my_instance->source));
    }

    if (my_instance->user_name)
    {
        json_object_set_new(rval, PARAM_USER, json_string(my_instance->user_name));
    }

    if (my_instance->match)
    {
        json_object_set_new(rval, PARAM_MATCH, json_string(my_instance->match));
    }

    if (my_instance->exclude)
    {
        json_object_set_new(rval, PARAM_EXCLUDE, json_string(my_instance->exclude));
    }
    json_object_set_new(rval, PARAM_SEPARATOR, json_string(my_instance->separator));
    json_object_set_new(rval, PARAM_NEWLINE, json_string(my_instance->query_newline));

    return rval;
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_NONE;
}

/**
 * Open the log file and print a header if appropriate.
 *
 * @param   instance    The filter instance
 * @param   data_flags  Data save settings flags
 * @param   filename    Target file path
 * @return  A valid file on success, null otherwise.
 */
static FILE* open_log_file(QLA_INSTANCE *instance, uint32_t data_flags, const char *filename)
{
    bool file_existed = false;
    FILE *fp = NULL;
    if (instance->append == false)
    {
        // Just open the file (possibly overwriting) and then print header.
        fp = fopen(filename, "w");
    }
    else
    {
        /**
         *  Using fopen() with 'a+' means we will always write to the end but can read
         *  anywhere. Depending on the "append"-setting the file has been
         *  opened in different modes, which should be considered if file handling
         *  changes later (e.g. rewinding).
         */
        if ((fp = fopen(filename, "a+")) != NULL)
        {
            // Check to see if file already has contents
            fseek(fp, 0, SEEK_END);
            if (ftell(fp) > 0) // Any errors in ftell cause overwriting
            {
                file_existed = true;
            }
        }
    }

    if (fp && !file_existed && data_flags != 0)
    {
        // Print a header.
        const char SERVICE[] = "Service";
        const char SESSION[] = "Session";
        const char DATE[] = "Date";
        const char USERHOST[] = "User@Host";
        const char QUERY[] = "Query";
        const char REPLY_TIME[] = "Reply_time";

        std::stringstream header;
        const char* curr_sep = ""; // Use empty string as the first separator
        const char* real_sep = instance->separator;

        if (data_flags & LOG_DATA_SERVICE)
        {
            header << SERVICE;
            curr_sep = real_sep;
        }
        if (data_flags & LOG_DATA_SESSION)
        {
            header << curr_sep << SESSION;
            curr_sep = real_sep;
        }
        if (data_flags & LOG_DATA_DATE)
        {
            header << curr_sep << DATE;
            curr_sep = real_sep;
        }
        if (data_flags & LOG_DATA_USER)
        {
            header << curr_sep << USERHOST;
            curr_sep = real_sep;
        }
        if (data_flags & LOG_DATA_REPLY_TIME)
        {
            header << curr_sep << REPLY_TIME;
            curr_sep = real_sep;
        }
        if (data_flags & LOG_DATA_QUERY)
        {
            header << curr_sep << QUERY;
        }
        header << '\n';

        // Finally, write the log header.
        int written = fprintf(fp, "%s", header.str().c_str());

        if ((written <= 0) || ((instance->flush_writes) && (fflush(fp) < 0)))
        {
            // Weird error, file opened but a write failed. Best to stop.
            fclose(fp);
            MXS_ERROR("Failed to print header to file %s.", filename);
            return NULL;
        }
    }
    return fp;
}

static void print_string_replace_newlines(const char *sql_string,
                                          size_t sql_str_len, const char* rep_newline,
                                          std::stringstream* output)
{
    ss_dassert(output);
    size_t line_begin = 0;
    size_t search_pos = 0;
    while (search_pos < sql_str_len)
    {
        int line_end_chars = 0;
        // A newline is either \r\n, \n or \r
        if (sql_string[search_pos] == '\r')
        {
            if (search_pos + 1 < sql_str_len && sql_string[search_pos + 1] == '\n')
            {
                // Got \r\n
                line_end_chars = 2;
            }
            else
            {
                // Just \r
                line_end_chars = 1;
            }
        }
        else if (sql_string[search_pos] == '\n')
        {
            // Just \n
            line_end_chars = 1;
        }

        if (line_end_chars > 0)
        {
            // Found line ending characters, write out the line excluding line end.
            output->write(&sql_string[line_begin], search_pos - line_begin);
            *output << rep_newline;
            // Next line begins after line end chars
            line_begin = search_pos + line_end_chars;
            // For \r\n, advance search_pos
            search_pos += line_end_chars - 1;
        }

        search_pos++;
    }

    // Print anything left
    if (line_begin < sql_str_len)
    {
        output->write(&sql_string[line_begin], sql_str_len - line_begin);
    }
}

/**
 * Write an entry to the log file.
 *
 * @param   logfile       Target file
 * @param   instance      Filter instance
 * @param   session       Filter session
 * @param   data_flags    Controls what to write
 * @param   time_string   Date entry
 * @param   sql_string    SQL-query, *not* NULL terminated
 * @param   sql_str_len   Length of SQL-string
 * @param   elapsed_ms    Query execution time, in milliseconds
 * @return  The number of characters written, or a negative value on failure
 */
static int write_log_entry(FILE *logfile, QLA_INSTANCE *instance, QLA_SESSION *session, uint32_t data_flags,
                           const char *time_string, const char *sql_string, size_t sql_str_len,
                           int elapsed_ms)
{
    ss_dassert(logfile != NULL);
    if (data_flags == 0)
    {
        // Nothing to print
        return 0;
    }

    /* Printing to the file in parts would likely cause garbled printing if several threads write
     * simultaneously, so we have to first print to a string. */
    std::stringstream output;
    const char* curr_sep = ""; // Use empty string as the first separator
    const char* real_sep = instance->separator;

    if (data_flags & LOG_DATA_SERVICE)
    {
        output << session->service;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_SESSION)
    {
        output << curr_sep << session->ses_id;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_DATE)
    {
        output << curr_sep << time_string;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_USER)
    {
        output << curr_sep << session->user << "@" << session->remote;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_REPLY_TIME)
    {
        output << curr_sep << elapsed_ms;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_QUERY)
    {
        output << curr_sep;
        if (*instance->query_newline)
        {
            print_string_replace_newlines(sql_string, sql_str_len, instance->query_newline, &output);
        }
        else
        {
            // The newline replacement is an empty string so print the query as is
            output.write(sql_string, sql_str_len); // non-null-terminated string
        }
    }
    output << "\n";

    // Finally, write the log event.
    int written = fprintf(logfile, "%s", output.str().c_str());

    if ((!instance->flush_writes) || (written <= 0))
    {
        return written;
    }
    else
    {
        // Try flushing. If successful, still return the characters written.
        int rval = fflush(logfile);
        if (rval >= 0)
        {
            return written;
        }
        return rval;
    }
}

static bool cb_log(const MODULECMD_ARG *argv, json_t** output)
{
    ss_dassert(argv->argc > 0);
    ss_dassert(argv->argv[0].type.type == MODULECMD_ARG_FILTER);

    MXS_FILTER_DEF* filter = argv[0].argv->value.filter;
    QLA_INSTANCE* instance = reinterpret_cast<QLA_INSTANCE*>(filter_def_get_instance(filter));
    bool rval = false;

    if (instance->log_mode_flags & CONFIG_FILE_UNIFIED)
    {
        ss_dassert(instance->unified_fp && instance->unified_filename);
        std::ifstream file(instance->unified_filename);

        if (file)
        {
            json_t* arr = json_array();
            // TODO: Add integer type to modulecmd
            int start = argv->argc > 1 ? atoi(argv->argv[1].value.string) : 0;
            int end = argv->argc > 2 ? atoi(argv->argv[2].value.string) : 0;
            int current = 0;

            /** Skip lines we don't want */
            for (std::string line; current < start && std::getline(file, line); current++)
            {
                ;
            }

            /** Read lines until either EOF or line count is reached */
            for (std::string line; std::getline(file, line) && (current < end || end == 0); current++)
            {
                json_array_append_new(arr, json_string(line.c_str()));
            }

            *output = arr;
            rval = true;
        }
        else
        {
            *output = mxs_json_error("Failed to open file '%s'",
                                     instance->unified_filename);
        }
    }
    else
    {
        *output = mxs_json_error("Filter '%s' does not have unified log file enabled",
                                 filter_def_get_name(filter));
    }

    return rval;
}
