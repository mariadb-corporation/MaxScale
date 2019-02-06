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
 * @file qlafilter.cc - Quary Log All Filter
 *
 * QLA Filter - Query Log All. A simple query logging filter. All queries passing
 * through the filter are written to a text file.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 */


#include "qlafilter.hh"

#include <cmath>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sstream>
#include <sys/time.h>
#include <fstream>
#include <sstream>
#include <string>

#include <maxscale/alloc.h>
#include <maxbase/atomic.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.h>
#include <maxscale/modulecmd.hh>
#include <maxscale/json_api.h>

using std::string;

/* Log file save mode flags */
#define CONFIG_FILE_SESSION (1 << 0)    // Default value, session specific files
#define CONFIG_FILE_UNIFIED (1 << 1)    // One file shared by all sessions

/* Default values for logged data */
#define LOG_DATA_DEFAULT "date,user,query"

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

/* The filter entry points */
static int      routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static int      clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue);
static void     diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb);
static json_t*  diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession);

static int write_log_entry(FILE*, QlaInstance*, QlaFilterSession*, uint32_t,
                           const char*, const char*, size_t, int);
static bool cb_log(const MODULECMD_ARG* argv, json_t** output);

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0             },
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
    {"service",    LOG_DATA_SERVICE   },
    {"session",    LOG_DATA_SESSION   },
    {"date",       LOG_DATA_DATE      },
    {"user",       LOG_DATA_USER      },
    {"query",      LOG_DATA_QUERY     },
    {"reply_time", LOG_DATA_REPLY_TIME},
    {NULL}
};

QlaInstance::QlaInstance(const string& name, MXS_CONFIG_PARAMETER* params)
    : name(name)
    , log_mode_flags(params->get_enum(PARAM_LOG_TYPE, log_type_values))
    , log_file_data_flags(params->get_enum(PARAM_LOG_DATA, log_data_values))
    , filebase(params->get_string(PARAM_FILEBASE))
    , unified_fp(NULL)
    , flush_writes(params->get_bool(PARAM_FLUSH))
    , append(params->get_bool(PARAM_APPEND))
    , query_newline(params->get_string(PARAM_NEWLINE))
    , separator(params->get_string(PARAM_SEPARATOR))
    , write_warning_given(false)
    , user_name(params->get_string(PARAM_USER))
    , source(params->get_string(PARAM_SOURCE))
    , match(params->get_string(PARAM_MATCH))
    , exclude(params->get_string(PARAM_EXCLUDE))
    , re_match(NULL)
    , re_exclude(NULL)
    , ovec_size(0)
{
}

QlaInstance::~QlaInstance()
{
    pcre2_code_free(re_match);
    pcre2_code_free(re_exclude);
    if (unified_fp != NULL)
    {
        fclose(unified_fp);
    }
}

QlaFilterSession::QlaFilterSession(const char* user, const char* remote, bool ses_active,
                                   pcre2_match_data* mdata,
                                   const string& ses_filename, FILE* ses_file,
                                   size_t ses_id, const char* service)
    : m_user(user)
    , m_remote(remote)
    , m_active(ses_active)
    , m_mdata(mdata)
    , m_filename(ses_filename)
    , m_logfile(ses_file)
    , m_ses_id(ses_id)
    , m_service(service)
{
}

QlaFilterSession::~QlaFilterSession()
{
    pcre2_match_data_free(m_mdata);
    // File should be closed and event data freed by now
    mxb_assert(m_logfile == NULL && m_event_data.has_message == false);
}

void QlaFilterSession::close()
{
    if (m_active && m_logfile)
    {
        fclose(m_logfile);
        m_logfile = nullptr;
    }
    m_event_data.clear();
}

QlaInstance* QlaInstance::create(const std::string name, MXS_CONFIG_PARAMETER* params)
{
    bool error = false;
    QlaInstance* my_instance = NULL;

    const char* keys[] = {PARAM_MATCH, PARAM_EXCLUDE};
    pcre2_code* re_match = NULL;
    pcre2_code* re_exclude = NULL;
    uint32_t ovec_size = 0;
    int cflags = params->get_enum(PARAM_OPTIONS, option_values);
    pcre2_code** code_arr[] = {&re_match, &re_exclude};
    if (config_get_compiled_regexes(params,
                                    keys,
                                    sizeof(keys) / sizeof(char*),
                                    cflags,
                                    &ovec_size,
                                    code_arr))
    {
        // The instance is allocated before opening the file since open_log_file() takes the instance as a
        // parameter. Will be fixed (or at least cleaned) with a later refactoring of functions/methods.
        my_instance = new(std::nothrow) QlaInstance(name, params);
        if (my_instance)
        {
            my_instance->re_match = re_match;
            my_instance->re_exclude = re_exclude;
            my_instance->ovec_size = ovec_size;
            // Try to open the unified log file
            if (my_instance->log_mode_flags & CONFIG_FILE_UNIFIED)
            {
                string unified_filename = my_instance->filebase + ".unified";
                // Open the file. It is only closed at program exit.
                FILE* unified_fp = my_instance->open_log_file(my_instance->log_file_data_flags,
                                                              unified_filename.c_str());
                if (unified_fp != NULL)
                {
                    my_instance->unified_filename = unified_filename;
                    my_instance->unified_fp = unified_fp;
                }
                else
                {
                    MXS_ERROR("Opening output file for qla-filter failed due to %d, %s",
                              errno,
                              mxs_strerror(errno));
                    delete my_instance;
                    my_instance = NULL;
                }
            }
        }
        else
        {
            error = true;
        }
    }
    else
    {
        error = true;
    }

    if (error)
    {
        pcre2_code_free(re_match);
        pcre2_code_free(re_exclude);
    }

    return my_instance;
}

QlaFilterSession* QlaInstance::newSession(MXS_SESSION* session)
{
    // Need the following values before session constructor
    const char* remote = session_get_remote(session);
    const char* userName = session_get_user(session);
    pcre2_match_data* mdata = NULL;
    bool ses_active = true;
    string filename;
    FILE* session_file = NULL;

    bool error = false;

    mxb_assert(userName && remote);
    if ((!this->source.empty() && remote && this->source != remote)
        || (!this->user_name.empty() && userName && this->user_name != userName))
    {
        ses_active = false;
    }

    if (this->ovec_size > 0)
    {
        mdata = pcre2_match_data_create(this->ovec_size, NULL);
        if (mdata == NULL)
        {
            // Can this happen? Would require pcre2 to fail completely.
            MXS_ERROR("pcre2_match_data_create returned NULL.");
            error = true;
        }
    }

    // Only open the session file if the corresponding mode setting is used
    if (!error && ses_active && this->log_mode_flags & CONFIG_FILE_SESSION)
    {
        std::stringstream filename_helper;
        filename_helper << this->filebase << "." << session->ses_id;
        filename = filename_helper.str();

        // Session numbers are not printed to session files
        uint32_t data_flags = (this->log_file_data_flags & ~LOG_DATA_SESSION);

        session_file = open_log_file(data_flags, filename.c_str());
        if (session_file == NULL)
        {
            MXS_ERROR("Opening output file for qla-filter failed due to %d, %s",
                      errno,
                      mxs_strerror(errno));
            error = true;
        }
    }

    QlaFilterSession* my_session = NULL;
    if (!error)
    {
        my_session = new(std::nothrow) QlaFilterSession(userName,
                                                        remote,
                                                        ses_active,
                                                        mdata,
                                                        filename,
                                                        session_file,
                                                        session->ses_id,
                                                        session->service->name);
        if (my_session == NULL)
        {
            error = true;
        }
    }

    if (error)
    {
        pcre2_match_data_free(mdata);
        if (session_file)
        {
            fclose(session_file);
        }
    }
    return my_session;
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
void write_log_entries(QlaInstance* my_instance,
                       QlaFilterSession* my_session,
                       const char* date_string,
                       const char* query,
                       int querylen,
                       int elapsed_ms)
{
    bool write_error = false;
    if (my_instance->log_mode_flags & CONFIG_FILE_SESSION)
    {
        // In this case there is no need to write the session
        // number into the files.
        uint32_t data_flags = (my_instance->log_file_data_flags & ~LOG_DATA_SESSION);
        if (write_log_entry(my_session->m_logfile,
                            my_instance,
                            my_session,
                            data_flags,
                            date_string,
                            query,
                            querylen,
                            elapsed_ms) < 0)
        {
            write_error = true;
        }
    }
    if (my_instance->log_mode_flags & CONFIG_FILE_UNIFIED)
    {
        uint32_t data_flags = my_instance->log_file_data_flags;
        if (write_log_entry(my_instance->unified_fp,
                            my_instance,
                            my_session,
                            data_flags,
                            date_string,
                            query,
                            querylen,
                            elapsed_ms) < 0)
        {
            write_error = true;
        }
    }
    if (write_error && !my_instance->write_warning_given)
    {
        MXS_ERROR("qla-filter '%s': Log file write failed. "
                  "Suppressing further similar warnings.",
                  my_instance->name.c_str());
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
static int routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    QlaInstance* my_instance = (QlaInstance*) instance;
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    char* query = NULL;
    int query_len = 0;

    if (my_session->m_active
        && modutil_extract_SQL(queue, &query, &query_len)
        && mxs_pcre2_check_match_exclude(my_instance->re_match,
                                         my_instance->re_exclude,
                                         my_session->m_mdata,
                                         query,
                                         query_len,
                                         MXS_MODULE_NAME))
    {
        const uint32_t data_flags = my_instance->log_file_data_flags;
        LogEventData& event = my_session->m_event_data;
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
                event.clear();
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
                                       my_session->down.session,
                                       queue);
}

/**
 * The clientReply entry point. Required for measuring and printing query execution time.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    QlaInstance* my_instance = (QlaInstance*) instance;
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    LogEventData& event = my_session->m_event_data;
    if (event.has_message)
    {
        const uint32_t data_flags = my_instance->log_file_data_flags;
        mxb_assert(data_flags & LOG_DATA_REPLY_TIME);
        char* query = NULL;
        int query_len = 0;
        if (data_flags & LOG_DATA_QUERY)
        {
            modutil_extract_SQL(event.query_clone, &query, &query_len);
        }
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);   // Gives time in seconds + nanoseconds
        // Calculate elapsed time in milliseconds.
        double elapsed_ms = 1E3 * (now.tv_sec - event.begin_time.tv_sec)
            + (now.tv_nsec - event.begin_time.tv_nsec) / (double)1E6;
        write_log_entries(my_instance,
                          my_session,
                          event.query_date,
                          query,
                          query_len,
                          std::floor(elapsed_ms + 0.5));
        event.clear();
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
static void diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb)
{
    QlaInstance* my_instance = (QlaInstance*) instance;
    QlaFilterSession* my_session = (QlaFilterSession*) fsession;

    if (my_session)
    {
        dcb_printf(dcb,
                   "\t\tLogging to file            %s.\n",
                   my_session->m_filename.c_str());
    }
    if (!my_instance->source.empty())
    {
        dcb_printf(dcb,
                   "\t\tLimit logging to connections from  %s\n",
                   my_instance->source.c_str());
    }
    if (!my_instance->user_name.empty())
    {
        dcb_printf(dcb,
                   "\t\tLimit logging to user      %s\n",
                   my_instance->user_name.c_str());
    }
    if (!my_instance->match.empty())
    {
        dcb_printf(dcb,
                   "\t\tInclude queries that match     %s\n",
                   my_instance->match.c_str());
    }
    if (!my_instance->exclude.empty())
    {
        dcb_printf(dcb,
                   "\t\tExclude queries that match     %s\n",
                   my_instance->exclude.c_str());
    }
    dcb_printf(dcb,
               "\t\tColumn separator     %s\n",
               my_instance->separator.c_str());
    dcb_printf(dcb,
               "\t\tNewline replacement     %s\n",
               my_instance->query_newline.c_str());
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
    QlaInstance* my_instance = (QlaInstance*)instance;
    QlaFilterSession* my_session = (QlaFilterSession*)fsession;

    json_t* rval = json_object();

    if (my_session)
    {
        json_object_set_new(rval, "session_filename", json_string(my_session->m_filename.c_str()));
    }

    if (!my_instance->source.empty())
    {
        json_object_set_new(rval, PARAM_SOURCE, json_string(my_instance->source.c_str()));
    }

    if (!my_instance->user_name.empty())
    {
        json_object_set_new(rval, PARAM_USER, json_string(my_instance->user_name.c_str()));
    }

    if (!my_instance->match.empty())
    {
        json_object_set_new(rval, PARAM_MATCH, json_string(my_instance->match.c_str()));
    }

    if (!my_instance->exclude.empty())
    {
        json_object_set_new(rval, PARAM_EXCLUDE, json_string(my_instance->exclude.c_str()));
    }
    json_object_set_new(rval, PARAM_SEPARATOR, json_string(my_instance->separator.c_str()));
    json_object_set_new(rval, PARAM_NEWLINE, json_string(my_instance->query_newline.c_str()));

    return rval;
}

/**
 * Open the log file and print a header if appropriate.
 *
 * @param   instance    The filter instance
 * @param   data_flags  Data save settings flags
 * @param   filename    Target file path
 * @return  A valid file on success, null otherwise.
 */
FILE* QlaInstance::open_log_file(uint32_t data_flags, const char* filename)
{
    auto instance = this;
    bool file_existed = false;
    FILE* fp = NULL;
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
            if (ftell(fp) > 0)      // Any errors in ftell cause overwriting
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
        string curr_sep;    // Use empty string as the first separator
        const string& real_sep = instance->separator;

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

static void print_string_replace_newlines(const char* sql_string,
                                          size_t sql_str_len,
                                          const char* rep_newline,
                                          std::stringstream* output)
{
    mxb_assert(output);
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
static int write_log_entry(FILE* logfile,
                           QlaInstance* instance,
                           QlaFilterSession* session,
                           uint32_t data_flags,
                           const char* time_string,
                           const char* sql_string,
                           size_t sql_str_len,
                           int elapsed_ms)
{
    mxb_assert(logfile != NULL);
    if (data_flags == 0)
    {
        // Nothing to print
        return 0;
    }

    /* Printing to the file in parts would likely cause garbled printing if several threads write
     * simultaneously, so we have to first print to a string. */
    std::stringstream output;
    string curr_sep;    // Use empty string as the first separator
    const string& real_sep = instance->separator;

    if (data_flags & LOG_DATA_SERVICE)
    {
        output << session->m_service;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_SESSION)
    {
        output << curr_sep << session->m_ses_id;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_DATE)
    {
        output << curr_sep << time_string;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_USER)
    {
        output << curr_sep << session->m_user << "@" << session->m_remote;
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
        if (!instance->query_newline.empty())
        {
            print_string_replace_newlines(sql_string, sql_str_len, instance->query_newline.c_str(), &output);
        }
        else
        {
            // The newline replacement is an empty string so print the query as is
            output.write(sql_string, sql_str_len);      // non-null-terminated string
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

static bool cb_log(const MODULECMD_ARG* argv, json_t** output)
{
    mxb_assert(argv->argc > 0);
    mxb_assert(argv->argv[0].type.type == MODULECMD_ARG_FILTER);

    MXS_FILTER_DEF* filter = argv[0].argv->value.filter;
    QlaInstance* instance = reinterpret_cast<QlaInstance*>(filter_def_get_instance(filter));
    bool rval = false;

    if (instance->log_mode_flags & CONFIG_FILE_UNIFIED)
    {
        mxb_assert(instance->unified_fp && !instance->unified_filename.empty());
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
                                     instance->unified_filename.c_str());
        }
    }
    else
    {
        *output = mxs_json_error("Filter '%s' does not have unified log file enabled",
                                 filter_def_get_name(filter));
    }

    return rval;
}

namespace
{

MXS_FILTER* createInstance(const char* name, MXS_CONFIG_PARAMETER* params)
{
    return QlaInstance::create(name, params);
}

MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session)
{
    auto my_instance = static_cast<QlaInstance*>(instance);
    return my_instance->newSession(session);
}

void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    auto my_session = static_cast<QlaFilterSession*>(session);
    my_session->close();
}

void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    delete my_session;
}

void setDownstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_DOWNSTREAM* downstream)
{
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    my_session->down = *downstream;
}

void setUpstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_UPSTREAM* upstream)
{
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    my_session->up = *upstream;
}

uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_NONE;
}

}

/**
 * The module entry point routine.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
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

    modulecmd_register_command(MXS_MODULE_NAME, "log", MODULECMD_TYPE_PASSIVE,
                               cb_log, 3, args,
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
        NULL,               // No destroyInstance
    };

    static const char description[] = "A simple query logging filter";
    uint64_t capabilities = RCAP_TYPE_CONTIGUOUS_INPUT;
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        description,
        "V1.1.1",
        capabilities,
        &MyObject,
        NULL,                   /* Process init. */
        NULL,                   /* Process finish. */
        NULL,                   /* Thread init. */
        NULL,                   /* Thread finish. */
        {
            {
                PARAM_MATCH,    MXS_MODULE_PARAM_REGEX
            },
            {
                PARAM_EXCLUDE,  MXS_MODULE_PARAM_REGEX
            },
            {
                PARAM_USER,     MXS_MODULE_PARAM_STRING
            },
            {
                PARAM_SOURCE,   MXS_MODULE_PARAM_STRING
            },
            {
                PARAM_FILEBASE, MXS_MODULE_PARAM_STRING,        NULL,             MXS_MODULE_OPT_REQUIRED
            },
            {
                PARAM_OPTIONS,  MXS_MODULE_PARAM_ENUM,          "ignorecase",     MXS_MODULE_OPT_NONE,
                option_values
            },
            {
                PARAM_LOG_TYPE, MXS_MODULE_PARAM_ENUM,          "session",        MXS_MODULE_OPT_NONE,
                log_type_values
            },
            {
                PARAM_LOG_DATA, MXS_MODULE_PARAM_ENUM,          LOG_DATA_DEFAULT, MXS_MODULE_OPT_NONE,
                log_data_values
            },
            {
                PARAM_NEWLINE,  MXS_MODULE_PARAM_QUOTEDSTRING,  "\" \"",          MXS_MODULE_OPT_NONE
            },
            {
                PARAM_SEPARATOR,MXS_MODULE_PARAM_QUOTEDSTRING,  ",",              MXS_MODULE_OPT_NONE
            },
            {
                PARAM_FLUSH,    MXS_MODULE_PARAM_BOOL,          "false"
            },
            {
                PARAM_APPEND,   MXS_MODULE_PARAM_BOOL,          "false"
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
