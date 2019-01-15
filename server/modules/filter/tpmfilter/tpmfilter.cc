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
 * @file tpmfilter.c - Transaction Performance Monitoring Filter
 * @verbatim
 *
 * A simple filter that groups queries into a transaction with the latency.
 *
 * The filter reads the routed queries, groups them into a transaction by
 * detecting 'commit' statement at the end. The transactions are timestamped with a
 * unix-timestamp and the latency of a transaction is recorded in milliseconds.
 * The filter will not record transactions that are rolled back.
 * Please note that the filter only works with 'autocommit' option disabled.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 *
 * Optional parameters:
 *  filename=<name of the file to which transaction performance logs are written (default=tpm.log)>
 *  delimiter=<delimiter for columns in a log (default=':::')>
 *  query_delimiter=<delimiter for query statements in a transaction (default='@@@')>
 *  source=<source address to limit filter>
 *  user=<username to limit filter>
 *
 * Date         Who             Description
 * 06/12/2015   Dong Young Yoon Initial implementation
 * 14/12/2016   Dong Young Yoon Prints which server the query executed on
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "tpmfilter"

#include <maxscale/ccdefs.hh>

#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <regex.h>
#include <thread>

#include <maxscale/alloc.h>
#include <maxscale/filter.hh>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxscale/server.hh>
#include <maxbase/atomic.h>
#include <maxscale/query_classifier.hh>

/* The maximum size for query statements in a transaction (64MB) */
static size_t sql_size_limit = 64 * 1024 * 1024;
/* The size of the buffer for recording latency of individual statements */
static int latency_buf_size = 64 * 1024;
static const int default_sql_size = 4 * 1024;

#define DEFAULT_QUERY_DELIMITER "@@@"
#define DEFAULT_LOG_DELIMITER   ":::"
#define DEFAULT_FILE_NAME       "tpm.log"
#define DEFAULT_NAMED_PIPE      "/tmp/tpmfilter"

/*
 * The filter entry points
 */
struct TPM_INSTANCE;

static MXS_FILTER*         createInstance(const char* name, MXS_CONFIG_PARAMETER*);
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session);
static void                closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                setDownstream(MXS_FILTER* instance,
                                         MXS_FILTER_SESSION* fsession,
                                         MXS_DOWNSTREAM* downstream);
static void setUpstream(MXS_FILTER* instance,
                        MXS_FILTER_SESSION* fsession,
                        MXS_UPSTREAM* upstream);
static int      routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static int      clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static void     diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb);
static json_t*  diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession);
static uint64_t getCapabilities(MXS_FILTER* instance);
static void     destroyInstance(MXS_FILTER* instance);

static void checkNamedPipe(TPM_INSTANCE* args);

/**
 * A instance structure, every instance will write to a same file.
 */
struct TPM_INSTANCE
{
    int   sessions;         /* Session count */
    char* source;           /* The source of the client connection */
    char* user;             /* The user name to filter on */
    char* filename;         /* filename */
    char* delimiter;        /* delimiter for columns in a log */
    char* query_delimiter;  /* delimiter for query statements in a transaction */
    char* named_pipe;
    int   named_pipe_fd;
    bool  log_enabled;

    int         query_delimiter_size;   /* the length of the query delimiter */
    FILE*       fp;
    std::thread thread;
    bool        shutdown;
};

/**
 * The session structure for this TPM filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
typedef struct
{
    MXS_DOWNSTREAM down;
    MXS_UPSTREAM   up;
    int            active;
    char*          clientHost;
    char*          userName;
    char*          sql;
    char*          latency;
    struct timeval start;
    char*          current;
    int            n_statements;
    struct timeval total;
    struct timeval current_start;
    struct timeval last_statement_start;
    bool           query_end;
    char*          buf;
    int            sql_index;
    int            latency_index;
    size_t         max_sql_size;
} TPM_SESSION;

extern "C"
{

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
        static const char description[] = "Transaction Performance Monitoring filter";
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
            destroyInstance
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_FILTER,
            MXS_MODULE_GA,
            MXS_FILTER_VERSION,
            description,
            "V1.0.1",
            RCAP_TYPE_CONTIGUOUS_INPUT,
            &MyObject,
            NULL,
            NULL,
            NULL,
            NULL,
            {
                {"named_pipe",         MXS_MODULE_PARAM_STRING,  DEFAULT_NAMED_PIPE     },
                {"filename",           MXS_MODULE_PARAM_STRING,  DEFAULT_FILE_NAME      },
                {"delimiter",          MXS_MODULE_PARAM_STRING,  DEFAULT_LOG_DELIMITER  },
                {"query_delimiter",    MXS_MODULE_PARAM_STRING,  DEFAULT_QUERY_DELIMITER},
                {"source",             MXS_MODULE_PARAM_STRING},
                {"user",               MXS_MODULE_PARAM_STRING},
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER* createInstance(const char* name, MXS_CONFIG_PARAMETER* params)
{
    TPM_INSTANCE* my_instance = static_cast<TPM_INSTANCE*>(MXS_CALLOC(1, sizeof(TPM_INSTANCE)));

    if (my_instance)
    {
        my_instance->sessions = 0;
        my_instance->log_enabled = false;
        my_instance->filename = MXS_STRDUP_A(config_get_string(params, "filename"));
        my_instance->delimiter = MXS_STRDUP_A(config_get_string(params, "delimiter"));
        my_instance->query_delimiter = MXS_STRDUP_A(config_get_string(params, "query_delimiter"));
        my_instance->query_delimiter_size = strlen(my_instance->query_delimiter);
        my_instance->named_pipe = MXS_STRDUP_A(config_get_string(params, "named_pipe"));
        my_instance->source = config_copy_string(params, "source");
        my_instance->user = config_copy_string(params, "user");

        bool error = false;

        // check if the file exists first.
        if (access(my_instance->named_pipe, F_OK) == 0)
        {
            // if exists, check if it is a named pipe.
            struct stat st;
            int ret = stat(my_instance->named_pipe, &st);

            // check whether the file is named pipe.
            if (ret == -1 && errno != ENOENT)
            {
                MXS_ERROR("stat() failed on named pipe: %s", strerror(errno));
                error = true;
            }
            else if (ret == 0 && S_ISFIFO(st.st_mode))
            {
                // if it is a named pipe, we delete it and recreate it.
                unlink(my_instance->named_pipe);
            }
            else
            {
                MXS_ERROR("The file '%s' already exists and it is not "
                          "a named pipe.",
                          my_instance->named_pipe);
                error = true;
            }
        }

        // now create the named pipe.
        if (mkfifo(my_instance->named_pipe, 0660) == -1)
        {
            MXS_ERROR("mkfifo() failed on named pipe: %s", strerror(errno));
            error = true;
        }


        my_instance->fp = fopen(my_instance->filename, "w");

        if (my_instance->fp == NULL)
        {
            MXS_ERROR("Opening output file '%s' for tpmfilter failed due to %d, %s",
                      my_instance->filename,
                      errno,
                      strerror(errno));
            error = true;
        }

        /*
         * Launch a thread that checks the named pipe.
         */
        if (!error)
        {
            try
            {
                my_instance->thread = std::thread(checkNamedPipe, my_instance);
            }
            catch (const std::exception& x)
            {
                MXS_ERROR("Couldn't create a thread to check the named pipe: %s", x.what());
                error = true;
            }
        }

        if (error)
        {
            MXS_FREE(my_instance->delimiter);
            MXS_FREE(my_instance->filename);
            MXS_FREE(my_instance->named_pipe);
            MXS_FREE(my_instance->query_delimiter);
            MXS_FREE(my_instance->source);
            MXS_FREE(my_instance->user);
            if (my_instance->fp)
            {
                fclose(my_instance->fp);
            }
            MXS_FREE(my_instance);
        }
    }

    return (MXS_FILTER*)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * Every session uses the same log file.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session)
{
    TPM_INSTANCE* my_instance = (TPM_INSTANCE*)instance;
    TPM_SESSION* my_session;
    int i;
    const char* remote, * user;

    if ((my_session = static_cast<TPM_SESSION*>(MXS_CALLOC(1, sizeof(TPM_SESSION)))) != NULL)
    {
        atomic_add(&my_instance->sessions, 1);

        my_session->latency = (char*)MXS_CALLOC(latency_buf_size, sizeof(char));
        my_session->max_sql_size = default_sql_size;    // default max query size of 4k.
        my_session->sql = (char*)MXS_CALLOC(my_session->max_sql_size, sizeof(char));
        memset(my_session->sql, 0x00, my_session->max_sql_size);
        my_session->sql_index = 0;
        my_session->latency_index = 0;
        my_session->n_statements = 0;
        my_session->total.tv_sec = 0;
        my_session->total.tv_usec = 0;
        my_session->current = NULL;
        if ((remote = session_get_remote(session)) != NULL)
        {
            my_session->clientHost = MXS_STRDUP_A(remote);
        }
        else
        {
            my_session->clientHost = NULL;
        }
        if ((user = session_get_user(session)) != NULL)
        {
            my_session->userName = MXS_STRDUP_A(user);
        }
        else
        {
            my_session->userName = NULL;
        }
        my_session->active = 1;
        if (my_instance->source && my_session->clientHost && strcmp(my_session->clientHost,
                                                                    my_instance->source))
        {
            my_session->active = 0;
        }
        if (my_instance->user && my_session->userName && strcmp(my_session->userName,
                                                                my_instance->user))
        {
            my_session->active = 0;
        }
    }

    return (MXS_FILTER_SESSION*)my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    TPM_SESSION* my_session = (TPM_SESSION*)session;
    TPM_INSTANCE* my_instance = (TPM_INSTANCE*)instance;
    if (my_instance->fp != NULL)
    {
        // flush FP when a session is closed.
        fflush(my_instance->fp);
    }
}

/**
 * Free the memory associated with the session
 *
 * @param instance  The filter instance
 * @param session   The filter session
 */
static void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    TPM_SESSION* my_session = (TPM_SESSION*)session;

    MXS_FREE(my_session->clientHost);
    MXS_FREE(my_session->userName);
    MXS_FREE(my_session->sql);
    MXS_FREE(my_session->latency);
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
static void setDownstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_DOWNSTREAM* downstream)
{
    TPM_SESSION* my_session = (TPM_SESSION*)session;

    my_session->down = *downstream;
}

/**
 * Set the upstream filter or session to which results will be
 * passed from this filter.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param upstream  The upstream filter or session.
 */
static void setUpstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_UPSTREAM* upstream)
{
    TPM_SESSION* my_session = (TPM_SESSION*)session;

    my_session->up = *upstream;
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
static int routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    TPM_INSTANCE* my_instance = (TPM_INSTANCE*)instance;
    TPM_SESSION* my_session = (TPM_SESSION*)session;
    char* ptr = NULL;
    size_t i;

    if (my_session->active)
    {
        if ((ptr = modutil_get_SQL(queue)) != NULL)
        {
            uint32_t query_type = qc_get_type_mask(queue);
            int query_len = strlen(ptr);
            my_session->query_end = false;

            /* check for commit and rollback */
            if (query_type & QUERY_TYPE_COMMIT)
            {
                my_session->query_end = true;
            }
            else if (query_type & QUERY_TYPE_ROLLBACK)
            {
                my_session->query_end = true;
                my_session->sql_index = 0;
            }

            /* for normal sql statements */
            if (!my_session->query_end)
            {
                /* check and expand buffer size first. */
                size_t new_sql_size = my_session->max_sql_size;
                size_t len = my_session->sql_index + strlen(ptr) + my_instance->query_delimiter_size + 1;

                /* if the total length of query statements exceeds the maximum limit, print an error and
                 * return */
                if (len > sql_size_limit)
                {
                    MXS_ERROR("The size of query statements exceeds the maximum buffer limit of 64MB.");
                    goto retblock;
                }

                /* double buffer size until the buffer fits the query */
                while (len > new_sql_size)
                {
                    new_sql_size *= 2;
                }
                if (new_sql_size > my_session->max_sql_size)
                {
                    char* new_sql = (char*)MXS_CALLOC(new_sql_size, sizeof(char));
                    if (new_sql == NULL)
                    {
                        MXS_ERROR("Memory allocation failure.");
                        goto retblock;
                    }
                    memcpy(new_sql, my_session->sql, my_session->sql_index);
                    MXS_FREE(my_session->sql);
                    my_session->sql = new_sql;
                    my_session->max_sql_size = new_sql_size;
                }

                /* first statement */
                if (my_session->sql_index == 0)
                {
                    memcpy(my_session->sql, ptr, strlen(ptr));
                    my_session->sql_index += strlen(ptr);
                    gettimeofday(&my_session->current_start, NULL);
                }
                /* otherwise, append the statement with semicolon as a statement delimiter */
                else
                {
                    /* append a query delimiter */
                    memcpy(my_session->sql + my_session->sql_index,
                           my_instance->query_delimiter,
                           my_instance->query_delimiter_size);
                    /* append the next query statement */
                    memcpy(my_session->sql + my_session->sql_index + my_instance->query_delimiter_size,
                           ptr,
                           strlen(ptr));
                    /* set new pointer for the buffer */
                    my_session->sql_index += (my_instance->query_delimiter_size + strlen(ptr));
                }
                gettimeofday(&my_session->last_statement_start, NULL);
            }
        }
    }

retblock:

    MXS_FREE(ptr);
    /* Pass the query downstream */
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session,
                                       queue);
}

static int clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* reply)
{
    TPM_INSTANCE* my_instance = (TPM_INSTANCE*)instance;
    TPM_SESSION* my_session = (TPM_SESSION*)session;
    struct      timeval tv, diff;
    int i, inserted;

    /* records latency of the SQL statement. */
    if (my_session->sql_index > 0)
    {
        gettimeofday(&tv, NULL);
        timersub(&tv, &(my_session->last_statement_start), &diff);

        /* get latency */
        double millis = (diff.tv_sec * 1000 + diff.tv_usec / 1000.0);

        int written = sprintf(my_session->latency + my_session->latency_index, "%.3f", millis);
        my_session->latency_index += written;
        if (!my_session->query_end)
        {
            written = sprintf(my_session->latency + my_session->latency_index,
                              "%s",
                              my_instance->query_delimiter);
            my_session->latency_index += written;
        }
        if (my_session->latency_index > latency_buf_size)
        {
            MXS_ERROR("Latency buffer overflow.");
        }
    }

    /* found 'commit' and sql statements exist. */
    if (my_session->query_end && my_session->sql_index > 0)
    {
        gettimeofday(&tv, NULL);
        timersub(&tv, &(my_session->current_start), &diff);

        /* get latency */
        uint64_t millis = (diff.tv_sec * (uint64_t)1000 + diff.tv_usec / 1000);
        /* get timestamp */
        uint64_t timestamp = (tv.tv_sec + (tv.tv_usec / (1000 * 1000)));

        *(my_session->sql + my_session->sql_index) = '\0';

        /* print to log. */
        if (my_instance->log_enabled)
        {
            /* this prints "timestamp | server_name | user_name | latency of entire transaction | latencies of
             * individual statements | sql_statements" */
            fprintf(my_instance->fp,
                    "%ld%s%s%s%s%s%ld%s%s%s%s\n",
                    timestamp,
                    my_instance->delimiter,
                    reply->server->name(),
                    my_instance->delimiter,
                    my_session->userName,
                    my_instance->delimiter,
                    millis,
                    my_instance->delimiter,
                    my_session->latency,
                    my_instance->delimiter,
                    my_session->sql);
        }

        my_session->sql_index = 0;
        my_session->latency_index = 0;
    }

    /* Pass the result upstream */
    return my_session->up.clientReply(my_session->up.instance,
                                      my_session->up.session,
                                      reply);
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
static void diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb)
{
    TPM_INSTANCE* my_instance = (TPM_INSTANCE*)instance;
    TPM_SESSION* my_session = (TPM_SESSION*)fsession;
    int i;

    if (my_instance->source)
    {
        dcb_printf(dcb,
                   "\t\tLimit logging to connections from   %s\n",
                   my_instance->source);
    }
    if (my_instance->user)
    {
        dcb_printf(dcb,
                   "\t\tLimit logging to user		%s\n",
                   my_instance->user);
    }
    if (my_instance->filename)
    {
        dcb_printf(dcb,
                   "\t\tLogging to file %s.\n",
                   my_instance->filename);
    }
    if (my_instance->delimiter)
    {
        dcb_printf(dcb,
                   "\t\tLogging with delimiter %s.\n",
                   my_instance->delimiter);
    }
    if (my_instance->query_delimiter)
    {
        dcb_printf(dcb,
                   "\t\tLogging with query delimiter %s.\n",
                   my_instance->query_delimiter);
    }
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
static json_t* diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession)
{
    TPM_INSTANCE* my_instance = (TPM_INSTANCE*)instance;

    json_t* rval = json_object();

    if (my_instance->source)
    {
        json_object_set_new(rval, "source", json_string(my_instance->source));
    }

    if (my_instance->user)
    {
        json_object_set_new(rval, "user", json_string(my_instance->user));
    }

    if (my_instance->filename)
    {
        json_object_set_new(rval, "filename", json_string(my_instance->filename));
    }

    if (my_instance->delimiter)
    {
        json_object_set_new(rval, "delimiter", json_string(my_instance->delimiter));
    }

    if (my_instance->query_delimiter)
    {
        json_object_set_new(rval, "query_delimiter", json_string(my_instance->query_delimiter));
    }

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

static void destroyInstance(MXS_FILTER* instance)
{
    TPM_INSTANCE* my_instance = (TPM_INSTANCE*)instance;

    my_instance->shutdown = true;

    if (my_instance->thread.joinable())
    {
        my_instance->thread.join();
    }
}

static void checkNamedPipe(TPM_INSTANCE* inst)
{
    int ret;
    char buffer[2];
    char buf[4096];
    char* named_pipe = inst->named_pipe;

    // open named pipe and this will block until middleware opens it.
    while (!inst->shutdown && ((inst->named_pipe_fd = open(named_pipe, O_RDONLY)) > 0))
    {
        // 1 for start logging, 0 for stopping.
        while (!inst->shutdown && ((ret = read(inst->named_pipe_fd, buffer, 1)) > 0))
        {
            if (buffer[0] == '1')
            {
                // reopens the log file.
                inst->fp = fopen(inst->filename, "w");
                if (inst->fp == NULL)
                {
                    MXS_ERROR("Failed to open a log file for tpmfilter.");
                    MXS_FREE(inst);
                    return;
                }
                inst->log_enabled = true;
            }
            else if (buffer[0] == '0')
            {
                inst->log_enabled = false;
            }
        }
        if (ret == 0)
        {
            close(inst->named_pipe_fd);
        }
    }

    if (!inst->shutdown && (inst->named_pipe_fd == -1))
    {
        MXS_ERROR("Failed to open the named pipe '%s': %s", named_pipe, strerror(errno));
        return;
    }

    return;
}
