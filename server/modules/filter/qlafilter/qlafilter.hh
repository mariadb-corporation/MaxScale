/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#define MXS_MODULE_NAME "qlafilter"

#include <maxscale/ccdefs.hh>

#include <string>
#include <maxscale/config.hh>
#include <maxscale/filter.hh>
#include <maxscale/pcre2.h>


/* Date string buffer size */
#define QLA_DATE_BUFFER_SIZE 20

class QlaFilterSession;

/**
 * Helper struct for holding data before it's written to file.
 */
class LogEventData
{
public:
    LogEventData(const LogEventData&) = delete;
    LogEventData& operator=(const LogEventData&) = default;
    LogEventData() = default;

    ~LogEventData()
    {
        mxb_assert(query_clone == NULL);
    }

    /**
     * Resets event data.
     *
     * @param event Event to reset
     */
    void clear()
    {
        gwbuf_free(query_clone);
        *this = LogEventData();
    }

    bool     has_message {false};               // Does message data exist?
    GWBUF*   query_clone {nullptr};             // Clone of the query buffer.
    char     query_date[QLA_DATE_BUFFER_SIZE];  // Text representation of date.
    timespec begin_time {0, 0};                 // Timer value at the moment of receiving query.
};

/**
 * A instance structure, the assumption is that the option passed
 * to the filter is simply a base for the filename to which the queries
 * are logged.
 *
 * To this base a session number is attached such that each session will
 * have a unique name.
 */
class QlaInstance : public MXS_FILTER
{
public:
    QlaInstance(const QlaInstance&) = delete;
    QlaInstance& operator=(const QlaInstance&) = delete;

    QlaInstance(const std::string& name, MXS_CONFIG_PARAMETER* params);
    ~QlaInstance();

    /**
     * Associate a new session with this instance of the filter. Creates a session-specific logfile.
     *
     * @param session   The generic session
     * @return          Router session on null on error
     */
    QlaFilterSession* newSession(MXS_SESSION* session);

    /**
     * Create an instance of the filter for a particular service within MaxScale.
     *
     * @param name      The name of the instance (as defined in the config file)
     * @param params    The array of name/value pair parameters for the filter
     * @return          The new filter instance, or NULL on error
     */
    static QlaInstance* create(const std::string name, MXS_CONFIG_PARAMETER* params);

    std::string name;   /* Filter definition name */

    uint32_t log_mode_flags;        /* Log file mode settings */
    uint32_t log_file_data_flags;   /* What data is saved to the files */

    std::string filebase;           /* The filename base */
    std::string unified_filename;   /* Filename of the unified log file */
    FILE*       unified_fp;         /* Unified log file. The pointer needs to be shared here
                                     * to avoid garbled printing. */
    bool        flush_writes;       /* Flush log file after every write? */
    bool        append;             /* Open files in append-mode? */
    std::string query_newline;      /* Character(s) used to replace a newline within a query */
    std::string separator;          /*  Character(s) used to separate elements */
    bool        write_warning_given;/* Avoid repeatedly printing some errors/warnings. */

    std::string user_name;  /* The user name to filter on */
    std::string source;     /* The source of the client connection to filter on */

    std::string match;      /* Optional text to match against */
    std::string exclude;    /* Optional text to match against for exclusion */
    pcre2_code* re_match;   /* Compiled regex text */
    pcre2_code* re_exclude; /* Compiled regex nomatch text */
    uint32_t    ovec_size;  /* PCRE2 match data ovector size */

private:
    FILE* open_log_file(uint32_t, const char*);
};

/* The session structure for this QLA filter. */
class QlaFilterSession : public MXS_FILTER_SESSION
{
public:
    QlaFilterSession(const QlaFilterSession&);
    QlaFilterSession& operator=(const QlaFilterSession&);
    QlaFilterSession(const char* user, const char* remote, bool ses_active,
                     pcre2_match_data* mdata, const std::string& ses_filename, FILE* ses_file,
                     size_t ses_id, const char* service);
    ~QlaFilterSession();

    /**
     * Close a session with the filter. Close the file descriptor and reset event info.
     */
    void close();

    const char*       m_user;       /* Client username */
    const char*       m_remote;     /* Client address */
    bool              m_active;     /* Is session active? */
    pcre2_match_data* m_mdata;      /* Regex match data */
    std::string       m_filename;   /* The session-specific log file name */
    FILE*             m_logfile;    /* The session-specific log file */
    size_t            m_ses_id;     /* The session this filter session serves. */
    const char*       m_service;    /* The service name this filter is attached to. */
    LogEventData      m_event_data; /* Information about the latest event, used if logging execution time. */

    MXS_UPSTREAM   up;
    MXS_DOWNSTREAM down;
};
