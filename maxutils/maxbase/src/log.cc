/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/log.hh>
#include <maxbase/stacktrace.hh>

#include <sys/time.h>
#include <syslog.h>

#ifdef HAVE_SYSTEMD
// Prevents line numbers from being automatically added to the calls
#define SD_JOURNAL_SUPPRESS_LOCATION 1
#include <systemd/sd-journal.h>
#endif

#include <cinttypes>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

#include <maxbase/assert.hh>
#include <maxbase/logger.hh>

/**
 * Variable holding the enabled priorities information.
 */
int mxb_log_enabled_priorities = (1 << LOG_ERR) | (1 << LOG_NOTICE) | (1 << LOG_WARNING);

// Number of chars needed to represent a number.
#define CALCLEN(i) ((size_t)(floor(log10(abs((int64_t)i))) + 1))
#define UINTLEN(i) (i < 10 ? 1 : (i < 100 ? 2 : (i < 1000 ? 3 : CALCLEN(i))))

namespace
{

int DEFAULT_LOG_AUGMENTATION = 0;

// A message that is logged 10 times in 1 second will be suppressed for 10 seconds.
static MXB_LOG_THROTTLING DEFAULT_LOG_THROTTLING = {10, 1000, 10000};

// BUFSIZ comes from the system. It equals with block size or its multiplication.
const int MAX_LOGSTRLEN = BUFSIZ;

// Current monotonic raw time in milliseconds.
uint64_t time_monotonic_ms()
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);

    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static const int TIMESTAMP_LENGTH = mxb::format_timestamp(timeval {}, false).size();
static const int TIMESTAMP_LENGTH_HP = mxb::format_timestamp(timeval {}, true).size();

const std::string_view PREFIX_EMERG = "emerg  : ";
const std::string_view PREFIX_ALERT = "alert  : ";
const std::string_view PREFIX_CRIT = "crit   : ";
const std::string_view PREFIX_ERROR = "error  : ";
const std::string_view PREFIX_WARNING = "warning: ";
const std::string_view PREFIX_NOTICE = "notice : ";
const std::string_view PREFIX_INFO = "info   : ";
const std::string_view PREFIX_DEBUG = "debug  : ";

std::string_view level_to_prefix(int level)
{
    assert((level & ~LOG_PRIMASK) == 0);

    switch (level)
    {
    case LOG_EMERG:
        return PREFIX_EMERG;

    case LOG_ALERT:
        return PREFIX_ALERT;

    case LOG_CRIT:
        return PREFIX_CRIT;

    case LOG_ERR:
        return PREFIX_ERROR;

    case LOG_WARNING:
        return PREFIX_WARNING;

    case LOG_NOTICE:
        return PREFIX_NOTICE;

    case LOG_INFO:
        return PREFIX_INFO;

    case LOG_DEBUG:
        return PREFIX_DEBUG;

    default:
        assert(!true);
        return PREFIX_ERROR;
    }
}

enum message_suppression_t
{
    MESSAGE_NOT_SUPPRESSED,     // Message is not suppressed.
    MESSAGE_SUPPRESSED,         // Message is suppressed for the first time (for this round)
    MESSAGE_STILL_SUPPRESSED,   // Message is still suppressed (for this round)
    MESSAGE_UNSUPPRESSED,       // Message was suppressed but the suppression is now over
};

class MessageRegistryKey
{
public:
    const char* filename;
    const int   linenumber;

    /**
     * @brief Constructs a message stats key
     *
     * @param filename    The filename where the message was reported. Must be a
     *                    statically allocated buffer, e.g. __FILE__.
     * @param linenumber  The linenumber where the message was reported.
     */
    MessageRegistryKey(const char* filename, int linenumber)
        : filename(filename)
        , linenumber(linenumber)
    {
    }

    bool eq(const MessageRegistryKey& other) const
    {
        return filename == other.filename   // Yes, we compare the pointer values and not the strings.
               && linenumber == other.linenumber;
    }

    size_t hash() const
    {
        /*
         * This is an implementation of the Jenkin's one-at-a-time hash function.
         * https://en.wikipedia.org/wiki/Jenkins_hash_function
         */
        uint64_t key1 = (uint64_t)filename;
        uint16_t key2 = (uint16_t)linenumber;   // The first 48 bits are likely to be 0.

        uint32_t hash_value = 0;
        size_t i;

        for (i = 0; i < sizeof(key1); ++i)
        {
            hash_value += (key1 >> i * 8) & 0xff;
            hash_value += (hash_value << 10);
            hash_value ^= (hash_value >> 6);
        }

        for (i = 0; i < sizeof(key2); ++i)
        {
            hash_value += (key2 >> i * 8) & 0xff;
            hash_value += (hash_value << 10);
            hash_value ^= (hash_value >> 6);
        }

        hash_value += (hash_value << 3);
        hash_value ^= (hash_value >> 11);
        hash_value += (hash_value << 15);
        return hash_value;
    }
};

class MessageRegistryStats
{
public:
    MessageRegistryStats()
        : m_first_ms(time_monotonic_ms())
        , m_last_ms(0)
        , m_count(0)
    {
    }

    std::pair<message_suppression_t, size_t> update_suppression(const MXB_LOG_THROTTLING& t)
    {
        message_suppression_t rv = MESSAGE_NOT_SUPPRESSED;

        std::lock_guard<std::mutex> guard(m_lock);
        uint64_t now_ms = time_monotonic_ms();

        size_t old_count = m_count - t.count;
        ++m_count;

        // Less that t.window_ms milliseconds since the message was logged
        // the last time. May have to be throttled.
        if (m_count < t.count)
        {
            // t.count times has not been reached, still ok to log.
        }
        else if (m_count == t.count)
        {
            mxb_assert(now_ms >= m_first_ms);
            // t.count times has been reached. Was it within the window?
            if (now_ms - m_first_ms < t.window_ms)
            {
                // Within the window, suppress the message.
                rv = MESSAGE_SUPPRESSED;
            }
            else
            {
                // Not within the window, reset the situation.

                // The flooding situation is analyzed window by window.
                // That means that if there in each of two consequtive
                // windows are not enough messages for throttling to take
                // effect, but there would be if the window was placed at a
                // slightly different position (e.g. starting in the middle
                // of the first and ending in the middle of the second) it
                // will go undetected and no throttling will be made.
                // However, if that's the case, it was a spike so the
                // flooding will stop anyway.

                m_first_ms = now_ms;
                m_count = 1;
            }
        }
        else
        {
            mxb_assert(now_ms >= m_first_ms);
            // In suppression mode.
            if (now_ms - m_first_ms < (t.window_ms + t.suppress_ms))
            {
                // Still in the suppression window.
                rv = MESSAGE_STILL_SUPPRESSED;

                if (now_ms - m_first_ms < t.window_ms)
                {
                    // Still within the trigger window, reset the timer.
                    m_first_ms = now_ms;
                }
            }
            else
            {
                // We have exited the suppression window, reset the situation.
                m_first_ms = now_ms;
                m_count = 1;
                rv = MESSAGE_UNSUPPRESSED;
            }
        }

        m_last_ms = now_ms;

        return {rv, old_count};
    }

private:
    std::mutex m_lock;
    uint64_t   m_first_ms;  /** The time when the error was logged the first time in this window. */
    uint64_t   m_last_ms;   /** The time when the error was logged the last time. */
    size_t     m_count;     /** How many times the error has been reported within this window. */
};
}

namespace std
{

template<>
struct hash<MessageRegistryKey>
{
    typedef MessageRegistryKey Key;
    typedef size_t             result_type;

    size_t operator()(const MessageRegistryKey& key) const
    {
        return key.hash();
    }
};

template<>
struct equal_to<MessageRegistryKey>
{
    typedef bool               result_type;
    typedef MessageRegistryKey first_argument_type;
    typedef MessageRegistryKey second_argument_type;

    bool operator()(const MessageRegistryKey& lhs, const MessageRegistryKey& rhs) const
    {
        return lhs.eq(rhs);
    }
};
}

namespace
{

class MessageRegistry;

static bool return_false(int)
{
    return false;
}

struct this_unit
{
    using SLogger = std::unique_ptr<mxb::Logger>;
    using SMessageRegistry = std::unique_ptr<MessageRegistry>;

    int                        augmentation {DEFAULT_LOG_AUGMENTATION};
    bool                       do_highprecision {false};
    bool                       do_syslog {true};
    bool                       do_maxlog {true};
    bool                       redirect_stdout {false};
    bool                       session_trace {false};
    MXB_LOG_THROTTLING         throttling {DEFAULT_LOG_THROTTLING};
    SLogger                    sLogger;
    SMessageRegistry           sMessage_registry;
    mxb_log_context_provider_t context_provider {nullptr};
    mxb_in_memory_log_t        in_memory_log {nullptr};
    const char*                syslog_identifier {nullptr};
    mxb_should_log_t           should_log {return_false};
} this_unit;

inline bool should_level_be_logged(int level)
{
    return mxb_log_is_priority_enabled(level) || this_unit.should_log(level);
}

inline bool is_session_tracing()
{
    return this_unit.session_trace && this_unit.in_memory_log;
}

class MessageRegistry
{
public:
    typedef MessageRegistryKey   Key;
    typedef MessageRegistryStats Stats;

    MessageRegistry(const MessageRegistry&) = delete;
    MessageRegistry& operator=(const MessageRegistry&) = delete;

    MessageRegistry()
    {
    }

    Stats& get_stats(const Key& key)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        return m_registry[key];
    }

    std::pair<message_suppression_t, size_t> get_status(const char* file, int line)
    {
        std::pair<message_suppression_t, size_t> rv = {MESSAGE_NOT_SUPPRESSED, 0};

        // Copy the config to prevent the values from changing while we are using
        // them. It does not matter if they are changed just when we are copying
        // them, but we want to use one set of values throughout the function.
        MXB_LOG_THROTTLING t = this_unit.throttling;

        if ((t.count != 0) && (t.window_ms != 0) && (t.suppress_ms != 0))
        {
            MessageRegistry::Key key(file, line);
            MessageRegistry::Stats& stats = this_unit.sMessage_registry->get_stats(key);

            rv = stats.update_suppression(t);
        }

        return rv;
    }

    void clear()
    {
        std::lock_guard<std::mutex> guard(m_lock);
        return m_registry.clear();
    }

private:
    std::mutex                     m_lock;
    std::unordered_map<Key, Stats> m_registry;
};
}

bool mxb_log_init(const char* ident,
                  const char* logdir,
                  const char* filename,
                  mxb_log_target_t target,
                  mxb_log_context_provider_t context_provider,
                  mxb_in_memory_log_t in_memory_log,
                  mxb_should_log_t should_log)
{
    assert(!mxb_log_inited());

    mxb_assert_message(!this_unit.session_trace || in_memory_log,
                       "If session tracing has already been enabled, then in_memory_log "
                       "must be provided.");

    // Tests mainly pass a NULL logdir with MXB_LOG_TARGET_STDOUT but using
    // /dev/null as the default allows total suppression of logging
    std::string filepath = "/dev/null";

    if (logdir)
    {
        std::string suffix;

        if (!filename)
        {
#ifdef __GNUC__
            suffix = program_invocation_short_name;
#else
            suffix = "messages";
#endif
            suffix += ".log";
        }
        else
        {
            suffix = filename;
        }

        filepath = std::string(logdir) + "/" + suffix;
    }

    if (!ident)
    {
#ifdef __GNUC__
        ident = program_invocation_short_name;
#else
        ident = "mxb_log";      // Better than nothing
#endif
    }

    this_unit.sMessage_registry.reset(new(std::nothrow) MessageRegistry);

    switch (target)
    {
    case MXB_LOG_TARGET_FS:
    case MXB_LOG_TARGET_DEFAULT:
        this_unit.sLogger = mxb::FileLogger::create(filepath);

        if (this_unit.sLogger && this_unit.redirect_stdout)
        {
            // Redirect stdout and stderr to the log file
            FILE* unused __attribute__ ((unused));
            unused = freopen(this_unit.sLogger->filename(), "a", stdout);
            unused = freopen(this_unit.sLogger->filename(), "a", stderr);
        }
        break;

    case MXB_LOG_TARGET_STDOUT:
        this_unit.sLogger = mxb::FDLogger::create(filepath, STDOUT_FILENO);
        break;

    case MXB_LOG_TARGET_STDERR:
        this_unit.sLogger = mxb::FDLogger::create(filepath, STDERR_FILENO);
        break;

    default:
        assert(!true);
        break;
    }

    if (this_unit.sLogger && this_unit.sMessage_registry)
    {
        this_unit.context_provider = context_provider;
        this_unit.in_memory_log = in_memory_log;
        this_unit.syslog_identifier = ident;

        if (should_log)
        {
            this_unit.should_log = should_log;
        }

        openlog(ident, LOG_PID | LOG_ODELAY, LOG_USER);
    }
    else
    {
        this_unit.sLogger.reset();
        this_unit.sMessage_registry.reset();
    }

    return this_unit.sLogger && this_unit.sMessage_registry;
}

void mxb_log_finish(void)
{
    assert(this_unit.sLogger && this_unit.sMessage_registry);

    closelog();
    this_unit.sLogger.reset();
    this_unit.sMessage_registry.reset();
    this_unit.context_provider = nullptr;
}

bool mxb_log_inited()
{
    return this_unit.sLogger && this_unit.sMessage_registry;
}

void mxb_log_set_augmentation(int bits)
{
    this_unit.augmentation = bits & MXB_LOG_AUGMENTATION_MASK;
}

void mxb_log_set_highprecision_enabled(bool enabled)
{
    this_unit.do_highprecision = enabled;

    MXB_NOTICE("highprecision logging is %s.", enabled ? "enabled" : "disabled");
}

bool mxb_log_is_highprecision_enabled()
{
    return this_unit.do_highprecision;
}

void mxb_log_set_syslog_enabled(bool enabled)
{
    this_unit.do_syslog = enabled;
}

bool mxb_log_is_syslog_enabled()
{
    return this_unit.do_syslog;
}

void mxb_log_set_maxlog_enabled(bool enabled)
{
    this_unit.do_maxlog = enabled;
}

bool mxb_log_is_maxlog_enabled()
{
    return this_unit.do_maxlog;
}

void mxb_log_set_throttling(const MXB_LOG_THROTTLING* throttling)
{
    // No locking; it does not have any real impact, even if the struct
    // is used right when its values are modified.
    this_unit.throttling = *throttling;

    if ((this_unit.throttling.count == 0)
        || (this_unit.throttling.window_ms == 0)
        || (this_unit.throttling.suppress_ms == 0))
    {
        MXB_NOTICE("Log throttling has been disabled.");
    }
    else
    {
        MXB_NOTICE("A message that is logged %lu times in %lu milliseconds, "
                   "will be suppressed for %lu milliseconds.",
                   this_unit.throttling.count,
                   this_unit.throttling.window_ms,
                   this_unit.throttling.suppress_ms);
    }
}

void mxb_log_reset_suppression()
{
    this_unit.sMessage_registry->clear();
}

void mxb_log_get_throttling(MXB_LOG_THROTTLING* throttling)
{
    // No locking; this is used only from maxadmin and an inconsistent set
    // may be returned only if mxb_log_set_throttling() is called via an
    // other instance of maxadmin at the very same moment.
    *throttling = this_unit.throttling;
}

void mxb_log_redirect_stdout(bool redirect)
{
    this_unit.redirect_stdout = redirect;
}

void mxb_log_set_session_trace(bool enabled)
{
    // It's always fine if session tracing is disabled or if the log
    // has not yet been inited. But if the session tracing is enabled and
    // the log has been inited, then an in_memory_log function *must*
    // have been provided when the log was inited.
    mxb_assert(!enabled || !mxb_log_inited() || this_unit.in_memory_log);
    this_unit.session_trace = enabled;
}

bool mxb_log_get_session_trace()
{
    return this_unit.session_trace;
}

bool mxb_log_should_log(int priority)
{
    return mxb_log_is_priority_enabled(priority)
           || this_unit.should_log(priority)
           || mxb_log_get_session_trace();
}

bool mxb_log_rotate()
{
    bool rval = this_unit.sLogger->rotate();
    this_unit.sMessage_registry->clear();

    if (this_unit.redirect_stdout && rval)
    {
        // Redirect stdout and stderr to the log file
        FILE* unused __attribute__ ((unused));
        unused = freopen(this_unit.sLogger->filename(), "a", stdout);
        unused = freopen(this_unit.sLogger->filename(), "a", stderr);
    }

    if (rval)
    {
        MXB_NOTICE("Log rotation complete");
    }

    return rval;
}

const char* mxb_log_get_filename()
{
    return this_unit.sLogger->filename();
}

static const char* level_to_string(int level)
{
    switch (level)
    {
    case LOG_EMERG:
        return "emergency";

    case LOG_ALERT:
        return "alert";

    case LOG_CRIT:
        return "critical";

    case LOG_ERR:
        return "error";

    case LOG_WARNING:
        return "warning";

    case LOG_NOTICE:
        return "notice";

    case LOG_INFO:
        return "info";

    case LOG_DEBUG:
        return "debug";

    default:
        assert(!true);
        return "unknown";
    }
}

const char* mxb_log_level_to_string(int level)
{
    return level_to_string(level);
}

bool mxb_log_set_priority_enabled(int level, bool enable)
{
    bool rv = false;
    const char* text = (enable ? "enable" : "disable");

    if ((level & ~LOG_PRIMASK) == 0)
    {
        int bit = (1 << level);

        if (enable)
        {
            mxb_log_enabled_priorities |= bit;
        }
        else
        {
            mxb_log_enabled_priorities &= ~bit;
        }

        MXB_NOTICE("The logging of %s messages has been %sd.", level_to_string(level), text);
        rv = true;
    }
    else
    {
        MXB_ERROR("Attempt to %s unknown syslog priority %d.", text, level);
    }

    return rv;
}

namespace
{

int log_message(message_suppression_t status,
                size_t msg_count,
                int level,
                int priority,
                const char* zModname,
                const char* zFunction,
                std::string_view message)
{
    int err = 0;

    // The log format looks as follows:
    //
    // timestamp   prefix : [(context) ][\[module\] ][(scope); ][(augmentation): ]message[suppression]
    //
    // where
    //   timestamp   :  The timestamp when the message was logged.
    //   prefix      :  debug, info, warning, etc.
    //   context     :  In practice, the session id if it is known.
    //   module      :  The module logging; in practice the current value of MXB_MODULE_NAME.
    //   scope       :  The scope of the message; explicitly set in code using LogScope.
    //   augmentation:  The function where the message was logged.
    //   message     :  The actual message.
    //   suppression :  If this particular message will henceforth be suppressed, a note about that.

    // timestamp
    struct timeval now;
    gettimeofday(&now, NULL);
    bool highprecision = this_unit.do_highprecision;
    int nTimestamp = highprecision ? TIMESTAMP_LENGTH_HP : TIMESTAMP_LENGTH;

    // prefix
    std::string_view prefix = level_to_prefix(level);
    int nPrefix = prefix.length();

    // context
    char context[32];   // The documentation will guarantee a buffer of at least 32 bytes.
    int nContext = 0;

    if (this_unit.context_provider)
    {
        nContext = this_unit.context_provider(context, sizeof(context));

        if (nContext != 0)
        {
            nContext += 3;      // +3 due to "(...) "
        }
    }

    // module
    int nModname = zModname ? strlen(zModname) + 3 : 0;     // +3 due to "[...] "

    // scope
    // If we know the actual object name, add that also
    auto zScope = mxb::LogScope::current_scope();
    int nScope = zScope ? strlen(zScope) + 4 : 0;       // +4 due to "(...); "

    // augmentation
    static const char AUGMENTATION_FORMAT[] = "(%s): ";
    // Other thread might change this_unit.augmentation.
    int augmentation = this_unit.augmentation;
    int nAugmentation = 0;

    switch (augmentation)
    {
    case MXB_LOG_AUGMENT_WITH_FUNCTION:
        nAugmentation = sizeof(AUGMENTATION_FORMAT) - 1;// Remove trailing 0
        nAugmentation -= 2;                             // Remove the %s
        nAugmentation += strlen(zFunction);
        break;

    default:
        break;
    }

    // message
    std::string streamlined_message;    // I.e. no newlines.
    bool not_debug = true;
    MXB_AT_DEBUG(not_debug = LOG_PRI(priority) != LOG_DEBUG);

    auto i = message.find('\n');
    if (i != std::string_view::npos && not_debug)
    {
        streamlined_message = message;

        do
        {
            streamlined_message.replace(i, 1, "\\n");
            i = streamlined_message.find('\n', i + 2);
        }
        while (i != std::string::npos);

        message = streamlined_message;
    }

    int nMessage = message.length();

    // suppression
    static const char SUPPRESSION_FORMAT[] =
        " (subsequent similar messages suppressed for %lu milliseconds)";
    int nSuppression = 0;
    size_t suppress_ms = this_unit.throttling.suppress_ms;
    static const char UNSUPPRESSION_FORMAT[] =
        " (%lu similar messages were previously suppressed)";

    if (status == MESSAGE_SUPPRESSED)
    {
        nSuppression += sizeof(SUPPRESSION_FORMAT) - 1; // Remove trailing NULL
        nSuppression -= 3;                              // Remove the %lu
        nSuppression += UINTLEN(suppress_ms);
    }
    else if (status == MESSAGE_UNSUPPRESSED)
    {
        nSuppression += sizeof(UNSUPPRESSION_FORMAT) - 1;   // Remove trailing NULL
        nSuppression -= 3;                                  // Remove the %lu
        nSuppression += UINTLEN(msg_count);
    }

    // All set, now the final message can be constructed.

    int nLog_line = 0;
    nLog_line += nTimestamp;
    nLog_line += nPrefix;
    nLog_line += nContext;
    nLog_line += nModname;
    nLog_line += nScope;
    nLog_line += nAugmentation;
    nLog_line += nMessage;
    nLog_line += nSuppression;

    if (nLog_line > MAX_LOGSTRLEN)
    {
        nMessage -= (nLog_line - MAX_LOGSTRLEN);
        nLog_line = MAX_LOGSTRLEN;

        assert(nTimestamp + nPrefix + nContext + nModname + nScope
               + nAugmentation + nMessage + nSuppression == nLog_line);
    }

    char log_line[nLog_line + 2];   // +2 for the '\n' that will be added and the final 0.

    // NOTE: All of these point into the same buffer, which will have a single NULL at the end.
    // NOTE: Thus, if printed without the length specified explicitly, not just that particular
    // NOTE: item will be printed, but all subsequent ones as well.
    char* pTimestamp = log_line;
    char* pPrefix = pTimestamp + nTimestamp;
    char* pContext = pPrefix + nPrefix;
    char* pModname = pContext + nContext;
    char* pScope = pModname + nModname;
    char* pAugmentation = pScope + nScope;
    char* pMessage = pAugmentation + nAugmentation;
    char* pSuppression = pMessage + nMessage;

    memcpy(pPrefix, prefix.data(), nPrefix);

    if (nContext)
    {
        strcpy(pContext, "(");
        strcat(pContext, context);
        strcat(pContext, ") ");
    }

    if (nModname)
    {
        strcpy(pModname, "[");
        strcat(pModname, zModname);
        strcat(pModname, "] ");
    }

    if (nScope)
    {
        strcpy(pScope, "(");
        strcat(pScope, zScope);
        strcat(pScope, "); ");
    }

    if (nAugmentation)
    {
        int len = 0;

        switch (augmentation)
        {
        case MXB_LOG_AUGMENT_WITH_FUNCTION:
            len = sprintf(pAugmentation, AUGMENTATION_FORMAT, zFunction);
            break;

        default:
            assert(!true);
        }

        (void)len;
        assert(len == nAugmentation);
    }

    memcpy(pMessage, message.data(), nMessage);
    pMessage[nMessage] = 0;

    if (status == MESSAGE_SUPPRESSED)
    {
        sprintf(pSuppression, SUPPRESSION_FORMAT, suppress_ms);
    }
    else if (status == MESSAGE_UNSUPPRESSED)
    {
        sprintf(pSuppression, UNSUPPRESSION_FORMAT, msg_count);
    }

    // Add a final newline.
    char* end = log_line + nLog_line;

    *end = '\n';
    *(end + 1) = 0;

    if (is_session_tracing())
    {
        this_unit.in_memory_log(now, {pPrefix, (std::string_view::size_type)nLog_line - nTimestamp});
    }

    if (should_level_be_logged(level))
    {
        // Converting the raw timestamp value into the formatted local time string is expensive. The
        // __tz_convert function that localtime_r ends up calling has a global mutex which introduces a
        // bottleneck for scaling. Delaying the timestamp string generation to this point makes it possible
        // for the in-memory logging to avoid this cost.
        std::string timestamp = mxb::format_timestamp(now, highprecision);
        memcpy(pTimestamp, timestamp.c_str(), timestamp.size());

        // Debug messages are never logged into syslog
        if (this_unit.do_syslog && LOG_PRI(priority) != LOG_DEBUG)
        {
#ifdef HAVE_SYSTEMD
            sd_journal_send("MESSAGE=%s", pMessage,
                            "PRIORITY=%d", LOG_PRI(priority),
                            "SESSION=%s", nContext ? context : "",
                            "MODULE=%s", nModname ? zModname : "",
                            "OBJECT=%s", nScope ? zScope : "",
                            "TIMESTAMP=%s", timestamp.c_str(),
                            "SYSLOG_IDENTIFIER=%s", this_unit.syslog_identifier,
                            LOG_FAC(priority) ? "SYSLOG_FACILITY=%d" : nullptr, LOG_FAC(priority),
                            nullptr);
#else
            // pContext does not only include the context, but the context and
            // everything that follows.
            syslog(priority, "%s", pContext);
#endif
        }

        err = this_unit.sLogger->write(log_line, nLog_line + 1) ? 0 : -1;
    }
    else
    {
        err = 0;
    }

    return err;
}
}

int mxb_log_message(int priority,
                    const char* modname,
                    const char* file,
                    int line,
                    const char* function,
                    const char* format,
                    ...)
{
    int err = 0;

    // The following will leave a stacktrace in the log in case something tries to log a message when it is
    // not possible. Otherwise we'll just get a single line message about this particular assertion failing
    // and nothing else.
#ifdef SS_DEBUG
    if (!this_unit.sLogger || !this_unit.sMessage_registry)
    {
        mxb::emergency_stacktrace();
    }
#endif

    assert(this_unit.sLogger && this_unit.sMessage_registry);
    assert((priority & ~(LOG_PRIMASK | LOG_FACMASK)) == 0);

    int level = priority & LOG_PRIMASK;

    if ((priority & ~(LOG_PRIMASK | LOG_FACMASK)) == 0)     // Check that the priority is ok,
    {
        message_suppression_t status = MESSAGE_NOT_SUPPRESSED;
        size_t msg_count = 0;

        // We only throttle errors and warnings. Info and debug messages
        // are never on during normal operation, so if they are enabled,
        // we are presumably debugging something. Notice messages are
        // assumed to be logged for a reason and always in a context where
        // flooding cannot be caused. If log_info is enabled, the throttling
        // is disabled as it would cause messages to be lost that bring context
        // to other messages.
        if (!mxb_log_is_priority_enabled(LOG_INFO)
            && (level == LOG_ERR || level == LOG_WARNING))
        {
            std::tie(status, msg_count) = this_unit.sMessage_registry->get_status(file, line);
        }

        if (status != MESSAGE_STILL_SUPPRESSED)
        {
            char message[MAX_LOGSTRLEN + 1];

            va_list valist;
            va_start(valist, format);
            int nMessage = vsnprintf(message, sizeof(message), format, valist);
            va_end(valist);

            if (nMessage >= 0)
            {
                // If the string got truncated, the return value from vsnprintf is the size that would've been
                // printed if there was enough space.
                nMessage = std::min(nMessage, (int)sizeof(message) - 1);

                // If there is redirection and the redirectee handles the message,
                // the regular logging is bypassed.
                bool redirected = false;

                if (auto redirect = mxb::LogRedirect::current_redirect())
                {
                    redirected = redirect(level, std::string_view(message, nMessage));
                }

                if ((!redirected && should_level_be_logged(level)) || is_session_tracing())
                {
                    err = log_message(status, msg_count, level,
                                      priority, modname, function,
                                      std::string_view(message, nMessage));
                }
            }
        }
    }
    else
    {
        MXB_WARNING("Invalid syslog priority: %d", priority);
    }

    return err;
}

int mxb_log_fatal_error(const char* message)
{
    return this_unit.sLogger->write(message, strlen(message)) ? 0 : -1;
}

namespace maxbase
{
thread_local LogScope* LogScope::s_current_scope {nullptr};
thread_local LogRedirect::Func LogRedirect::s_redirect {nullptr};

LogRedirect::LogRedirect(Func func)
{
    mxb_assert(s_redirect == nullptr);
    s_redirect = func;
}

LogRedirect::~LogRedirect()
{
    s_redirect = nullptr;
}

// static
LogRedirect::Func LogRedirect::current_redirect()
{
    return s_redirect;
}

std::string format_timestamp(const struct timeval& tv, bool highprecision)
{
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    char buf[100];
    MXB_AT_DEBUG(int rc);

    if (highprecision)
    {
        int usec = tv.tv_usec / 1000;
        MXB_AT_DEBUG(rc = ) snprintf(buf, sizeof(buf),
                                     "%04d-%02d-%02d %02d:%02d:%02d.%03d   ",
                                     tm.tm_year + 1900,
                                     tm.tm_mon + 1,
                                     tm.tm_mday,
                                     tm.tm_hour,
                                     tm.tm_min,
                                     tm.tm_sec,
                                     usec);
    }
    else
    {
        MXB_AT_DEBUG(rc = ) snprintf(buf, sizeof(buf),
                                     "%04d-%02d-%02d %02d:%02d:%02d   ",
                                     tm.tm_year + 1900,
                                     tm.tm_mon + 1,
                                     tm.tm_mday,
                                     tm.tm_hour,
                                     tm.tm_min,
                                     tm.tm_sec);
    }

    mxb_assert(rc < (int)sizeof(buf) && rc > 0);

    return buf;
}
}
