/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file gateway.c - The entry point of MaxScale
 */

#include <maxscale/ccdefs.hh>

#ifdef HAVE_GLIBC
#include <execinfo.h>
#endif
#include <ftw.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <getopt.h>
#ifdef HAVE_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <set>
#include <map>
#include <fstream>

#include <openssl/opensslconf.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/sysinfo.h>

#include <maxbase/maxbase.hh>
#include <maxbase/ini.hh>
#include <maxbase/stacktrace.hh>
#include <maxbase/format.hh>
#include <maxbase/pretty_print.hh>
#include <maxbase/watchdognotifier.hh>
#include <maxsql/mariadb.hh>
#include <maxbase/alloc.h>
#include <maxscale/built_in_modules.hh>
#include <maxscale/dcb.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/maxscale.h>
#include <maxscale/mysql_utils.hh>
#include <maxscale/paths.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/server.hh>
#include <maxscale/sqlite3.h>
#include <maxscale/threadpool.hh>
#include <maxscale/utils.h>
#include <maxscale/version.h>

#include "internal/admin.hh"
#include "internal/adminusers.hh"
#include "internal/config.hh"
#include "internal/http_sql.hh"
#include "internal/maxscale.hh"
#include "internal/modules.hh"
#include "internal/monitormanager.hh"
#include "internal/service.hh"
#include "internal/listener.hh"
#include "internal/secrets.hh"
#include "internal/servermanager.hh"
#include "internal/configmanager.hh"

#if !defined (OPENSSL_THREADS)
#error OpenSSL library does not support multi-threading.
#endif

using namespace maxscale;
using std::string;

const int PIDFD_CLOSED = -1;

extern char* program_invocation_name;
extern char* program_invocation_short_name;

static struct ThisUnit
{
    char datadir[PATH_MAX + 1] = "";/* Data directory created for this gateway instance */
    bool datadir_defined = false;   /*< If the datadir was already set */
    char pidfile[PATH_MAX + 1] = "";
    int  pidfd = PIDFD_CLOSED;

    std::map<std::string, int> directory_locks;
    bool                       daemon_mode = true;
    bool                       syslog_configured = false;
    bool                       maxlog_configured = false;
    volatile sig_atomic_t      last_signal = 0;
    bool                       unload_modules_at_exit = true;
    std::string                redirect_output_to;
#ifndef OPENSSL_1_1
    /** SSL multi-threading functions and structures */
    pthread_mutex_t* ssl_locks = nullptr;
#endif
} this_unit;

static const char* maxscale_commit = MAXSCALE_COMMIT;

#ifdef HAVE_GLIBC
// getopt_long is a GNU extension
static struct option long_options[] =
{
    {"config-check",        no_argument,       0, 'c'},
    {"export-config",       required_argument, 0, 'e'},
    {"daemon",              no_argument,       0, 'n'},
    {"nodaemon",            no_argument,       0, 'd'},
    {"config",              required_argument, 0, 'f'},
    {"log",                 required_argument, 0, 'l'},
    {"logdir",              required_argument, 0, 'L'},
    {"cachedir",            required_argument, 0, 'A'},
    {"libdir",              required_argument, 0, 'B'},
    {"configdir",           required_argument, 0, 'C'},
    {"datadir",             required_argument, 0, 'D'},
    {"execdir",             required_argument, 0, 'E'},
    {"persistdir",          required_argument, 0, 'F'},
    {"sharedir",            required_argument, 0, 'J'},
    {"module_configdir",    required_argument, 0, 'M'},
    {"language",            required_argument, 0, 'N'},
    {"piddir",              required_argument, 0, 'P'},
    {"basedir",             required_argument, 0, 'R'},
    {"runtimedir",          required_argument, 0, 'r'},
    {"user",                required_argument, 0, 'U'},
    {"syslog",              required_argument, 0, 's'},
    {"maxlog",              required_argument, 0, 'S'},
    {"log_augmentation",    required_argument, 0, 'G'},
    {"version",             no_argument,       0, 'v'},
    {"version-full",        no_argument,       0, 'V'},
    {"help",                no_argument,       0, '?'},
    {"connector_plugindir", required_argument, 0, 'H'},
    {"passive",             no_argument,       0, 'p'},
    {"debug",               required_argument, 0, 'g'},
    {0,                     0,                 0, 0  }
};
#endif

static int  write_pid_file();   /* write MaxScale pidfile */
static bool lock_dir(const std::string& path);
static bool lock_directories();
static void unlock_directories();
static void unlink_pidfile(void);   /* remove pidfile */
static void unlock_pidfile();
static int  ntfw_cb(const char*, const struct stat*, int, struct FTW*);
static bool handle_path_arg(std::string* dest, const char* path, const char* arg);
static bool handle_debug_args(char* args);
static void set_log_augmentation(const char* value);
static void usage(void);
static void print_alert(int eno, const char* format, ...) mxb_attribute((format(printf, 2, 3)));
static void print_alert(const char* format, ...) mxb_attribute((format(printf, 1, 2)));
static void print_info(int eno, const char* format, ...) mxb_attribute((format(printf, 2, 3)));
static void print_info(const char* format, ...) mxb_attribute((format(printf, 1, 2)));
static void print_warning(int eno, const char* format, ...) mxb_attribute((format(printf, 2, 3)));
static void print_warning(const char* format, ...) mxb_attribute((format(printf, 1, 2)));
static void log_startup_error(int eno, const char* format, ...) mxb_attribute((format(printf, 2, 3)));
static void log_startup_error(const char* format, ...) mxb_attribute((format(printf, 1, 2)));
bool        check_paths();
static int  set_user(const char* user);
bool        pid_file_exists();
void        write_child_exit_code(int fd, int code);
static bool change_cwd();
static void log_exit_status();
static int  daemonize();
static void disable_module_unloading(const char* arg);
static void enable_module_unloading(const char* arg);
static void enable_statement_logging(const char* arg);
static void disable_statement_logging(const char* arg);
static void enable_cors(const char* arg);
static void allow_duplicate_servers(const char* arg);
static void redirect_output_to_file(const char* arg);
static bool user_is_acceptable(const char* specified_user);
static bool init_sqlite3();
static bool init_base_libraries();
static void finish_base_libraries();
static bool redirect_stdout_and_stderr(const std::string& path);
static bool is_maxscale_already_running();

namespace
{
// The default configuration file name
const char default_cnf_fname[] = "maxscale.cnf";

string get_absolute_fname(const string& relative_path, const char* fname);
bool   is_file_and_readable(const string& absolute_pathname);
bool   path_is_readable(const string& absolute_pathname);

struct SniffResult
{
    bool                                success {false};
    mxb::ini::map_result::Configuration config;
};

SniffResult sniff_configuration(const string& filepath);
string      resolve_maxscale_conf_fname(const string& cnf_file_arg);
}

#define VA_MESSAGE(message, format) \
    va_list ap ## __LINE__; \
    va_start(ap ## __LINE__, format); \
    int len ## __LINE__ = vsnprintf(nullptr, 0, format, ap ## __LINE__); \
    va_end(ap ## __LINE__); \
    char message[len ## __LINE__ + 1]; \
    va_start(ap ## __LINE__, format); \
    vsnprintf(message, sizeof(message), format, ap ## __LINE__); \
    va_end(ap ## __LINE__);

struct DEBUG_ARGUMENT
{
    const char* name;                       /**< The name of the debug argument */
    void        (* action)(const char* arg);/**< The function implementing the argument */
    const char* description;                /**< Help text */
};

#define SPACER "                              "

const DEBUG_ARGUMENT debug_arguments[] =
{
    {
        "disable-module-unloading", disable_module_unloading,
        "disable module unloading at exit. Will produce better\n"
        SPACER "Valgrind leak reports if leaked memory was allocated in\n"
        SPACER "a shared library"
    },
    {
        "enable-module-unloading", enable_module_unloading,
        "cancels disable-module-unloading"
    },
    {
        "redirect-output-to-file", redirect_output_to_file,
        "redirect stdout and stderr to the file given as an argument"
    },
    {
        "enable-statement-logging", enable_statement_logging,
        "enable the logging of monitor and authenticator SQL statements sent by MaxScale to the servers"
    },
    {
        "disable-statement-logging", disable_statement_logging,
        "disable the logging of monitor and authenticator SQL statements sent by MaxScale to the servers"
    },
    {
        "enable-cors", enable_cors,
        "enable CORS support in the REST API"
    },
    {
        "allow-duplicate-servers", allow_duplicate_servers,
        "allow multiple servers to have the same address/port combination"
    },
    {NULL, NULL, NULL}
};

#ifndef OPENSSL_1_1
/** SSL multi-threading functions and structures */

static void ssl_locking_function(int mode, int n, const char* file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        pthread_mutex_lock(&this_unit.ssl_locks[n]);
    }
    else
    {
        pthread_mutex_unlock(&this_unit.ssl_locks[n]);
    }
}
/**
 * OpenSSL requires this struct to be defined in order to use dynamic locks
 */
struct CRYPTO_dynlock_value
{
    pthread_mutex_t lock;
};

/**
 * Create a dynamic OpenSSL lock. The dynamic lock is just a wrapper structure
 * around a SPINLOCK structure.
 * @param file File name
 * @param line Line number
 * @return Pointer to new lock or NULL of an error occurred
 */
static struct CRYPTO_dynlock_value* ssl_create_dynlock(const char* file, int line)
{
    struct CRYPTO_dynlock_value* lock =
        (struct CRYPTO_dynlock_value*) MXS_MALLOC(sizeof(struct CRYPTO_dynlock_value));
    if (lock)
    {
        pthread_mutex_init(&lock->lock, NULL);
    }
    return lock;
}

/**
 * Lock a dynamic lock for OpenSSL.
 * @param mode
 * @param n pointer to lock
 * @param file File name
 * @param line Line number
 */
static void ssl_lock_dynlock(int mode, struct CRYPTO_dynlock_value* n, const char* file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        pthread_mutex_lock(&n->lock);
    }
    else
    {
        pthread_mutex_unlock(&n->lock);
    }
}

/**
 * Free a dynamic OpenSSL lock.
 * @param n Lock to free
 * @param file File name
 * @param line Line number
 */
static void ssl_free_dynlock(struct CRYPTO_dynlock_value* n, const char* file, int line)
{
    MXS_FREE(n);
}

#ifdef OPENSSL_1_0
/**
 * The thread ID callback function for OpenSSL dynamic locks.
 * @param id Id to modify
 */
static void maxscale_ssl_id(CRYPTO_THREADID* id)
{
    CRYPTO_THREADID_set_numeric(id, pthread_self());
}
#endif
#endif

/**
 * Handler for SIGHUP signal.
 */
static void sighup_handler(int i)
{
    // Legacy configuration reload handler
}

/**
 * Handler for SIGUSR1 signal. A SIGUSR1 signal will cause
 * maxscale to rotate all log files.
 */
static void sigusr1_handler(int i)
{
    MXS_NOTICE("Log file flush following reception of SIGUSR1\n");
    mxs_log_rotate();
}

static const char shutdown_msg[] = "\n\nShutting down MaxScale\n\n";
static const char patience_msg[] =
    "\n"
    "Patience is a virtue...\n"
    "Shutdown in progress, but one more Ctrl-C or SIGTERM and MaxScale goes down,\n"
    "no questions asked.\n";

static void sigterm_handler(int i)
{
    this_unit.last_signal = i;
    int n_shutdowns = maxscale_shutdown();

    if (n_shutdowns == 1)
    {
        if (!this_unit.daemon_mode)
        {
            if (write(STDERR_FILENO, shutdown_msg, sizeof(shutdown_msg) - 1) == -1)
            {
                printf("Failed to write shutdown message!\n");
            }
        }
    }
    else
    {
        exit(EXIT_FAILURE);
    }
}

static void sigint_handler(int i)
{
    this_unit.last_signal = i;
    int n_shutdowns = maxscale_shutdown();

    if (n_shutdowns == 1)
    {
        if (!this_unit.daemon_mode)
        {
            if (write(STDERR_FILENO, shutdown_msg, sizeof(shutdown_msg) - 1) == -1)
            {
                printf("Failed to write shutdown message!\n");
            }
        }
    }
    else if (n_shutdowns == 2)
    {
        if (!this_unit.daemon_mode)
        {
            if (write(STDERR_FILENO, patience_msg, sizeof(patience_msg) - 1) == -1)
            {
                printf("Failed to write shutdown message!\n");
            }
        }
    }
    else
    {
        exit(EXIT_FAILURE);
    }
}

volatile sig_atomic_t fatal_handling = 0;

static int signal_set(int sig, void (* handler)(int));

static void sigfatal_handler(int i)
{
    thread_local std::thread::id current_id;
    std::thread::id no_id;

    if (current_id != no_id)
    {
        // Fatal error when processing a fatal error.
        // TODO: This should be overhauled to proper signal handling (MXS-599).
        signal_set(i, SIG_DFL);
        raise(i);
    }

    current_id = std::this_thread::get_id();

    const mxs::Config& cnf = mxs::Config::get();

    print_alert("MaxScale %s received fatal signal %d. "
                "Commit ID: %s System name: %s Release string: %s\n\n",
                MAXSCALE_VERSION, i, maxscale_commit, cnf.sysname.c_str(), cnf.release_string);

    MXS_ALERT("MaxScale %s received fatal signal %d. "
              "Commit ID: %s System name: %s Release string: %s",
              MAXSCALE_VERSION, i, maxscale_commit, cnf.sysname.c_str(), cnf.release_string);

    const char* pStmt;
    size_t nStmt;

    if (!qc_get_current_stmt(&pStmt, &nStmt))
    {
        pStmt = "none/unknown";
        nStmt = strlen(pStmt);
    }

    MXS_ALERT("Statement currently being classified: %.*s", (int)nStmt, pStmt);

    if (DCB* dcb = dcb_get_current())
    {
        auto ses = dcb->session();
        if (ses)
        {
            ses->dump_statements();
            ses->dump_session_log();
            MXS_ALERT("DCB: %p Session: %lu Service: %s",
                      dcb, dcb->session()->id(), dcb->session()->service->name());
        }
    }

    thread_local std::string msg;

    auto cb = [](const char* symbol, const char* cmd) {
            char buf[512];
            snprintf(buf, sizeof(buf), "  %s: %s\n", symbol, cmd);
            msg += buf;
        };

    mxb::dump_stacktrace(cb);

    print_alert("%s\n", msg.c_str());
    MXS_ALERT("\n%s", msg.c_str());

    /* re-raise signal to enforce core dump */
    print_alert("Writing core dump.");
    signal_set(i, SIG_DFL);
    raise(i);
}

/**
 * @node Wraps sigaction calls
 *
 * Parameters:
 * @param sig Signal to set
 * @param void Handler function for signal *
 *
 * @return 0 in success, 1 otherwise
 *
 *
 * @details (write detailed description here)
 *
 */
static int signal_set(int sig, void (* handler)(int))
{
    int rc = 0;

    struct sigaction sigact = {};
    sigact.sa_handler = handler;

    int err;

    do
    {
        errno = 0;
        err = sigaction(sig, &sigact, NULL);
    }
    while (errno == EINTR);

    if (err < 0)
    {
        MXS_ERROR("Failed call sigaction() in %s due to %d, %s.",
                  program_invocation_short_name,
                  errno,
                  mxs_strerror(errno));
        rc = 1;
    }

    return rc;
}

/**
 * @brief Create the data directory for this process
 *
 * This will prevent conflicts when multiple MaxScale instances are run on the
 * same machine.
 * @param base Base datadir path
 * @param datadir The result where the process specific datadir is stored
 * @return True if creation was successful and false on error
 */
static bool create_datadir(const char* base, char* datadir)
{
    bool created = false;
    int len = 0;

    if ((len = snprintf(datadir, PATH_MAX, "%s", base)) < PATH_MAX
        && (mkdir(datadir, 0777) == 0 || errno == EEXIST))
    {
        if ((len = snprintf(datadir, PATH_MAX, "%s/data%d", base, getpid())) < PATH_MAX)
        {
            if ((mkdir(datadir, 0777) == 0) || (errno == EEXIST))
            {
                created = true;
            }
            else
            {
                MXS_ERROR("Cannot create data directory '%s': %s",
                          datadir,
                          mxs_strerror(errno));
            }
        }
    }
    else
    {
        if (len < PATH_MAX)
        {
            MXS_ERROR("Cannot create data directory '%s': %s",
                      datadir,
                      mxs_strerror(errno));
        }
        else
        {
            MXS_ERROR("Data directory pathname exceeds the maximum allowed pathname "
                      "length: %s/data%d.",
                      base,
                      getpid());
        }
    }

    return created;
}

/**
 * Cleanup the temporary data directory we created for the gateway
 */
int ntfw_cb(const char* filename,
            const struct stat* filestat,
            int fileflags,
            struct FTW* pfwt)
{
    int rc = 0;
    int datadir_len = strlen(mxs::datadir());
    std::string filename_string(filename + datadir_len);

    if (strncmp(filename_string.c_str(), "/data", 5) == 0)
    {
        rc = remove(filename);
        if (rc != 0)
        {
            int eno = errno;
            errno = 0;
            MXS_ERROR("Failed to remove the data directory %s of MaxScale due to %d, %s.",
                      filename_string.c_str(),
                      eno,
                      mxs_strerror(eno));
        }
    }
    return rc;
}

/**
 * @brief Clean up the data directory
 *
 * This removes the process specific datadir which is currently only used by
 * the embedded library. In the future this directory could contain other
 * temporary files and relocating this to to, for example, /tmp/ could make sense.
 */
void cleanup_process_datadir()
{
    int depth = 1;
    int flags = FTW_CHDIR | FTW_DEPTH | FTW_MOUNT;
    const char* proc_datadir = mxs::process_datadir();

    if (strcmp(proc_datadir, mxs::datadir()) != 0 && access(proc_datadir, F_OK) == 0)
    {
        nftw(proc_datadir, ntfw_cb, depth, flags);
    }
}

void cleanup_old_process_datadirs()
{
    int depth = 1;
    int flags = FTW_CHDIR | FTW_DEPTH | FTW_MOUNT;
    nftw(mxs::datadir(), ntfw_cb, depth, flags);
}

namespace
{
string resolve_maxscale_conf_fname(const string& cnf_file_arg)
{
    string cnf_full_path;
    if (!cnf_file_arg.empty())
    {
        char resolved_path[PATH_MAX + 1];
        if (realpath(cnf_file_arg.c_str(), resolved_path) == nullptr)
        {
            log_startup_error(errno, "Failed to open read access to configuration file");
        }
        else
        {
            cnf_full_path = resolved_path;
        }
    }
    else
    {
        /*< default config file name is used */
        string home_dir = mxs::configdir();
        if (home_dir.empty() || home_dir.back() != '/')
        {
            home_dir += '/';
        }
        cnf_full_path = get_absolute_fname(home_dir, default_cnf_fname);
    }

    if (!cnf_full_path.empty() && !is_file_and_readable(cnf_full_path))
    {
        cnf_full_path.clear();
    }
    return cnf_full_path;
}
}


/**
 * Check read and write accessibility to a directory.
 * @param dirname       directory to be checked
 *
 * @return NULL if directory can be read and written, an error message if either
 *      read or write is not permitted.
 */
static bool check_dir_access(const char* dirname, bool rd, bool wr)
{
    mxb_assert(dirname);
    std::ostringstream ss;

    if (access(dirname, F_OK) != 0)
    {
        ss << "Can't access '" << dirname << "'.";
    }
    else if (rd && access(dirname, R_OK) != 0)
    {
        ss << "MaxScale doesn't have read permission to '" << dirname << "'.";
    }
    else if (wr && access(dirname, W_OK) != 0)
    {
        ss << "MaxScale doesn't have write permission to '" << dirname << "'.";
    }

    auto err = ss.str();

    if (!err.empty())
    {
        print_alert(errno, "%s", err.c_str());
    }

    return err.empty();
}

static bool init_log()
{
    bool rval = false;
    const mxs::Config& cnf = mxs::Config::get();

    if (!cnf.config_check && !mxs_mkdir_all(mxs::logdir(), 0777, false))
    {
        print_alert(errno, "Cannot create log directory '%s'", mxs::logdir());
    }
    else if (mxs_log_init(NULL, mxs::logdir(), cnf.log_target))
    {
        // Since init_log() may be called more than once, we need to ensure
        // that the cleanup-function is not registered more than once.
        static bool atexit_registered = false;

        if (!atexit_registered)
        {
            atexit(mxs_log_finish);
            atexit_registered = true;
        }
        rval = true;
    }

    return rval;
}

static void print_message(const char* tag, int eno, const char* message)
{
    fprintf(stderr,
            "%s: %s%s%s%s\n",
            tag,
            message,
            eno == 0 ? "" : ": ",
            eno == 0 ? "" : mxs_strerror(eno),
            eno == 0 ? "" : ".");
    fflush(stderr);
}

/**
 * Print message to stderr
 *
 * @param eno      Errno value, ignored if 0.
 * @param message  Message to be printed.
 */
static void print_alert(int eno, const char* format, ...)
{
    VA_MESSAGE(message, format);

    print_message("alert  ", eno, message);
}

static void print_alert(const char* format, ...)
{
    VA_MESSAGE(message, format);

    print_message("alert  ", 0, message);
}

static void print_info(int eno, const char* format, ...)
{
    VA_MESSAGE(message, format);

    print_message("info   ", eno, message);
}

static void print_info(const char* format, ...)
{
    VA_MESSAGE(message, format);

    print_message("info   ", 0, message);
}

static void print_warning(int eno, const char* format, ...)
{
    VA_MESSAGE(message, format);

    print_message("warning", eno, message);
}

static void print_warning(const char* format, ...)
{
    VA_MESSAGE(message, format);

    print_message("warning", 0, message);
}

static void log_startup_message(int eno, const char* message)
{
    if (mxb_log_inited() || init_log())
    {
        MXS_ALERT("%s%s%s%s",
                  message,
                  eno == 0 ? "" : ": ",
                  eno == 0 ? "" : mxs_strerror(eno),
                  eno == 0 ? "" : ".");
    }

    print_alert(eno, "%s", message);
}

/**
 * Log startup error.
 *
 * - If possible, log message as an error to the log.
 * - Always print the message to stdeerr.
 *
 * @param eno     Errno value, ignored if 0.
 * @param format  Printf format string.
 * @param ...     Arguments according to @c format.
 */
static void log_startup_error(int eno, const char* format, ...)
{
    VA_MESSAGE(message, format);

    log_startup_message(eno, message);
}

/**
 * Log startup error.
 *
 * - If possible, log message as an error to the log.
 * - Always print the message to stdeerr.
 *
 * @param format  Printf format string.
 * @param ...     Arguments according to @c format.
 */
static void log_startup_error(const char* format, ...)
{
    VA_MESSAGE(message, format);

    log_startup_message(0, message);
}

namespace
{
/**
 * Check that a path refers to a readable file.
 *
 * @param absolute_pathname The path to check.
 * @return True if the path refers to a readable file. is readable
 */
bool is_file_and_readable(const string& absolute_pathname)
{
    bool rv = false;
    struct stat info {};

    if (stat(absolute_pathname.c_str(), &info) == 0)
    {
        if ((info.st_mode & S_IFMT) == S_IFREG)
        {
            // There is a race here as the file can be deleted and a directory
            // created in its stead between the stat() call here and the access()
            // call in file_is_readable().
            rv = path_is_readable(absolute_pathname);
        }
        else
        {
            log_startup_error("'%s' does not refer to a regular file.", absolute_pathname.c_str());
        }
    }
    else
    {
        log_startup_error(errno, "Could not access '%s'", absolute_pathname.c_str());
    }

    return rv;
}

/**
 * Check if the file or directory is readable
 * @param absolute_pathname Path of the file or directory to check
 * @return True if file is readable
 */
bool path_is_readable(const string& absolute_pathname)
{
    bool succp = true;

    if (access(absolute_pathname.c_str(), R_OK) != 0)
    {
        log_startup_error(errno, "Opening file '%s' for reading failed", absolute_pathname.c_str());
        succp = false;
    }
    return succp;
}


/**
 * Get absolute pathname, given a relative path and a filename.
 *
 * @param relative_path  Relative path.
 * @param fname          File name to be concatenated to the path.
 *
 * @return Absolute path if resulting path exists and the file is
 *         readable, otherwise an empty string.
 */
string get_absolute_fname(const string& relative_path, const char* fname)
{
    mxb_assert(fname);

    string absolute_fname;

    /*<
     * Expand possible relative pathname to absolute path
     */
    char expanded_path[PATH_MAX];
    if (realpath(relative_path.c_str(), expanded_path) == NULL)
    {
        log_startup_error(errno, "Failed to read the directory '%s'.", relative_path.c_str());
    }
    else
    {
        /*<
         * Concatenate an absolute filename and test its existence and
         * readability.
         */

        absolute_fname += expanded_path;
        absolute_fname += "/";
        absolute_fname += fname;

        if (!path_is_readable(absolute_fname))
        {
            absolute_fname.clear();
        }
    }

    return absolute_fname;
}
}

static void usage()
{
    fprintf(stderr,
            "\nUsage : %s [OPTION]...\n\n"
            "  -c, --config-check          validate configuration file and exit\n"
            "  -e, --export-config=FILE    export configuration to a single file\n"
            "  -d, --nodaemon              enable running in terminal process\n"
            "  -f, --config=FILE           relative or absolute pathname of config file\n"
            "  -l, --log=[file|stdout]     log to file or stdout\n"
            "                              (default: file)\n"
            "  -L, --logdir=PATH           path to log file directory\n"
            "  -A, --cachedir=PATH         path to cache directory\n"
            "  -B, --libdir=PATH           path to module directory\n"
            "  -C, --configdir=PATH        path to configuration file directory\n"
            "  -D, --datadir=PATH          path to data directory,\n"
            "                              stores internal MaxScale data\n"
            "  -E, --execdir=PATH          path to the maxscale and other executable files\n"
            "  -F, --persistdir=PATH       path to persisted configuration directory\n"
            "  -M, --module_configdir=PATH path to module configuration directory\n"
            "  -H, --connector_plugindir=PATH\n"
            "                              path to MariaDB Connector-C plugin directory\n"
            "  -J, --sharedir=PATH         path to share directory\n"
            "  -N, --language=PATH         path to errmsg.sys file\n"
            "  -P, --piddir=PATH           path to PID file directory\n"
            "  -R, --basedir=PATH          base path for all other paths\n"
            "  -r  --runtimedir=PATH       base path for all other paths expect binaries\n"
            "  -U, --user=USER             user ID and group ID of specified user are used to\n"
            "                              run MaxScale\n"
            "  -s, --syslog=[yes|no]       log messages to syslog (default:yes)\n"
            "  -S, --maxlog=[yes|no]       log messages to MaxScale log (default: yes)\n"
            "  -G, --log_augmentation=0|1  augment messages with the name of the function\n"
            "                              where the message was logged (default: 0)\n"
            "  -p, --passive               start MaxScale as a passive standby\n"
            "  -g, --debug=arg1,arg2,...   enable or disable debug features. Supported arguments:\n",
            program_invocation_short_name);
    for (int i = 0; debug_arguments[i].action != NULL; i++)
    {
        fprintf(stderr,
                "   %-25s  %s\n",
                debug_arguments[i].name,
                debug_arguments[i].description);
    }
    fprintf(stderr,
            "  -v, --version               print version info and exit\n"
            "  -V, --version-full          print full version info and exit\n"
            "  -?, --help                  show this help\n"
            "\n"
            "Defaults paths:\n"
            "  config file       : %s/%s\n"
            "  configdir         : %s\n"
            "  logdir            : %s\n"
            "  cachedir          : %s\n"
            "  libdir            : %s\n"
            "  sharedir          : %s\n"
            "  datadir           : %s\n"
            "  execdir           : %s\n"
            "  language          : %s\n"
            "  piddir            : %s\n"
            "  persistdir        : %s\n"
            "  module configdir  : %s\n"
            "  connector plugins : %s\n"
            "\n"
            "If '--basedir' is provided then all other paths, including the default\n"
            "configuration file path, are defined relative to that. As an example,\n"
            "if '--basedir /path/maxscale' is specified, then, for instance, the log\n"
            "dir will be '/path/maxscale/var/log/maxscale', the config dir will be\n"
            "'/path/maxscale/etc' and the default config file will be\n"
            "'/path/maxscale/etc/maxscale.cnf'.\n\n"
            "MaxScale documentation: https://mariadb.com/kb/en/maxscale/ \n",
            mxs::configdir(),
            default_cnf_fname,
            mxs::configdir(),
            mxs::logdir(),
            mxs::cachedir(),
            mxs::libdir(),
            mxs::sharedir(),
            mxs::datadir(),
            mxs::execdir(),
            mxs::langdir(),
            mxs::piddir(),
            mxs::config_persistdir(),
            mxs::module_configdir(),
            mxs::connector_plugindir());
}

/**
 * Deletes a particular signal from a provided signal set.
 *
 * @param sigset  The signal set to be manipulated.
 * @param signum  The signal to be deleted.
 * @param signame The name of the signal.
 *
 * @return True, if the signal could be deleted from the set, false otherwise.
 */
static bool delete_signal(sigset_t* sigset, int signum, const char* signame)
{
    int rc = sigdelset(sigset, signum);

    if (rc != 0)
    {
        log_startup_error(errno, "Failed to delete signal %s from the signal set of MaxScale", signame);
    }

    return rc == 0;
}

/**
 * Disables all signals.
 *
 * @return True, if all signals could be disabled, false otherwise.
 */
bool disable_signals(void)
{
    sigset_t sigset;

    if (sigfillset(&sigset) != 0)
    {
        log_startup_error(errno, "Failed to initialize set the signal set for MaxScale");
        return false;
    }

    if (!delete_signal(&sigset, SIGHUP, "SIGHUP"))
    {
        return false;
    }

    if (!delete_signal(&sigset, SIGUSR1, "SIGUSR1"))
    {
        return false;
    }

    if (!delete_signal(&sigset, SIGTERM, "SIGTERM"))
    {
        return false;
    }

    if (!delete_signal(&sigset, SIGSEGV, "SIGSEGV"))
    {
        return false;
    }

    if (!delete_signal(&sigset, SIGABRT, "SIGABRT"))
    {
        return false;
    }

    if (!delete_signal(&sigset, SIGILL, "SIGILL"))
    {
        return false;
    }

    if (!delete_signal(&sigset, SIGFPE, "SIGFPE"))
    {
        return false;
    }

    if (!delete_signal(&sigset, SIGCHLD, "SIGCHLD"))
    {
        return false;
    }

#ifdef SIGBUS
    if (!delete_signal(&sigset, SIGBUS, "SIGBUS"))
    {
        return false;
    }
#endif

    if (sigprocmask(SIG_SETMASK, &sigset, NULL) != 0)
    {
        log_startup_error(errno, "Failed to set the signal set for MaxScale");
        return false;
    }

    return true;
}

bool disable_normal_signals(void)
{
    sigset_t sigset;

    if (sigfillset(&sigset) != 0)
    {
        log_startup_error(errno, "Failed to initialize the signal set for MaxScale");
        return false;
    }

    if (!delete_signal(&sigset, SIGHUP, "SIGHUP"))
    {
        return false;
    }

    if (!delete_signal(&sigset, SIGUSR1, "SIGUSR1"))
    {
        return false;
    }

    if (!delete_signal(&sigset, SIGTERM, "SIGTERM"))
    {
        return false;
    }

    if (sigprocmask(SIG_SETMASK, &sigset, NULL) != 0)
    {
        log_startup_error(errno, "Failed to set the signal set for MaxScale");
        return false;
    }

    return true;
}

/**
 * Configures the handling of a particular signal.
 *
 * @param signum  The signal number.
 * @param signame The name of the signal.
 * @param handler The handler function for the signal.
 *
 * @return True, if the signal could be configured, false otherwise.
 */
static bool configure_signal(int signum, const char* signame, void (* handler)(int))
{
    int rc = signal_set(signum, handler);

    if (rc != 0)
    {
        log_startup_error("Failed to set signal handler for %s.", signame);
    }

    return rc == 0;
}

/**
 * Configure fatal signal handlers
 *
 * @return True if signal handlers were installed correctly
 */
bool configure_critical_signals(void)
{
    if (!configure_signal(SIGSEGV, "SIGSEGV", sigfatal_handler))
    {
        return false;
    }

    if (!configure_signal(SIGABRT, "SIGABRT", sigfatal_handler))
    {
        return false;
    }

    if (!configure_signal(SIGILL, "SIGILL", sigfatal_handler))
    {
        return false;
    }

    if (!configure_signal(SIGFPE, "SIGFPE", sigfatal_handler))
    {
        return false;
    }

#ifdef SIGBUS
    if (!configure_signal(SIGBUS, "SIGBUS", sigfatal_handler))
    {
        return false;
    }
#endif

    return true;
}

/**
 * Configures signal handling of MaxScale.
 *
 * @return True, if all signals could be configured, false otherwise.
 */
bool configure_normal_signals(void)
{
    if (!configure_signal(SIGHUP, "SIGHUP", sighup_handler))
    {
        return false;
    }

    if (!configure_signal(SIGUSR1, "SIGUSR1", sigusr1_handler))
    {
        return false;
    }

    if (!configure_signal(SIGTERM, "SIGTERM", sigterm_handler))
    {
        return false;
    }

    if (!configure_signal(SIGINT, "SIGINT", sigint_handler))
    {
        return false;
    }

    return true;
}

bool setup_signals()
{
    bool rv = false;

    if (!configure_critical_signals())
    {
        log_startup_error("Failed to configure fatal signal handlers.");
    }
    else
    {
        sigset_t sigpipe_mask;
        sigemptyset(&sigpipe_mask);
        sigaddset(&sigpipe_mask, SIGPIPE);
        sigset_t saved_mask;
        int eno = pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask);

        if (eno != 0)
        {
            log_startup_error(eno, "Failed to initialise signal mask for MaxScale");
        }
        else
        {
            rv = true;
        }
    }

    return rv;
}

/**
 * Restore default signals
 */
void restore_signals()
{
    configure_signal(SIGHUP, "SIGHUP", SIG_DFL);
    configure_signal(SIGUSR1, "SIGUSR1", SIG_DFL);
    configure_signal(SIGTERM, "SIGTERM", SIG_DFL);
    configure_signal(SIGINT, "SIGINT", SIG_DFL);
    configure_signal(SIGSEGV, "SIGSEGV", SIG_DFL);
    configure_signal(SIGABRT, "SIGABRT", SIG_DFL);
    configure_signal(SIGILL, "SIGILL", SIG_DFL);
    configure_signal(SIGFPE, "SIGFPE", SIG_DFL);
#ifdef SIGBUS
    configure_signal(SIGBUS, "SIGBUS", SIG_DFL);
#endif
}

bool set_runtime_dirs(const char* basedir)
{
    bool rv = true;
    std::string path;

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_SHARE_SUBPATH)))
    {
        set_sharedir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_LOG_SUBPATH)))
    {
        set_logdir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_CACHE_SUBPATH)))
    {
        set_cachedir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_CONFIG_SUBPATH)))
    {
        set_configdir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_MODULE_CONFIG_SUBPATH)))
    {
        set_module_configdir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_DATA_SUBPATH)))
    {
        mxs::set_datadir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_LANG_SUBPATH)))
    {
        mxs::set_langdir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_PID_SUBPATH)))
    {
        set_piddir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path,
                                    basedir,
                                    "var/" MXS_DEFAULT_DATA_SUBPATH "/"
                                    MXS_DEFAULT_CONFIG_PERSIST_SUBPATH)))
    {
        set_config_persistdir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path,
                                    basedir,
                                    MXS_DEFAULT_CONNECTOR_PLUGIN_SUBPATH)))
    {
        set_connector_plugindir(path.c_str());
    }

    return rv;
}

/**
 * Set the directories of MaxScale relative to a basedir
 *
 * @param basedir The base directory relative to which the other are set.
 *
 * @return True if the directories could be set, false otherwise.
 */
bool set_dirs(const char* basedir)
{
    bool rv = true;
    std::string path;

    rv = set_runtime_dirs(basedir);

    // The two paths here are not inside set_runtime_dirs on purpose: they are set by --basedir but not by
    // --runtimedir. The former is used with tarball installations and the latter is used to run multiple
    // MaxScale instances on the same server.

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_LIB_SUBPATH)))
    {
        set_libdir(path.c_str());
    }

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_EXEC_SUBPATH)))
    {
        set_execdir(path.c_str());
    }

    return rv;
}

static mxb::ThreadPool thread_pool;

mxb::ThreadPool& mxs::thread_pool()
{
    return ::thread_pool;
}

/**
 * A RAII class that at construction time takes overship of pipe
 * handle and at destruction time notifies parent if there is
 * a need for that.
 */
class ChildExit
{
public:
    ChildExit(const ChildExit&) = delete;
    ChildExit& operator=(const ChildExit&) = delete;

    ChildExit(int child_pipe, int* pRc)
        : m_child_pipe(child_pipe)
        , m_rc(*pRc)
    {
    }

    ~ChildExit()
    {
        if (m_child_pipe != -1 && m_rc != MAXSCALE_SHUTDOWN)
        {
            write_child_exit_code(m_child_pipe, m_rc) ;
            ::close(m_child_pipe);
        }

        if (this_unit.unload_modules_at_exit)
        {
            unload_all_modules();
        }

        log_exit_status();
        restore_signals();
    }

private:
    int        m_child_pipe;
    const int& m_rc;
};

/**
 * @mainpage
 * The main entry point into MaxScale
 *
 * Logging and error printing
 * ---
 * What is printed to the terminal is something that the user can understand,
 * and/or something what the user can do for. For example, fix configuration.
 * More detailed messages are printed to error log, and optionally to trace
 * and debug log.
 *
 * As soon as process switches to daemon process, stderr printing is stopped -
 * except when it comes to command-line argument processing.
 * This is not an obvious solution because stderr is often directed to somewhere,
 * but currently this is the case.
 *
 * The configuration file is by default /etc/maxscale.cnf
 * The name of configuration file and its location can also be specified with a
 * command-line argument.
 *
 * @param argc The argument count
 * @param argv The array of arguments themselves
 * @return 0 if process exited normally, otherwise a non-zero value is returned
 */
int main(int argc, char** argv)
{
    int rc = MAXSCALE_SHUTDOWN;

    mxs::Config& cnf = mxs::Config::init(argc, argv);

    maxscale_reset_starttime();

    snprintf(this_unit.datadir, PATH_MAX, "%s", MXS_DEFAULT_DATADIR);
    this_unit.datadir[PATH_MAX] = '\0';

    // Option string for getopt
    const char accepted_opts[] = "dnce:f:g:l:vVs:S:?L:D:C:B:U:A:P:G:N:E:F:M:H:J:p";
    const char* specified_user = NULL;
    char export_cnf[PATH_MAX + 1] = "";
    string cnf_file_arg;    /*< conf filename from cmd-line arg */
    string tmp_path;
    int opt;
#ifdef HAVE_GLIBC
    int option_index;
    while ((opt = getopt_long(argc,
                              argv,
                              accepted_opts,
                              long_options,
                              &option_index)) != -1)
#else
    while ((opt = getopt(argc, argv, accepted_opts)) != -1)
#endif
    {
        bool succp = true;

        switch (opt)
        {
        case 'n':
            /*< Daemon mode, MaxScale forks and parent exits. */
            this_unit.daemon_mode = true;
            break;

        case 'd':
            /*< Non-daemon mode, MaxScale does not fork. */
            this_unit.daemon_mode = false;
            break;

        case 'f':
            /*<
             * Simply copy the conf file argument. Expand or validate
             * it when MaxScale home directory is resolved.
             */
            if (optarg[0] != '-')
            {
                cnf_file_arg = optarg;
            }
            if (cnf_file_arg.empty())
            {
                log_startup_error("Configuration file argument identifier \'-f\' was specified but "
                                  "the argument didn't specify a valid configuration file or the "
                                  "argument was missing.");
                usage();
                succp = false;
            }
            break;

        case 'v':
            printf("MaxScale %s\n", MAXSCALE_VERSION);
            return EXIT_SUCCESS;

        case 'V':
            printf("MaxScale %s - %s\n", MAXSCALE_VERSION, maxscale_commit);

            // MAXSCALE_SOURCE is two values separated by a space, see CMakeLists.txt
            if (strcmp(MAXSCALE_SOURCE, " ") != 0)
            {
                printf("Source:        %s\n", MAXSCALE_SOURCE);
            }
            if (strcmp(MAXSCALE_CMAKE_FLAGS, "") != 0)
            {
                printf("CMake flags:   %s\n", MAXSCALE_CMAKE_FLAGS);
            }
            if (strcmp(MAXSCALE_JENKINS_BUILD_TAG, "") != 0)
            {
                printf("Jenkins build: %s\n", MAXSCALE_JENKINS_BUILD_TAG);
            }
            return EXIT_SUCCESS;

        case 'l':
            if (strncasecmp(optarg, "file", PATH_MAX) == 0)
            {
                cnf.log_target = MXB_LOG_TARGET_FS;
            }
            else if (strncasecmp(optarg, "stdout", PATH_MAX) == 0)
            {
                cnf.log_target = MXB_LOG_TARGET_STDOUT;
            }
            else
            {
                log_startup_error("Configuration file argument identifier \'-l\' was specified but "
                                  "the argument didn't specify a valid configuration file or the "
                                  "argument was missing.");
                usage();
                succp = false;
            }
            break;

        case 'L':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_logdir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'N':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                mxs::set_langdir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'P':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_piddir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'D':
            snprintf(this_unit.datadir, PATH_MAX, "%s", optarg);
            this_unit.datadir[PATH_MAX] = '\0';
            mxs::set_datadir(optarg);
            this_unit.datadir_defined = true;
            break;

        case 'C':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_configdir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'B':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_libdir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'A':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_cachedir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'E':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_execdir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'H':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_connector_plugindir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'J':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_sharedir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'F':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_config_persistdir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'M':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                set_module_configdir(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'R':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                succp = set_dirs(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'r':
            if (handle_path_arg(&tmp_path, optarg, NULL))
            {
                succp = set_runtime_dirs(tmp_path.c_str());
            }
            else
            {
                succp = false;
            }
            break;

        case 'S':
            {
                char* tok = strstr(optarg, "=");
                if (tok)
                {
                    tok++;
                    if (tok)
                    {
                        cnf.maxlog.set(config_truth_value(tok));
                        this_unit.maxlog_configured = true;
                    }
                }
                else
                {
                    cnf.maxlog.set(config_truth_value(optarg));
                    this_unit.maxlog_configured = true;
                }
            }
            break;

        case 's':
            {
                char* tok = strstr(optarg, "=");
                if (tok)
                {
                    tok++;
                    if (tok)
                    {
                        cnf.syslog.set(config_truth_value(tok));
                        this_unit.syslog_configured = true;
                    }
                }
                else
                {
                    cnf.syslog.set(config_truth_value(optarg));
                    this_unit.syslog_configured = true;
                }
            }
            break;

        case 'U':
            specified_user = optarg;
            if (set_user(specified_user) != 0)
            {
                succp = false;
            }
            break;

        case 'G':
            set_log_augmentation(optarg);
            break;

        case '?':
            usage();
            return EXIT_SUCCESS;

        case 'c':
            cnf.config_check = true;
            break;

        case 'e':
            cnf.config_check = true;
            strcpy(export_cnf, optarg);
            break;

        case 'p':
            cnf.passive.set(true);
            break;

        case 'g':
            if (!handle_debug_args(optarg))
            {
                succp = false;
            }
            break;

        default:
            usage();
            succp = false;
            break;
        }

        if (!succp)
        {
            return MAXSCALE_BADARG;
        }
    }

    if (!user_is_acceptable(specified_user))
    {
        // Error was logged in user_is_acceptable().
        return EXIT_FAILURE;
    }

    if (cnf.config_check)
    {
        this_unit.daemon_mode = false;
        cnf.log_target = MXB_LOG_TARGET_STDOUT;
    }

    uint64_t systemd_interval = 0;      // in microseconds
#ifdef HAVE_SYSTEMD
    // Systemd watchdog. Must be called in the initial thread */
    if (sd_watchdog_enabled(false, &systemd_interval) <= 0)
    {
        systemd_interval = 0;   // Disabled
    }
#endif

    int child_pipe = -1;
    if (!this_unit.daemon_mode)
    {
        print_info("MaxScale will be run in the terminal process.");
    }
    else
    {
        // If the function returns, we are in the child.
        child_pipe = daemonize();

        if (child_pipe == -1)
        {
            return MAXSCALE_INTERNALERROR;
        }
    }

    // This RAII class ensures that the parent is notified at process exit.
    ChildExit child_exit(child_pipe, &rc);

    // NOTE: From here on, rc *must* be assigned the return value, before returning.
    if (!setup_signals())
    {
        rc = MAXSCALE_INTERNALERROR;
        return rc;
    }

    const string cnf_file_path = resolve_maxscale_conf_fname(cnf_file_arg);
    if (cnf_file_path.empty())
    {
        rc = MAXSCALE_BADCONFIG;
        return rc;
    }

    auto cfg_file_read_res = sniff_configuration(cnf_file_path);
    if (!cfg_file_read_res.success)
    {
        rc = MAXSCALE_BADCONFIG;
        return rc;
    }

    // Set the default location for plugins. Path-related settings have been read by now.
    mxq::MariaDB::set_default_plugin_dir(mxs::connector_plugindir());

    if (mxb_log_inited())
    {
        // If the log was inited due to some error logging *and* we did not exit,
        // we need to close it so that it can be opened again, this time with
        // the final settings.
        mxs_log_finish();
    }

    if (cnf.log_target != MXB_LOG_TARGET_STDOUT && this_unit.daemon_mode)
    {
        mxs_log_redirect_stdout(true);
    }

    if (!init_log())
    {
        rc = MAXSCALE_BADCONFIG;
        return rc;
    }

    if (!init_base_libraries())
    {
        rc = MAXSCALE_INTERNALERROR;
        return rc;
    }

    atexit(finish_base_libraries);

    mxb::WatchdogNotifier watchdog_notifier(systemd_interval);
    MainWorker main_worker(&watchdog_notifier);

    if (!apply_main_config(cfg_file_read_res.config))
    {
        rc = MAXSCALE_BADCONFIG;
        return rc;
    }

    mxs_log_set_syslog_enabled(cnf.syslog.get());
    mxs_log_set_maxlog_enabled(cnf.maxlog.get());

    MXB_NOTICE("syslog logging is %s.", cnf.syslog.get() ? "enabled" : "disabled");
    MXB_NOTICE("maxlog logging is %s.", cnf.maxlog.get() ? "enabled" : "disabled");

    // Try to create the persisted configuration directory. This needs to be done before the path validation
    // done by check_paths() to prevent it from failing. The directory wont' exist if it's the first time
    // MaxScale is starting up with this configuration.
    mxs_mkdir_all(mxs::config_persistdir(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    if (!check_paths())
    {
        rc = MAXSCALE_BADCONFIG;
        return rc;
    }

    if (!cnf.debug.empty() && !handle_debug_args(&cnf.debug[0]))
    {
        rc = MAXSCALE_INTERNALERROR;
        return rc;
    }

    if (!this_unit.redirect_output_to.empty())
    {
        if (!redirect_stdout_and_stderr(this_unit.redirect_output_to))
        {
            rc = MAXSCALE_INTERNALERROR;
            return rc;
        }
    }

    if (!cnf.config_check)
    {
        if (is_maxscale_already_running())
        {
            rc = MAXSCALE_ALREADYRUNNING;
            return rc;
        }
    }

    if (!cnf.syslog.get() && !cnf.maxlog.get())
    {
        print_warning("Both MaxScale and Syslog logging disabled.");
    }

    // Config successfully read and we are a unique MaxScale, time to log some info.
    MXS_NOTICE("Host: '%s' OS: %s@%s, %s, %s with %lu processor cores.",
               cnf.nodename.c_str(), cnf.sysname.c_str(), cnf.release.c_str(),
               cnf.version.c_str(), cnf.machine.c_str(), get_processor_count());

    struct sysinfo info;
    sysinfo(&info);
    MXS_NOTICE("Total usable main memory: %s.",
               mxb::pretty_size(info.mem_unit * info.totalram).c_str());
    MXS_NOTICE("MariaDB MaxScale %s started (Commit: %s)", MAXSCALE_VERSION, MAXSCALE_COMMIT);
    MXS_NOTICE("MaxScale is running in process %i", getpid());

    if (!this_unit.daemon_mode)
    {
        fprintf(stderr,
                "\n"
                "Configuration file : %s\n"
                "Log directory      : %s\n"
                "Data directory     : %s\n"
                "Module directory   : %s\n"
                "Service cache      : %s\n\n",
                cnf_file_path.c_str(),
                mxs::logdir(),
                mxs::datadir(),
                mxs::libdir(),
                mxs::cachedir());
    }

    MXS_NOTICE("Configuration file: %s", cnf_file_path.c_str());
    MXS_NOTICE("Log directory: %s", mxs::logdir());
    MXS_NOTICE("Data directory: %s", mxs::datadir());
    MXS_NOTICE("Module directory: %s", mxs::libdir());
    MXS_NOTICE("Service cache: %s", mxs::cachedir());

    if (this_unit.daemon_mode)
    {
        if (!change_cwd())
        {
            rc = MAXSCALE_INTERNALERROR;
            return rc;
        }
    }

    cleanup_old_process_datadirs();
    if (!cnf.config_check)
    {
        /*
         * Set the data directory. We use a unique directory name to avoid conflicts
         * if multiple instances of MaxScale are being run on the same machine.
         */
        if (create_datadir(mxs::datadir(), this_unit.datadir))
        {
            mxs::set_process_datadir(this_unit.datadir);
            atexit(cleanup_process_datadir);
        }
        else
        {
            log_startup_error(errno, "Cannot create data directory '%s'", this_unit.datadir);
            rc = MAXSCALE_BADCONFIG;
            return rc;
        }
    }

    if (!qc_setup(&cnf.qc_cache_properties,
                  cnf.qc_sql_mode,
                  cnf.qc_name.c_str(),
                  cnf.qc_args.c_str()))
    {
        log_startup_error("Failed to initialise query classifier library.");
        rc = MAXSCALE_INTERNALERROR;
        return rc;
    }

    // Load the password encryption/decryption key, as monitors and services may need it.
    if (!load_encryption_keys())
    {
        log_startup_error("Error loading password decryption key.");
        rc = MAXSCALE_SHUTDOWN;
        return rc;
    }

    /** Load the admin users */
    rest_users_init();

    // Create the configuration manager
    mxs::ConfigManager manager(&main_worker);

    /**
     * The following lambda function is executed as the first event on the main worker. This is what starts
     * up the listeners for all services.
     *
     * Due to the fact that the main thread runs a worker thread we have to queue the starting
     * of the listeners to happen after all workers have started. This allows worker messages to be used
     * when listeners are being started.
     *
     * Once the main worker is dedicated to doing work other than handling traffic the code could be executed
     * immediately after the worker thread have been started. This would make the startup logic clearer as
     * the order of the events would be the way they appear to be.
     */
    auto do_startup = [&]() {
            bool use_static_cnf = !manager.load_cached_config();

            if (use_static_cnf)
            {
                ConfContextMap config_context;
                if (!config_load_and_process(cnf_file_path, cfg_file_read_res.config, config_context))
                {
                    print_alert("Failed to open, read or process the MaxScale configuration "
                                "file. See the error log for details.");
                    MXS_ALERT("Failed to open, read or process the MaxScale configuration file %s.",
                              cnf_file_path.c_str());
                    rc = MAXSCALE_BADCONFIG;
                    maxscale_shutdown();
                    return;
                }

                if (cnf.config_check)
                {
                    MXS_NOTICE("Configuration was successfully verified.");

                    if (*export_cnf && export_config_file(export_cnf, config_context))
                    {
                        MXS_NOTICE("Configuration exported to '%s'", export_cnf);
                    }

                    rc = MAXSCALE_SHUTDOWN;
                    maxscale_shutdown();
                    return;
                }
            }
            else
            {
                auto res = manager.process_cached_config();

                if (res != mxs::ConfigManager::Startup::OK)
                {
                    if (res == mxs::ConfigManager::Startup::RESTART)
                    {
                        MXS_NOTICE("Attempting to restart MaxScale");

                        if (this_unit.daemon_mode)
                        {
                            // We have to fake success on the main process since we are using Type=forking.
                            // This has to be done because systemd only considers the final process as the
                            // main process whose return code is checked against RestartForceExitStatus. For
                            // more information, refer to the following issues:
                            //   https://github.com/systemd/systemd/issues/19295
                            //   https://github.com/systemd/systemd/pull/19685

                            write_child_exit_code(child_pipe, MAXSCALE_SHUTDOWN);
                        }

                        rc = MAXSCALE_RESTARTING;
                    }
                    else
                    {
                        MXS_ALERT("Failed to apply cached configuration, cannot continue. "
                                  "To start MaxScale without the cached configuration, disable "
                                  "configuration synchronization or remove the cached file.");
                        rc = MAXSCALE_BADCONFIG;
                    }

                    maxscale_shutdown();
                    return;
                }
            }

            if (cnf.admin_enabled)
            {
                bool success = mxs_admin_init();

                if (!success && (cnf.admin_host == "::"))
                {
                    MXS_WARNING("Failed to bind on address '::', attempting to "
                                "bind on IPv4 address '0.0.0.0'.");
                    cnf.admin_host = "0.0.0.0";
                    success = mxs_admin_init();
                }

                if (success)
                {
                    MXS_NOTICE("Started REST API on [%s]:%d",
                               cnf.admin_host.c_str(), (int)cnf.admin_port);
                    // Start HttpSql cleanup thread.
                    HttpSql::start_cleanup();
                }
                else
                {
                    log_startup_error("Failed to initialize REST API.");
                    rc = MAXSCALE_INTERNALERROR;
                    maxscale_shutdown();
                    return;
                }
            }

            // If the configuration was read from the static configuration file, the objects need to be
            // started after they have been created.
            if (use_static_cnf)
            {
                // Ideally we'd do this in mxs::Config::Specification::validate but since it is configured
                // before the objects are created, it's simpler to do the check here. For runtime changes it
                // is done inside the validation function.
                auto cluster = cnf.config_sync_cluster;

                if (!cluster.empty() && !MonitorManager::find_monitor(cluster.c_str()))
                {
                    log_startup_error("The value of '%s' is not the name of a monitor: %s.",
                                      CN_CONFIG_SYNC_CLUSTER, cluster.c_str());
                    rc = MAXSCALE_BADCONFIG;
                    maxscale_shutdown();
                    return;
                }

                MonitorManager::start_all_monitors();
                MonitorManager::wait_one_tick();

                if (!service_launch_all())
                {
                    log_startup_error("Failed to start all MaxScale services.");
                    rc = MAXSCALE_NOSERVICES;
                    maxscale_shutdown();
                    return;
                }
            }

            manager.start_sync();

            if (this_unit.daemon_mode)
            {
                // Successful start, notify the parent process that it can exit.
                write_child_exit_code(child_pipe, rc);
            }
        };

    watchdog_notifier.start();

    add_built_in_module(mariadbprotocol_info());
    add_built_in_module(mariadbauthenticator_info());
    // Initialize the internal query classifier. The actual plugin will be
    // initialized via the module initialization below.
    if (qc_process_init(QC_INIT_SELF))
    {
        // Before we start the workers we need to check if a shutdown signal has been received
        if (!maxscale_is_shutting_down())
        {
            if (RoutingWorker::init(&watchdog_notifier))
            {
                // If a shutdown signal was received while we were initializing the workers,
                // we need to exit. After this point, the shutdown will be driven by the workers.
                if (!maxscale_is_shutting_down())
                {

                    // Start the routing workers, each in a thread of its own.
                    if (RoutingWorker::start_workers())
                    {
                        MXS_NOTICE("MaxScale started with %d worker threads.", config_threadcount());

                        if (configure_normal_signals())
                        {
                            if (main_worker.execute(do_startup, RoutingWorker::EXECUTE_QUEUED))
                            {
                                // This call will block until MaxScale is shut down.
                                main_worker.run();
                                MXS_NOTICE("MaxScale is shutting down.");

                                disable_normal_signals();

                                // Shutting down started, wait for all routing workers.
                                RoutingWorker::join_workers();
                                MXS_NOTICE("All workers have shut down.");

                                MonitorManager::destroy_all_monitors();

                                maxscale_start_teardown();
                                service_destroy_instances();
                                filter_destroy_instances();
                                listener_destroy_instances();
                                ServerManager::destroy_all();

                                MXS_NOTICE("MaxScale shutdown completed.");
                            }
                            else
                            {
                                log_startup_error("Failed to queue startup task.");
                                rc = MAXSCALE_INTERNALERROR;
                            }
                        }
                        else
                        {
                            log_startup_error("Failed to install signal handlers.");
                            rc = MAXSCALE_INTERNALERROR;
                        }
                    }
                    else
                    {
                        log_startup_error("Failed to start routing workers.");
                        rc = MAXSCALE_INTERNALERROR;
                    }
                }
                else
                {
                    rc = MAXSCALE_SHUTDOWN;
                }

                RoutingWorker::finish();
            }
            else
            {
                log_startup_error("Failed to initialize routing workers.");
                rc = MAXSCALE_INTERNALERROR;
            }
        }
        else
        {
            rc = MAXSCALE_SHUTDOWN;
        }

        // Finalize the internal query classifier. The actual plugin was finalized
        // via the module finalizarion above.
        qc_process_end(QC_INIT_SELF);
    }
    else
    {
        log_startup_error("Failed to initialize the internal query classifier.");
        rc = MAXSCALE_INTERNALERROR;
    }

    watchdog_notifier.stop();

    return rc;
}   /*< End of main */

static void unlock_pidfile()
{
    if (this_unit.pidfd != PIDFD_CLOSED)
    {
        if (flock(this_unit.pidfd, LOCK_UN | LOCK_NB) != 0)
        {
            log_startup_error(errno, "Failed to unlock PID file '%s'", this_unit.pidfile);
        }
        close(this_unit.pidfd);
        this_unit.pidfd = PIDFD_CLOSED;
    }
}

/**
 * Unlink pid file, called at program exit
 */
static void unlink_pidfile(void)
{
    unlock_pidfile();

    if (strlen(this_unit.pidfile))
    {
        if (unlink(this_unit.pidfile))
        {
            MXS_WARNING("Failed to remove pidfile %s: %s", this_unit.pidfile, mxs_strerror(errno));
        }
    }
}

bool pid_is_maxscale(int pid)
{
    bool rval = false;
    std::stringstream ss;
    ss << "/proc/" << pid << "/comm";
    std::ifstream file(ss.str());
    std::string line;

    if (file && std::getline(file, line))
    {
        if (line == "maxscale" && pid != getpid())
        {
            rval = true;
        }
    }

    return rval;
}

/**
 * Check if the maxscale.pid file exists and has a valid PID in it. If one has already been
 * written and a MaxScale process is running, this instance of MaxScale should shut down.
 * @return True if the conditions for starting MaxScale are not met and false if
 * no PID file was found or there is no process running with the PID of the maxscale.pid
 * file. If false is returned, this process should continue normally.
 */
bool pid_file_exists()
{
    const int PIDSTR_SIZE = 1024;

    char pathbuf[PATH_MAX + 1];
    char pidbuf[PIDSTR_SIZE];
    pid_t pid;
    bool lock_failed = false;

    snprintf(pathbuf, PATH_MAX, "%s/maxscale.pid", mxs::piddir());
    pathbuf[PATH_MAX] = '\0';

    if (access(pathbuf, F_OK) != 0)
    {
        return false;
    }

    if (access(pathbuf, R_OK) == 0)
    {
        int fd, b;

        if ((fd = open(pathbuf, O_RDWR)) == -1)
        {
            log_startup_error(errno, "Failed to open PID file '%s'", pathbuf);
            return true;
        }
        if (flock(fd, LOCK_EX | LOCK_NB))
        {
            if (errno != EWOULDBLOCK)
            {
                log_startup_error(errno, "Failed to lock PID file '%s'", pathbuf);
                close(fd);
                return true;
            }
            lock_failed = true;
        }

        this_unit.pidfd = fd;
        b = read(fd, pidbuf, sizeof(pidbuf));

        if (b == -1)
        {
            log_startup_error(errno, "Failed to read from PID file '%s'", pathbuf);
            unlock_pidfile();
            return true;
        }
        else if (b == 0)
        {
            /** Empty file */
            log_startup_error("PID file read from '%s'. File was empty. If the file is the "
                              "correct PID file and no other MaxScale processes are running, "
                              "please remove it manually and start MaxScale again.", pathbuf);
            unlock_pidfile();
            return true;
        }

        pidbuf[(size_t)b < sizeof(pidbuf) ? (size_t)b : sizeof(pidbuf) - 1] = '\0';
        pid = strtol(pidbuf, NULL, 0);

        if (pid < 1)
        {
            /** Bad PID */
            log_startup_error("PID file read from '%s'. File contents not valid. If the file "
                              "is the correct PID file and no other MaxScale processes are "
                              "running, please remove it manually and start MaxScale again.", pathbuf);
            unlock_pidfile();
            return true;
        }

        if (pid_is_maxscale(pid))
        {
            log_startup_error("MaxScale is already running. Process id: %d. "
                              "Use another location for the PID file to run multiple "
                              "instances of MaxScale on the same machine.", pid);
            unlock_pidfile();
        }
        else
        {
            /** no such process, old PID file */
            if (lock_failed)
            {
                log_startup_error("Locking the PID file '%s' failed. Read PID from file "
                                  "and no process found with PID %d. Confirm that no other "
                                  "process holds the lock on the PID file.",
                                  pathbuf, pid);
                close(fd);
            }
            return lock_failed;
        }
    }
    else
    {
        log_startup_error("Cannot open PID file '%s', no read permissions. Please confirm "
                          "that the user running MaxScale has read permissions on the file.",
                          pathbuf);
    }
    return true;
}

/**
 * Write process pid into pidfile anc close it
 * Parameters:
 * @param home_dir The MaxScale home dir
 * @return 0 on success, 1 on failure
 *
 */

static int write_pid_file()
{
    if (!mxs_mkdir_all(mxs::piddir(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
    {
        MXS_ERROR("Failed to create PID directory.");
        return 1;
    }

    snprintf(this_unit.pidfile, PATH_MAX, "%s/maxscale.pid", mxs::piddir());

    if (this_unit.pidfd == PIDFD_CLOSED)
    {
        int fd = -1;

        fd = open(this_unit.pidfile, O_WRONLY | O_CREAT, 0777);
        if (fd == -1)
        {
            log_startup_error(errno, "Failed to open PID file '%s'", this_unit.pidfile);
            return -1;
        }

        if (flock(fd, LOCK_EX | LOCK_NB))
        {
            if (errno == EWOULDBLOCK)
            {
                log_startup_error("Failed to lock PID file '%s', another process is holding a lock on it. "
                                  "Please confirm that no other MaxScale process is using the same "
                                  "PID file location.",
                                  this_unit.pidfile);
            }
            else
            {
                log_startup_error("Failed to lock PID file '%s'.", this_unit.pidfile);
            }
            close(fd);
            return -1;
        }
        this_unit.pidfd = fd;
    }

    /* truncate pidfile content */
    if (ftruncate(this_unit.pidfd, 0))
    {
        log_startup_error("MaxScale failed to truncate PID file '%s'.", this_unit.pidfile);
        unlock_pidfile();
        return -1;
    }

    string pidstr = std::to_string(getpid());
    ssize_t len = pidstr.length();

    if (pwrite(this_unit.pidfd, pidstr.c_str(), len, 0) != len)
    {
        log_startup_error(errno, "MaxScale failed to write into PID file '%s'", this_unit.pidfile);
        unlock_pidfile();
        return -1;
    }

    /* success */
    return 0;
}

static bool handle_path_arg(std::string* dest, const char* path, const char* arg)
{
    bool rval = false;
    if (path)
    {
        string tmp = path;
        if (tmp.empty() || tmp.back() != '/')
        {
            tmp += '/';
        }

        if (arg)
        {
            tmp += arg;
        }

        *dest = move(tmp);
        rval = true;
    }
    return rval;
}

bool check_paths()
{
    // The default path for the connector_plugindir isn't valid. This doesn't matter that much as we don't
    // include the plugins in the installation.
    if (strcmp(mxs::connector_plugindir(), MXS_DEFAULT_CONNECTOR_PLUGINDIR) != 0)
    {
        if (!check_dir_access(mxs::connector_plugindir(), true, false))
        {
            return false;
        }
    }

    return check_dir_access(mxs::logdir(), true, false)
           && check_dir_access(mxs::cachedir(), true, true)
           && check_dir_access(mxs::configdir(), true, false)
           && check_dir_access(mxs::module_configdir(), true, false)
           && check_dir_access(mxs::datadir(), true, false)
           && check_dir_access(mxs::langdir(), true, false)
           && check_dir_access(mxs::piddir(), true, true)
           && check_dir_access(mxs::config_persistdir(), true, true)
           && check_dir_access(mxs::libdir(), true, false)
           && check_dir_access(mxs::execdir(), true, false);
}

void set_log_augmentation(const char* value)
{
    // Command line arguments are handled first, thus command line argument
    // has priority.

    static bool augmentation_set = false;

    if (!augmentation_set)
    {
        mxs_log_set_augmentation(atoi(value));

        augmentation_set = true;
    }
}

/**
 * Read various directory paths and log settings from configuration. Variable substitution is
 * assumed to be already performed.
 *
 * @param main_config Parsed [maxscale]-section from the main configuration file.
 */
static void apply_dir_log_config(const mxb::ini::map_result::ConfigSection& main_config)
{
    const string* value = nullptr;

    auto find_helper = [&main_config, &value](const string& key) {
            bool rval = false;
            const auto& kvs = main_config.key_values;
            auto it = kvs.find(key);
            if (it != kvs.end())
            {
                value = &it->second.value;
                rval = true;
            }
            return rval;
        };

    auto add_slash = [](const string* path) {
            string rval;
            rval.reserve(path->length() + 1);
            rval += *path;
            if (rval.empty() || rval.back() != '/')
            {
                rval += '/';
            }
            return rval;
        };

    // These will not override command line parameters but will override default values. */
    if (find_helper(CN_LOGDIR))
    {
        if (strcmp(mxs::logdir(), MXS_DEFAULT_LOGDIR) == 0)
        {
            auto tmp = add_slash(value);
            set_logdir(tmp.c_str());
        }
    }

    if (find_helper(CN_LIBDIR))
    {
        if (strcmp(mxs::libdir(), MXS_DEFAULT_LIBDIR) == 0)
        {
            auto tmp = add_slash(value);
            set_libdir(tmp.c_str());
        }
    }

    if (find_helper(CN_SHAREDIR))
    {
        if (strcmp(mxs::sharedir(), MXS_DEFAULT_SHAREDIR) == 0)
        {
            auto tmp = add_slash(value);
            set_sharedir(tmp.c_str());
        }
    }

    if (find_helper(CN_PIDDIR))
    {
        if (strcmp(mxs::piddir(), MXS_DEFAULT_PIDDIR) == 0)
        {
            auto tmp = add_slash(value);
            set_piddir(tmp.c_str());
        }
    }

    if (find_helper(CN_DATADIR))
    {
        if (!this_unit.datadir_defined)
        {
            auto tmp = add_slash(value);
            snprintf(this_unit.datadir, PATH_MAX, "%s", tmp.c_str());
            this_unit.datadir[PATH_MAX] = '\0';
            mxs::set_datadir(tmp.c_str());
            this_unit.datadir_defined = true;
        }
    }

    if (find_helper(CN_CACHEDIR))
    {
        if (strcmp(mxs::cachedir(), MXS_DEFAULT_CACHEDIR) == 0)
        {
            auto tmp = add_slash(value);
            set_cachedir(tmp.c_str());
        }
    }

    if (find_helper(CN_LANGUAGE))
    {
        if (strcmp(mxs::langdir(), MXS_DEFAULT_LANGDIR) == 0)
        {
            auto tmp = add_slash(value);
            mxs::set_langdir(tmp.c_str());
        }
    }

    if (find_helper(CN_EXECDIR))
    {
        if (strcmp(mxs::execdir(), MXS_DEFAULT_EXECDIR) == 0)
        {
            auto tmp = add_slash(value);
            set_execdir(tmp.c_str());
        }
    }

    if (find_helper(CN_CONNECTOR_PLUGINDIR))
    {
        if (strcmp(mxs::connector_plugindir(), MXS_DEFAULT_CONNECTOR_PLUGINDIR) == 0)
        {
            auto tmp = add_slash(value);
            set_connector_plugindir(tmp.c_str());
        }
    }

    if (find_helper(CN_PERSISTDIR))
    {
        if (strcmp(mxs::config_persistdir(), MXS_DEFAULT_CONFIG_PERSISTDIR) == 0)
        {
            auto tmp = add_slash(value);
            set_config_persistdir(tmp.c_str());
        }
    }

    if (find_helper(CN_MODULE_CONFIGDIR))
    {
        if (strcmp(mxs::module_configdir(), MXS_DEFAULT_MODULE_CONFIGDIR) == 0)
        {
            auto tmp = add_slash(value);
            set_module_configdir(tmp.c_str());
        }
    }

    mxs::Config& cnf = mxs::Config::get();
    if (find_helper(CN_SYSLOG))
    {
        if (!this_unit.syslog_configured)
        {
            cnf.syslog.set(config_truth_value(*value));
        }
    }

    if (find_helper(CN_MAXLOG))
    {
        if (!this_unit.maxlog_configured)
        {
            cnf.maxlog.set(config_truth_value(*value));
        }
    }

    if (find_helper(CN_LOG_AUGMENTATION))
    {
        set_log_augmentation(value->c_str());
    }
}

static int set_user(const char* user)
{
    errno = 0;
    struct passwd* pwname;
    int rval;

    pwname = getpwnam(user);
    if (pwname == NULL)
    {
        printf("Error: Failed to retrieve user information for '%s': %d %s\n",
               user,
               errno,
               errno == 0 ? "User not found" : mxs_strerror(errno));
        return -1;
    }

    rval = setgid(pwname->pw_gid);
    if (rval != 0)
    {
        printf("Error: Failed to change group to '%d': %d %s\n",
               pwname->pw_gid,
               errno,
               mxs_strerror(errno));
        return rval;
    }

    rval = setuid(pwname->pw_uid);
    if (rval != 0)
    {
        printf("Error: Failed to change user to '%s': %d %s\n",
               pwname->pw_name,
               errno,
               mxs_strerror(errno));
        return rval;
    }
    if (prctl(PR_GET_DUMPABLE) == 0)
    {
        if (prctl(PR_SET_DUMPABLE, 1) == -1)
        {
            printf("Error: Failed to set dumpable flag on for the process '%s': %d %s\n",
                   pwname->pw_name,
                   errno,
                   mxs_strerror(errno));
            return -1;
        }
    }
#ifdef SS_DEBUG
    else
    {
        printf("Running MaxScale as: %s %d:%d\n", pwname->pw_name, pwname->pw_uid, pwname->pw_gid);
    }
#endif


    return rval;
}

/**
 * Write the exit status of the child process to the parent process.
 * @param fd File descriptor to write to
 * @param code Exit status of the child process
 */
void write_child_exit_code(int fd, int code)
{
    /** Notify the parent process that an error has occurred */
    if (write(fd, &code, sizeof(int)) == -1)
    {
        printf("Failed to write child process message!\n");
    }
    close(fd);
}

/**
 * Change the current working directory
 *
 * Change the current working directory to the log directory. If this is not
 * possible, try to change location to the file system root. If this also fails,
 * return with an error.
 * @return True if changing the current working directory was successful.
 */
static bool change_cwd()
{
    bool rval = true;

    if (chdir(mxs::logdir()) != 0)
    {
        MXS_ERROR("Failed to change working directory to '%s': %d, %s. "
                  "Trying to change working directory to '/'.",
                  mxs::logdir(),
                  errno,
                  mxs_strerror(errno));
        if (chdir("/") != 0)
        {
            MXS_ERROR("Failed to change working directory to '/': %d, %s",
                      errno,
                      mxs_strerror(errno));
            rval = false;
        }
        else
        {
            MXS_WARNING("Using '/' instead of '%s' as the current working directory.",
                        mxs::logdir());
        }
    }
    else
    {
        MXS_NOTICE("Working directory: %s", mxs::logdir());
    }

    return rval;
}

/**
 * @brief Log a message about the last received signal
 */
static void log_exit_status()
{
    switch (this_unit.last_signal)
    {
    case SIGTERM:
        MXS_NOTICE("MaxScale received signal SIGTERM. Exiting.");
        break;

    case SIGINT:
        MXS_NOTICE("MaxScale received signal SIGINT. Exiting.");
        break;

    default:
        break;
    }
}

/**
 * Daemonize the process by forking and putting the process into the
 * background.
 *
 * @return File descriptor the child process should write its exit
 *          code to. -1 if the daemonization failed.
 */
static int daemonize(void)
{
    int child_pipe = -1;

    int daemon_pipe[2] = {-1, -1};
    if (pipe(daemon_pipe) == -1)
    {
        log_startup_error(errno, "Failed to create pipe for inter-process communication");
    }
    else
    {
        if (!disable_signals())
        {
            log_startup_error("Failed to disable signals.");
        }
        else
        {
            pid_t pid = fork();

            if (pid < 0)
            {
                log_startup_error(errno,
                                  "Forking MaxScale failed, the process cannot be turned into a daemon");
            }
            else if (pid != 0)
            {
                // The parent.
                close(daemon_pipe[1]);
                int child_status;
                int nread = read(daemon_pipe[0], (void*)&child_status, sizeof(int));
                close(daemon_pipe[0]);

                if (nread == -1)
                {
                    log_startup_error(errno, "Failed to read data from child process pipe");
                    exit(MAXSCALE_INTERNALERROR);
                }
                else if (nread == 0)
                {
                    /** Child process has exited or closed write pipe */
                    log_startup_error("No data read from child process pipe.");
                    exit(MAXSCALE_INTERNALERROR);
                }

                _exit(child_status);
            }
            else
            {
                // The child.
                close(daemon_pipe[0]);
                if (setsid() < 0)
                {
                    log_startup_error(errno,
                                      "Creating a new session for the daemonized MaxScale process failed");
                    close(daemon_pipe[1]);
                }
                else
                {
                    child_pipe = daemon_pipe[1];
                }
            }
        }
    }

    return child_pipe;
}

namespace
{
/**
 * Sniffs the configuration file, primarily for various directory paths, so that certain settings
 * take effect immediately.
 *
 * @param filepath The path of the configuration file.
 * @return Result object
 */
SniffResult sniff_configuration(const string& filepath)
{
    SniffResult rval;
    auto load_res = mxb::ini::parse_config_file_to_map(filepath);
    if (load_res.errors.empty())
    {
        rval.success = true;
        // At this point, we are only interested in the "maxscale"-section.
        auto& config = load_res.config;
        auto it = config.find(CN_MAXSCALE);
        if (it != config.end())
        {
            bool substitution_ok = true;

            auto& mxs_section = it->second.key_values;
            auto it2 = mxs_section.find(CN_SUBSTITUTE_VARIABLES);
            if (it2 != mxs_section.end())
            {
                bool subst_on = config_truth_value(it2->second.value);
                // Substitution affects other config files as well so save the setting.
                if (subst_on)
                {
                    mxs::Config& cnf = mxs::Config::get();
                    cnf.substitute_variables = true;
                    auto subst_errors = mxb::ini::substitute_env_vars(config);
                    if (!subst_errors.empty())
                    {
                        string errmsg = mxb::string_printf("Variable substitution to file '%s' failed. ",
                                                           filepath.c_str());
                        errmsg += mxb::create_list_string(subst_errors, " ");
                        print_alert("%s", errmsg.c_str());
                        substitution_ok = false;
                    }
                }
            }

            if (substitution_ok)
            {
                apply_dir_log_config(it->second);
            }
            rval.success = substitution_ok;
        }

        if (rval.success)
        {
            rval.config = move(load_res.config);
        }
    }
    else
    {
        string all_errors = mxb::create_list_string(load_res.errors, " ");
        print_alert("Failed to read configuration file '%s'. %s", filepath.c_str(), all_errors.c_str());
    }
    return rval;
}
}

static void enable_module_unloading(const char* arg)
{
    this_unit.unload_modules_at_exit = true;
}

static void disable_module_unloading(const char* arg)
{
    this_unit.unload_modules_at_exit = false;
}

static void enable_statement_logging(const char* arg)
{
    maxsql::mysql_set_log_statements(true);
}

static void disable_statement_logging(const char* arg)
{
    maxsql::mysql_set_log_statements(false);
}

static void enable_cors(const char* arg)
{
    mxs_admin_enable_cors();
}

static void allow_duplicate_servers(const char* arg)
{
    ServerManager::set_allow_duplicates(true);
}

static void redirect_output_to_file(const char* arg)
{
    if (arg)
    {
        this_unit.redirect_output_to = arg;
    }
}

/**
 * Process command line debug arguments
 *
 * @param args The debug argument list
 * @return True on success, false on error
 */
static bool handle_debug_args(char* args)
{
    bool arg_error = false;
    int args_found = 0;
    char* endptr = NULL;
    char* token = strtok_r(args, ",", &endptr);
    while (token)
    {
        char* value = strchr(token, '=');

        if (value)
        {
            *value++ = '\0';
        }

        bool found = false;
        for (int i = 0; debug_arguments[i].action != NULL; i++)
        {

            // Debug features are activated by running functions in the struct-array.
            if (strcmp(token, debug_arguments[i].name) == 0)
            {
                found = true;
                args_found++;
                debug_arguments[i].action(value);
                break;
            }
        }
        if (!found)
        {
            log_startup_error("Unrecognized debug setting: '%s'.", token);
            arg_error = true;
        }
        token = strtok_r(NULL, ",", &endptr);
    }
    if (args_found == 0)
    {
        arg_error = true;
    }
    if (arg_error)
    {
        // Form a string with all debug argument names listed.
        size_t total_len = 1;
        for (int i = 0; debug_arguments[i].action != NULL; i++)
        {
            total_len += strlen(debug_arguments[i].name) + 2;
        }
        char arglist[total_len];
        arglist[0] = '\0';
        for (int i = 0; debug_arguments[i].action != NULL; i++)
        {
            strcat(arglist, debug_arguments[i].name);
            // If not the last element, add a comma
            if (debug_arguments[i + 1].action != NULL)
            {
                strcat(arglist, ", ");
            }
        }
        log_startup_error("Debug argument identifier '-g' or '--debug' was specified "
                          "but no arguments were found or one of them was invalid. Supported "
                          "arguments are: %s.",
                          arglist);
    }
    return !arg_error;
}

static bool user_is_acceptable(const char* specified_user)
{
    bool acceptable = false;

    // This is very early, so we do not have logging available, but write to stderr.
    // As this is security related, we want to do as little as possible.

    uid_t uid = getuid();   // Always succeeds
    errno = 0;
    struct passwd* pw = getpwuid(uid);
    if (pw)
    {
        if (strcmp(pw->pw_name, "root") == 0)
        {
            if (specified_user && (strcmp(specified_user, "root") == 0))
            {
                // MaxScale was invoked as root and with --user=root.
                acceptable = true;
            }
            else
            {
                log_startup_error("MaxScale cannot be run as root.");
            }
        }
        else
        {
            acceptable = true;
        }
    }
    else
    {
        log_startup_error(errno, "Could not obtain user information, MaxScale will not run");
    }

    return acceptable;
}

static bool init_sqlite3()
{
    bool rv = true;

    // Collecting the memstatus introduces locking that, according to customer reports,
    // has a significant impact on the performance.
    if (sqlite3_config(SQLITE_CONFIG_MEMSTATUS, (int)0) != SQLITE_OK)   // 0 turns off.
    {
        MXS_WARNING("Could not turn off the collection of SQLite memory allocation statistics.");
        // Non-fatal, we simply will take a small performance hit.
    }

    if (sqlite3_config(SQLITE_CONFIG_MULTITHREAD) != SQLITE_OK)
    {
        MXS_ERROR("Could not set the threading mode of SQLite to Multi-thread. "
                  "MaxScale will terminate.");
        rv = false;
    }

    return rv;
}

static bool lock_dir(const std::string& path)
{
    std::string lock = path + "/maxscale.lock";
    int fd = open(lock.c_str(), O_WRONLY | O_CREAT, 0777);
    std::string pid = std::to_string(getpid());

    if (fd == -1)
    {
        MXS_ERROR("Failed to open lock file %s: %s", lock.c_str(), mxs_strerror(errno));
        return false;
    }

    if (lockf(fd, F_TLOCK, 0) == -1)
    {
        if (errno == EACCES || errno == EAGAIN)
        {
            MXS_ERROR("Failed to lock directory with file '%s', another process is holding a lock on it. "
                      "Please confirm that no other MaxScale process is using the "
                      "directory %s",
                      lock.c_str(),
                      path.c_str());
        }
        else
        {
            MXS_ERROR("Failed to lock file %s. %s", lock.c_str(), mxs_strerror(errno));
        }
        close(fd);
        return false;
    }

    if (ftruncate(fd, 0) == -1)
    {
        MXS_ERROR("Failed to truncate lock file %s: %s", lock.c_str(), mxs_strerror(errno));
        close(fd);
        unlink(lock.c_str());
        return false;
    }

    if (write(fd, pid.c_str(), pid.length()) == -1)
    {
        MXS_ERROR("Failed to write into lock file %s: %s", lock.c_str(), mxs_strerror(errno));
        close(fd);
        unlink(lock.c_str());
        return false;
    }

    this_unit.directory_locks.insert(std::pair<std::string, int>(lock, fd));

    return true;
}

bool lock_directories()
{
    std::set<std::string> paths {mxs::cachedir(), mxs::datadir()};
    return std::all_of(paths.begin(), paths.end(), lock_dir);
}

static void unlock_directories()
{
    std::for_each(this_unit.directory_locks.begin(),
                  this_unit.directory_locks.end(),
                  [&](std::pair<std::string, int> pair) {
                      close(pair.second);
                      unlink(pair.first.c_str());
                  });
}

static bool init_ssl()
{
    bool initialized = true;

#ifndef OPENSSL_1_1
    int numlocks = CRYPTO_num_locks();
    this_unit.ssl_locks = (pthread_mutex_t*)MXS_MALLOC(sizeof(pthread_mutex_t) * (numlocks + 1));

    if (this_unit.ssl_locks != NULL)
    {
        for (int i = 0; i < numlocks + 1; i++)
        {
            pthread_mutex_init(&this_unit.ssl_locks[i], NULL);
        }
    }
    else
    {
        initialized = false;
    }
#endif

    if (initialized)
    {
        SSL_library_init();
        SSL_load_error_strings();
        OPENSSL_add_all_algorithms_noconf();

#ifndef OPENSSL_1_1
        CRYPTO_set_locking_callback(ssl_locking_function);
        CRYPTO_set_dynlock_create_callback(ssl_create_dynlock);
        CRYPTO_set_dynlock_destroy_callback(ssl_free_dynlock);
        CRYPTO_set_dynlock_lock_callback(ssl_lock_dynlock);
#ifdef OPENSSL_1_0
        CRYPTO_THREADID_set_callback(maxscale_ssl_id);
#else
        CRYPTO_set_id_callback(pthread_self);
#endif
#endif
    }

    return initialized;
}

static void finish_ssl()
{
    ERR_free_strings();
    EVP_cleanup();

#ifndef OPENSSL_1_1
    int numlocks = CRYPTO_num_locks();
    for (int i = 0; i < numlocks + 1; i++)
    {
        pthread_mutex_destroy(&this_unit.ssl_locks[i]);
    }

    MXS_FREE(this_unit.ssl_locks);
    this_unit.ssl_locks = nullptr;
#endif
}

static bool init_base_libraries()
{
    bool initialized = false;

    if (init_ssl())
    {
        if (init_sqlite3())
        {
            if (maxbase::init())
            {
                initialized = true;
            }
            else
            {
                log_startup_error("Failed to initialize MaxScale base library.");

                // No finalization of sqlite3
                finish_ssl();
            }
        }
        else
        {
            log_startup_error("Failed to initialize sqlite3.");

            finish_ssl();
        }
    }
    else
    {
        log_startup_error("Failed to initialize SSL.");
    }

    return initialized;
}

static void finish_base_libraries()
{
    maxbase::finish();
    // No finalization of sqlite3
    finish_ssl();
}

static bool redirect_stdout_and_stderr(const std::string& path)
{
    bool rv = false;

    if (freopen(path.c_str(), "a", stdout))
    {
        if (freopen(path.c_str(), "a", stderr))
        {
            rv = true;
        }
        else
        {
            // The state of stderr is now somewhat unclear. We log nonetheless.
            log_startup_error(errno, "Failed to redirect stderr to file");
        }
    }
    else
    {
        log_startup_error(errno,
                          "Failed to redirect stdout (and will not attempt to redirect stderr) to file");
    }

    return rv;
}

static bool is_maxscale_already_running()
{
    bool rv = true;

    if (!pid_file_exists())
    {
        if (write_pid_file() == 0)
        {
            atexit(unlink_pidfile);

            if (lock_directories())
            {
                atexit(unlock_directories);

                rv = false;
            }
        }
    }

    return rv;
}
