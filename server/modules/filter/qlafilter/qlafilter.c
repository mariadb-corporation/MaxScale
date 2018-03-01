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
 * @file qlafilter.c - Quary Log All Filter
 * @verbatim
 *
 * QLA Filter - Query Log All. A primitive query logging filter, simply
 * used to verify the filter mechanism for downstream filters. All queries
 * that are passed through the filter will be written to file.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 *
 * A single option may be passed to the filter, this is the name of the
 * file to which the queries are logged. A serial number is appended to this
 * name in order that each session logs to a different file.
 *
 * Date         Who             Description
 * 03/06/2014   Mark Riddoch    Initial implementation
 * 11/06/2014   Mark Riddoch    Addition of source and match parameters
 * 19/06/2014   Mark Riddoch    Addition of user parameter
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "qlafilter"

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <maxscale/filter.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/utils.h>
#include <maxscale/log_manager.h>
#include <time.h>
#include <sys/time.h>
#include <regex.h>
#include <string.h>
#include <maxscale/atomic.h>
#include <maxscale/alloc.h>
#include <maxscale/service.h>

/** Date string buffer size */
#define QLA_DATE_BUFFER_SIZE 20

/** Log file save mode flags */
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
};

/** Default values for logged data */
#define LOG_DATA_DEFAULT "date,user,query"

/*
 * The filter entry points
 */
static MXS_FILTER *createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *);
static MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session);
static void closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_DOWNSTREAM *downstream);
static int routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static void diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
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
    regex_t re; /* Compiled regex text */
    char *nomatch; /* Optional text to match against for exclusion */
    regex_t nore; /* Compiled regex nomatch text */
    uint32_t log_mode_flags; /* Log file mode settings */
    uint32_t log_file_data_flags; /* What data is saved to the files */
    FILE *unified_fp; /* Unified log file. The pointer needs to be shared here
                       * to avoid garbled printing. */
    bool flush_writes; /* Flush log file after every write? */
    bool append;    /* Open files in append-mode? */
    bool write_warning_given; /* To make sure some warning are only given once */
} QLA_INSTANCE;

/**
 * The session structure for this QLA filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
typedef struct
{
    int active;
    MXS_DOWNSTREAM down;
    char *filename;   /* The session-specific log file name */
    FILE *fp;         /* The session-specific log file */
    const char *remote;
    char *service;    /* The service name this filter is attached to. Not owned. */
    size_t ses_id;    /* The session this filter serves */
    const char *user; /* The client */
} QLA_SESSION;

static FILE* open_log_file(uint32_t, QLA_INSTANCE *, const char *);
static int write_log_entry(uint32_t, FILE*, QLA_INSTANCE*, QLA_SESSION*, const char*,
                           const char*, size_t);

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", REG_ICASE},
    {"case",       0},
    {"extended",   REG_EXTENDED},
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
    {"service", LOG_DATA_SERVICE},
    {"session", LOG_DATA_SESSION},
    {"date",    LOG_DATA_DATE},
    {"user",    LOG_DATA_USER},
    {"query",   LOG_DATA_QUERY},
    {NULL}
};

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_FILTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        setDownstream,
        NULL, // No Upstream requirement
        routeQuery,
        NULL, // No client reply
        diagnostic,
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
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {
                "match",
                MXS_MODULE_PARAM_STRING
            },
            {
                "exclude",
                MXS_MODULE_PARAM_STRING
            },
            {
                "user",
                MXS_MODULE_PARAM_STRING
            },
            {
                "source",
                MXS_MODULE_PARAM_STRING
            },
            {
                "filebase",
                MXS_MODULE_PARAM_STRING,
                NULL,
                MXS_MODULE_OPT_REQUIRED
            },
            {
                "options",
                MXS_MODULE_PARAM_ENUM,
                "ignorecase",
                MXS_MODULE_OPT_NONE,
                option_values
            },
            {
                "log_type",
                MXS_MODULE_PARAM_ENUM,
                "session",
                MXS_MODULE_OPT_NONE,
                log_type_values
            },
            {
                "log_data",
                MXS_MODULE_PARAM_ENUM,
                LOG_DATA_DEFAULT,
                MXS_MODULE_OPT_NONE,
                log_data_values
            },
            {
                "flush",
                MXS_MODULE_PARAM_BOOL,
                "false"
            },
            {
                "append",
                MXS_MODULE_PARAM_BOOL,
                "false"
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param name      The name of the instance (as defined in the config file).
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER *
createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    QLA_INSTANCE *my_instance = (QLA_INSTANCE*) MXS_MALLOC(sizeof(QLA_INSTANCE));

    if (my_instance)
    {
        my_instance->sessions = 0;
        my_instance->unified_fp = NULL;
        my_instance->write_warning_given = false;
        my_instance->name = MXS_STRDUP_A(name);
        my_instance->filebase = MXS_STRDUP_A(config_get_string(params, "filebase"));
        my_instance->flush_writes = config_get_bool(params, "flush");
        my_instance->append = config_get_bool(params, "append");
        my_instance->match = config_copy_string(params, "match");
        my_instance->nomatch = config_copy_string(params, "exclude");
        my_instance->source = config_copy_string(params, "source");
        my_instance->user_name = config_copy_string(params, "user");
        my_instance->log_file_data_flags = config_get_enum(params, "log_data", log_data_values);
        my_instance->log_mode_flags = config_get_enum(params, "log_type", log_type_values);
        bool error = false;

        int cflags = config_get_enum(params, "options", option_values);

        if (my_instance->match && regcomp(&my_instance->re, my_instance->match, cflags))
        {
            MXS_ERROR("Invalid regular expression '%s' for the 'match' "
                      "parameter.", my_instance->match);
            MXS_FREE(my_instance->match);
            my_instance->match = NULL;
            error = true;
        }

        if (my_instance->nomatch && regcomp(&my_instance->nore, my_instance->nomatch, cflags))
        {
            MXS_ERROR("Invalid regular expression '%s' for the 'nomatch'"
                      " parameter.", my_instance->nomatch);
            MXS_FREE(my_instance->nomatch);
            my_instance->nomatch = NULL;
            error = true;
        }

        // Try to open the unified log file
        if (!error && (my_instance->log_mode_flags & CONFIG_FILE_UNIFIED))
        {
            // First calculate filename length
            const char UNIFIED[] = ".unified";
            int namelen = strlen(my_instance->filebase) + sizeof(UNIFIED);
            char *filename = NULL;
            if ((filename = MXS_CALLOC(namelen, sizeof(char))) != NULL)
            {
                snprintf(filename, namelen, "%s.unified", my_instance->filebase);
                // Open the file. It is only closed at program exit
                my_instance->unified_fp = open_log_file(my_instance->log_file_data_flags,
                                                        my_instance, filename);

                if (my_instance->unified_fp == NULL)
                {
                    char errbuf[MXS_STRERROR_BUFLEN];
                    MXS_ERROR("Opening output file for qla "
                              "filter failed due to %d, %s",
                              errno,
                              strerror_r(errno, errbuf, sizeof(errbuf)));
                    error = true;
                }
                MXS_FREE(filename);
            }
            else
            {
                error = true;
            }
        }

        if (error)
        {
            if (my_instance->match)
            {
                MXS_FREE(my_instance->match);
                regfree(&my_instance->re);
            }

            if (my_instance->nomatch)
            {
                MXS_FREE(my_instance->nomatch);
                regfree(&my_instance->nore);
            }
            if (my_instance->unified_fp != NULL)
            {
                fclose(my_instance->unified_fp);
            }
            MXS_FREE(my_instance->filebase);
            MXS_FREE(my_instance->source);
            MXS_FREE(my_instance->user_name);
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

    if ((my_session = MXS_CALLOC(1, sizeof(QLA_SESSION))) != NULL)
    {
        if ((my_session->filename = (char *)MXS_MALLOC(strlen(my_instance->filebase) + 20)) == NULL)
        {
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
                my_session->ses_id); // Fixed possible race condition

        // Multiple sessions can try to update my_instance->sessions simultaneously
        atomic_add(&(my_instance->sessions), 1);

        // Only open the session file if the corresponding mode setting is used
        if (my_session->active && (my_instance->log_mode_flags & CONFIG_FILE_SESSION))
        {
            uint32_t data_flags = (my_instance->log_file_data_flags &
                                   ~LOG_DATA_SESSION); // No point printing "Session"
            my_session->fp = open_log_file(data_flags, my_instance, my_session->filename);

            if (my_session->fp == NULL)
            {
                char errbuf[MXS_STRERROR_BUFLEN];
                MXS_ERROR("Opening output file for qla "
                          "filter failed due to %d, %s",
                          errno,
                          strerror_r(errno, errbuf, sizeof(errbuf)));
                MXS_FREE(my_session->filename);
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
    char *sql;
    struct tm t;
    struct timeval tv;
    regmatch_t limits[] = {{0, 0}};

    if (my_session->active)
    {
        if (modutil_extract_SQL(queue, &sql, &limits[0].rm_eo))
        {
            if ((my_instance->match == NULL ||
                 regexec(&my_instance->re, sql, 0, limits, REG_STARTEND) == 0) &&
                (my_instance->nomatch == NULL ||
                 regexec(&my_instance->nore, sql, 0, limits, REG_STARTEND) != 0))
            {
                char buffer[QLA_DATE_BUFFER_SIZE];
                gettimeofday(&tv, NULL);
                localtime_r(&tv.tv_sec, &t);
                strftime(buffer, sizeof(buffer), "%F %T", &t);

                /**
                 * Loop over all the possible log file modes and write to
                 * the enabled files.
                 */
                int length = limits[0].rm_eo;
                bool write_error = false;
                if (my_instance->log_mode_flags & CONFIG_FILE_SESSION)
                {
                    // In this case there is no need to write the session
                    // number into the files.
                    uint32_t data_flags = (my_instance->log_file_data_flags &
                                           ~LOG_DATA_SESSION);

                    if (write_log_entry(data_flags, my_session->fp,
                                        my_instance, my_session, buffer, sql, length) < 0)
                    {
                        write_error = true;
                    }
                }
                if (my_instance->log_mode_flags & CONFIG_FILE_UNIFIED)
                {
                    uint32_t data_flags = my_instance->log_file_data_flags;
                    if (write_log_entry(data_flags, my_instance->unified_fp,
                                        my_instance, my_session, buffer, sql, length) < 0)
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
        }
    }
    /* Pass the query downstream */
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session, queue);
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
 * @param   dcb     The DCB for diagnostic output
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
    if (my_instance->nomatch)
    {
        dcb_printf(dcb, "\t\tExclude queries that match     %s\n",
                   my_instance->nomatch);
    }
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}
/**
 * Open the log file and print a header if appropriate.
 * @param   data_flags  Data save settings flags
 * @param   instance    The filter instance
 * @param   filename    Target file path
 * @return  A valid file on success, null otherwise.
 */
static FILE* open_log_file(uint32_t data_flags, QLA_INSTANCE *instance, const char *filename)
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
        // Using fopen() with 'a+' means we will always write to the end but can read
        // anywhere. Depending on the "append"-setting the file has been
        // opened in different modes, which should be considered if file handling
        // changes later (e.g. rewinding).
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
    if (fp && !file_existed)
    {
        // Print a header. Luckily, we know the header has limited length
        const char SERVICE[] = "Service,";
        const char SESSION[] = "Session,";
        const char DATE[] = "Date,";
        const char USERHOST[] = "User@Host,";
        const char QUERY[] = "Query,";

        const int headerlen = sizeof(SERVICE) + sizeof(SERVICE) + sizeof(DATE) +
                              sizeof(USERHOST) + sizeof(QUERY);

        char print_str[headerlen];
        memset(print_str, '\0', headerlen);

        char *current_pos = print_str;
        if (instance->log_file_data_flags & LOG_DATA_SERVICE)
        {
            strcat(current_pos, SERVICE);
            current_pos += sizeof(SERVICE) - 1;
        }
        if (instance->log_file_data_flags & LOG_DATA_SESSION)
        {
            strcat(current_pos, SESSION);
            current_pos += sizeof(SERVICE) - 1;
        }
        if (instance->log_file_data_flags & LOG_DATA_DATE)
        {
            strcat(current_pos, DATE);
            current_pos += sizeof(DATE) - 1;
        }
        if (instance->log_file_data_flags & LOG_DATA_USER)
        {
            strcat(current_pos, USERHOST);
            current_pos += sizeof(USERHOST) - 1;
        }
        if (instance->log_file_data_flags & LOG_DATA_QUERY)
        {
            strcat(current_pos, QUERY);
            current_pos += sizeof(QUERY) - 1;
        }
        if (current_pos > print_str)
        {
            // Overwrite the last ','.
            *(current_pos - 1) = '\n';
        }
        else
        {
            // Nothing to print
            return fp;
        }

        // Finally, write the log header.
        int written = fprintf(fp, "%s", print_str);

        if ((written <= 0) ||
            ((instance->flush_writes) && (fflush(fp) < 0)))
        {
            // Weird error, file opened but a write failed. Best to stop.
            fclose(fp);
            MXS_ERROR("Failed to print header to file %s.", filename);
            return NULL;
        }
    }
    return fp;
}

/**
 * Write an entry to the log file.
 * @param   data_flags    Controls what to write
 * @param   logfile    Target file
 * @param   instance    Filter instance
 * @param   session    Filter session
 * @param   time_string Date entry
 * @param   sql_string SQL-query, not NULL terminated!
 * @param   sql_str_len Length of SQL-string
 * @return  The number of characters written, or a negative value on failure
 */
static int write_log_entry(uint32_t data_flags, FILE *logfile, QLA_INSTANCE *instance,
                           QLA_SESSION *session, const char *time_string, const char *sql_string,
                           size_t sql_str_len)
{
    ss_dassert(logfile != NULL);
    size_t print_len = 0;

    // First calculate an upper limit for the total length. The strlen()-calls
    // could be removed if the values would be saved into the instance or session
    // or if we had some reasonable max lengths. (Apparently there are max lengths
    // but they are much higher than what is typically needed)

    // The numbers have some extra for delimiters.
    if (data_flags & LOG_DATA_SERVICE)
    {
        print_len += strlen(session->service) + 1;
    }
    if (data_flags & LOG_DATA_SESSION)
    {
        print_len += 20; // To print a 64bit integer
    }
    if (data_flags & LOG_DATA_DATE)
    {
        print_len += QLA_DATE_BUFFER_SIZE + 1;
    }
    if (data_flags & LOG_DATA_USER)
    {
        print_len += strlen(session->user) + strlen(session->remote) + 2;
    }
    if (data_flags & LOG_DATA_QUERY)
    {
        print_len += sql_str_len + 1; // Can't use strlen, not null-terminated
    }

    if (print_len == 0)
    {
        return 0; // Nothing to print
    }

    // Allocate space for a buffer. Printing to the file in parts would likely
    // cause garbled printing if several threads write simultaneously, so we
    // have to first print to a string.
    char *print_str = NULL;
    if ((print_str = MXS_CALLOC(print_len, sizeof(char))) == NULL)
    {
        return -1;
    }

    bool error = false;
    char *current_pos = print_str;
    int rval = 0;
    if (!error && (data_flags & LOG_DATA_SERVICE))
    {
        if ((rval = sprintf(current_pos, "%s,", session->service)) < 0)
        {
            error = true;
        }
        else
        {
            current_pos += rval;
        }
    }
    if (!error && (data_flags & LOG_DATA_SESSION))
    {
        if ((rval = sprintf(current_pos, "%lu,", session->ses_id)) < 0)
        {
            error = true;
        }
        else
        {
            current_pos += rval;
        }
    }
    if (!error && (data_flags & LOG_DATA_DATE))
    {
        if ((rval = sprintf(current_pos, "%s,", time_string)) < 0)
        {
            error = true;
        }
        else
        {
            current_pos += rval;
        }
    }
    if (!error && (data_flags & LOG_DATA_USER))
    {
        if ((rval = sprintf(current_pos, "%s@%s,", session->user, session->remote)) < 0)
        {
            error = true;
        }
        else
        {
            current_pos += rval;
        }
    }
    if (!error && (data_flags & LOG_DATA_QUERY))
    {
        strncat(current_pos, sql_string, sql_str_len); // non-null-terminated string
        current_pos += sql_str_len + 1; // +1 to move to the next char after
    }
    if (error || current_pos <= print_str)
    {
        MXS_FREE(print_str);
        MXS_ERROR("qlafilter ('%s'): Failed to format log event.", instance->name);
        return -1;
    }
    else
    {
        // Overwrite the last ','. The rest is already filled with 0.
        *(current_pos - 1) = '\n';
    }

    // Finally, write the log event.
    int written = fprintf(logfile, "%s", print_str);
    MXS_FREE(print_str);

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
