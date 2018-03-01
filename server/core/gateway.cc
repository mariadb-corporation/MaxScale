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
 * @file gateway.c - The gateway entry point.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 23-05-2013   Massimiliano Pinto      epoll loop test
 * 12-06-2013   Mark Riddoch            Add the -p option to set the
 *                                      listening port
 *                                      and bind addr is 0.0.0.0
 * 19/06/13     Mark Riddoch            Extract the epoll functionality
 * 21/06/13     Mark Riddoch            Added initial config support
 * 27/06/13
 * 28/06/13     Vilho Raatikka          Added necessary headers, example functions and
 *                                      calls to log manager and to query classifier.
 *                                      Put example code behind SS_DEBUG macros.
 * 05/02/14     Mark Riddoch            Addition of version string
 * 29/06/14     Massimiliano Pinto      Addition of pidfile
 * 10/08/15     Markus Makela           Added configurable directory locations
 * 19/01/16     Markus Makela           Set cwd to log directory
 * @endverbatim
 */

#include <maxscale/cdefs.h>

#include <execinfo.h>
#include <ftw.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <ini.h>
#include <openssl/opensslconf.h>
#include <pwd.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/housekeeper.h>
#include <maxscale/log_manager.h>
#include <maxscale/maxscale.h>
#include <maxscale/paths.h>
#include <maxscale/query_classifier.h>
#include <maxscale/server.h>
#include <maxscale/session.h>
#include <maxscale/thread.h>
#include <maxscale/utils.h>
#include <maxscale/version.h>
#include <maxscale/random_jkiss.h>

#include "maxscale/config.h"
#include "maxscale/maxscale.h"
#include "maxscale/modules.h"
#include "maxscale/monitor.h"
#include "maxscale/poll.h"
#include "maxscale/service.h"
#include "maxscale/statistics.h"

#define STRING_BUFFER_SIZE 1024
#define PIDFD_CLOSED -1

/** for procname */
#if !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif

#if defined(OPENSSL_THREADS)
#define HAVE_OPENSSL_THREADS 1
#else
#define HAVE_OPENSSL_THREADS 0
#endif

extern char *program_invocation_name;
extern char *program_invocation_short_name;

/* The data directory we created for this gateway instance */
static char     datadir[PATH_MAX + 1] = "";
static bool     datadir_defined = false; /*< If the datadir was already set */
/* The data directory we created for this gateway instance */
static char     pidfile[PATH_MAX + 1] = "";
static int pidfd = PIDFD_CLOSED;

/**
 * exit flag for log flusher.
 */
static bool do_exit = false;

/**
 * If MaxScale is started to run in daemon process the value is true.
 */
static bool     daemon_mode = true;

static const char* maxscale_commit = MAXSCALE_COMMIT;

const char *progname = NULL;
static struct option long_options[] =
{
    {"config-check",     no_argument,       0, 'c'},
    {"nodaemon",         no_argument,       0, 'd'},
    {"config",           required_argument, 0, 'f'},
    {"log",              required_argument, 0, 'l'},
    {"logdir",           required_argument, 0, 'L'},
    {"cachedir",         required_argument, 0, 'A'},
    {"libdir",           required_argument, 0, 'B'},
    {"configdir",        required_argument, 0, 'C'},
    {"datadir",          required_argument, 0, 'D'},
    {"execdir",          required_argument, 0, 'E'},
    {"persistdir",       required_argument, 0, 'F'},
    {"module_configdir", required_argument, 0, 'M'},
    {"language",         required_argument, 0, 'N'},
    {"piddir",           required_argument, 0, 'P'},
    {"basedir",          required_argument, 0, 'R'},
    {"user",             required_argument, 0, 'U'},
    {"syslog",           required_argument, 0, 's'},
    {"maxlog",           required_argument, 0, 'S'},
    {"log_augmentation", required_argument, 0, 'G'},
    {"version",          no_argument,       0, 'v'},
    {"version-full",     no_argument,       0, 'V'},
    {"help",             no_argument,       0, '?'},
    {"connector_plugindir", required_argument, 0, 'H'},
    {0, 0, 0, 0}
};
static bool syslog_configured = false;
static bool maxlog_configured = false;
static bool log_to_shm_configured = false;
static volatile sig_atomic_t  last_signal = 0;

static int cnf_preparser(void* data, const char* section, const char* name, const char* value);
static void log_flush_shutdown(void);
static void log_flush_cb(void* arg);
static int write_pid_file(); /* write MaxScale pidfile */
static void unlink_pidfile(void); /* remove pidfile */
static void unlock_pidfile();
static bool file_write_header(FILE* outfile);
static bool file_write_footer(FILE* outfile);
static void write_footer(void);
static int ntfw_cb(const char*, const struct stat*, int, struct FTW*);
static bool is_file_and_readable(const char* absolute_pathname);
static bool path_is_readable(const char* absolute_pathname);
static bool path_is_writable(const char* absolute_pathname);
bool handle_path_arg(char** dest, const char* path, const char* arg, bool rd, bool wr);
static void set_log_augmentation(const char* value);
static void usage(void);
static char* get_expanded_pathname(
    char** abs_path,
    const char* input_path,
    const char* fname);
static void print_log_n_stderr(
    bool do_log,
    bool do_stderr,
    const char* logstr,
    const char* fprstr,
    int eno);
static bool resolve_maxscale_conf_fname(
    char** cnf_full_path,
    const char* home_dir,
    char* cnf_file_arg);

static char* check_dir_access(char* dirname, bool, bool);
static int set_user(const char* user);
bool pid_file_exists();
void write_child_exit_code(int fd, int code);
static bool change_cwd();
static void log_exit_status();
static bool daemonize();
static bool sniff_configuration(const char* filepath);
static bool modules_process_init();
static void modules_process_finish();
static bool modules_thread_init();
static void modules_thread_finish();

#ifndef OPENSSL_1_1
/** SSL multi-threading functions and structures */

static SPINLOCK* ssl_locks;

static void ssl_locking_function(int mode, int n, const char* file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        spinlock_acquire(&ssl_locks[n]);
    }
    else
    {
        spinlock_release(&ssl_locks[n]);
    }
}
/**
 * OpenSSL requires this struct to be defined in order to use dynamic locks
 */
struct CRYPTO_dynlock_value
{
    SPINLOCK lock;
};

/**
 * Create a dynamic OpenSSL lock. The dynamic lock is just a wrapper structure
 * around a SPINLOCK structure.
 * @param file File name
 * @param line Line number
 * @return Pointer to new lock or NULL of an error occurred
 */
static struct CRYPTO_dynlock_value *ssl_create_dynlock(const char* file, int line)
{
    struct CRYPTO_dynlock_value* lock =
        (struct CRYPTO_dynlock_value*) MXS_MALLOC(sizeof(struct CRYPTO_dynlock_value));
    if (lock)
    {
        spinlock_init(&lock->lock);
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
static void ssl_lock_dynlock(int mode, struct CRYPTO_dynlock_value * n, const char* file, int line)
{
    if (mode & CRYPTO_LOCK)
    {
        spinlock_acquire(&n->lock);
    }
    else
    {
        spinlock_release(&n->lock);
    }
}

/**
 * Free a dynamic OpenSSL lock.
 * @param n Lock to free
 * @param file File name
 * @param line Line number
 */
static void ssl_free_dynlock(struct CRYPTO_dynlock_value * n, const char* file, int line)
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
 * Handler for SIGHUP signal. Reload the configuration for the
 * gateway.
 */
static void sighup_handler (int i)
{
    MXS_NOTICE("Refreshing configuration following SIGHUP\n");
    config_reload();
}

/**
 * Handler for SIGUSR1 signal. A SIGUSR1 signal will cause
 * maxscale to rotate all log files.
 */
static void sigusr1_handler (int i)
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
    last_signal = i;
    int n_shutdowns = maxscale_shutdown();

    if (n_shutdowns == 1)
    {
        if (write(STDERR_FILENO, shutdown_msg, sizeof(shutdown_msg) - 1) == -1)
        {
            printf("Failed to write shutdown message!\n");
        }
    }
    else
    {
        exit(EXIT_FAILURE);
    }
}

static void
sigint_handler(int i)
{
    last_signal = i;
    int n_shutdowns = maxscale_shutdown();

    if (n_shutdowns == 1)
    {
        if (write(STDERR_FILENO, shutdown_msg, sizeof(shutdown_msg) - 1) == -1)
        {
            printf("Failed to write shutdown message!\n");
        }
    }
    else if (n_shutdowns == 2)
    {
        if (write(STDERR_FILENO, patience_msg, sizeof(patience_msg) - 1) == -1)
        {
            printf("Failed to write shutdown message!\n");
        }
    }
    else
    {
        exit(EXIT_FAILURE);
    }
}

static void
sigchld_handler (int i)
{
    int exit_status = 0;
    pid_t child = -1;

    if ((child = wait(&exit_status)) == -1)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to wait child process: %d %s",
                  errno, strerror_r(errno, errbuf, sizeof(errbuf)));
    }
    else
    {
        if (WIFEXITED(exit_status))
        {
            if (WEXITSTATUS(exit_status) != 0)
            {
                MXS_ERROR("Child process %d exited with status %d",
                          child, WEXITSTATUS(exit_status));
            }
            else
            {
                MXS_INFO("Child process %d exited with status %d",
                         child, WEXITSTATUS(exit_status));
            }
        }
        else if (WIFSIGNALED(exit_status))
        {
            MXS_ERROR("Child process %d was stopped by signal %d.",
                      child, WTERMSIG(exit_status));
        }
        else
        {
            MXS_ERROR("Child process %d did not exit normally. Exit status: %d",
                      child, exit_status);
        }
    }
}

volatile sig_atomic_t fatal_handling = 0;

static int signal_set(int sig, void (*handler)(int));

static void
sigfatal_handler(int i)
{
    if (fatal_handling)
    {
        fprintf(stderr, "Fatal signal %d while backtracing\n", i);
        _exit(1);
    }
    fatal_handling = 1;
    MXS_CONFIG* cnf = config_get_global_options();
    fprintf(stderr, "Fatal: MaxScale " MAXSCALE_VERSION " received fatal signal %d. "
            "Attempting backtrace.\n", i);
    fprintf(stderr, "Commit ID: %s System name: %s Release string: %s\n\n",
            maxscale_commit, cnf->sysname, cnf->release_string);
    void *addrs[128];
    int count = backtrace(addrs, 128);

    // First print the stack trace to stderr as malloc is likely broken
    backtrace_symbols_fd(addrs, count, STDERR_FILENO);

    MXS_ALERT("Fatal: MaxScale " MAXSCALE_VERSION " received fatal signal %d. "
              "Attempting backtrace.", i);
    MXS_ALERT("Commit ID: %s System name: %s "
              "Release string: %s",
              maxscale_commit, cnf->sysname, cnf->release_string);
    // Then see if we can log them
    char** symbols = backtrace_symbols(addrs, count);

    if (symbols)
    {
        for (int n = 0; n < count; n++)
        {
            MXS_ALERT("  %s\n", symbols[n]);
        }
        MXS_FREE(symbols);
    }

    mxs_log_flush_sync();

    /* re-raise signal to enforce core dump */
    fprintf(stderr, "\n\nWriting core dump\n");
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
static int signal_set(int sig, void (*handler)(int))
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
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed call sigaction() in %s due to %d, %s.",
                  program_invocation_short_name,
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
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

    if ((len = snprintf(datadir, PATH_MAX, "%s", base)) < PATH_MAX &&
        (mkdir(datadir, 0777) == 0 || errno == EEXIST))
    {
        if ((len = snprintf(datadir, PATH_MAX, "%s/data%d", base, getpid())) < PATH_MAX)
        {
            if ((mkdir(datadir, 0777) == 0) || (errno == EEXIST))
            {
                created = true;
            }
            else
            {
                char errbuf[MXS_STRERROR_BUFLEN];
                MXS_ERROR("Cannot create data directory '%s': %d %s\n",
                          datadir, errno, strerror_r(errno, errbuf, sizeof(errbuf)));
            }
        }
    }
    else
    {
        if (len < PATH_MAX)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            fprintf(stderr, "Error: Cannot create data directory '%s': %d %s\n",
                    datadir, errno, strerror_r(errno, errbuf, sizeof(errbuf)));
        }
        else
        {
            MXS_ERROR("Data directory pathname exceeds the maximum allowed pathname "
                      "length: %s/data%d.", base, getpid());
        }
    }

    return created;
}

/**
 * Cleanup the temporary data directory we created for the gateway
 */
int ntfw_cb(const char*        filename,
            const struct stat* filestat,
            int                fileflags,
            struct FTW*        pfwt)
{
    int rc = remove(filename);

    if (rc != 0)
    {
        int eno = errno;
        errno = 0;
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to remove the data directory %s of MaxScale due to %d, %s.",
                  datadir, eno, strerror_r(eno, errbuf, sizeof(errbuf)));
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
    const char *proc_datadir = get_process_datadir();

    if (strcmp(proc_datadir, get_datadir()) != 0 && access(proc_datadir, F_OK) == 0)
    {
        nftw(proc_datadir, ntfw_cb, depth, flags);
    }
}

static void write_footer(void)
{
    file_write_footer(stdout);
}

static bool file_write_footer(FILE* outfile)
{
    bool        succp = false;
    size_t      len1;
    const char* header_buf1;

    header_buf1 = "------------------------------------------------------"
                  "\n\n";
    len1 = strlen(header_buf1);
    fwrite((void*)header_buf1, len1, 1, outfile);

    succp = true;

    return succp;
}

// Documentation says 26 bytes is enough, but 32 is a nice round number.
#define ASCTIME_BUF_LEN 32
static bool file_write_header(FILE* outfile)
{
    bool        succp = false;
    size_t      len1;
    size_t      len2;
    size_t      len3;
    const char* header_buf1;
    char        header_buf2[ASCTIME_BUF_LEN];
    const char* header_buf3;
    time_t      t;
    struct tm   tm;
#if defined(LAPTOP_TEST)
    struct timespec ts1;
    ts1.tv_sec = 0;
    ts1.tv_nsec = DISKWRITE_LATENCY * 1000000;
#endif

#if !defined(SS_DEBUG)
    return true;
#endif

    t = time(NULL);
    localtime_r(&t, &tm);

    header_buf1 = "\n\nMariaDB Corporation MaxScale " MAXSCALE_VERSION "\t";
    asctime_r(&tm, header_buf2);
    header_buf3 = "------------------------------------------------------\n";

    len1 = strlen(header_buf1);
    len2 = strlen(header_buf2);
    len3 = strlen(header_buf3);
#if defined(LAPTOP_TEST)
    nanosleep(&ts1, NULL);
#else
    fwrite((void*)header_buf1, len1, 1, outfile);
    fwrite((void*)header_buf2, len2, 1, outfile);
    fwrite((void*)header_buf3, len3, 1, outfile);
#endif

    succp = true;

    return succp;
}

static bool resolve_maxscale_conf_fname(char** cnf_full_path,
                                        const char* home_dir,
                                        char* cnf_file_arg)
{
    if (cnf_file_arg)
    {
        *cnf_full_path = (char *)MXS_MALLOC(PATH_MAX + 1);
        MXS_ABORT_IF_NULL(*cnf_full_path);

        if (!realpath(cnf_file_arg, *cnf_full_path))
        {
            const char* logstr = "Failed to open read access to configuration file.";
            int eno = errno;
            print_log_n_stderr(true, true, logstr, logstr, eno);
            MXS_FREE(*cnf_full_path);
            *cnf_full_path = NULL;
        }
    }
    else /*< default config file name is used */
    {
        *cnf_full_path = get_expanded_pathname(NULL, home_dir, default_cnf_fname);
    }

    return *cnf_full_path && is_file_and_readable(*cnf_full_path);
}

/**
 * Check read and write accessibility to a directory.
 * @param dirname       directory to be checked
 *
 * @return NULL if directory can be read and written, an error message if either
 *      read or write is not permitted.
 */
static char* check_dir_access(char* dirname, bool rd, bool wr)
{
    char errbuf[PATH_MAX * 2];
    char* errstr = NULL;

    if (dirname == NULL)
    {
        errstr = MXS_STRDUP_A("Directory argument is NULL");
        goto retblock;
    }

    if (access(dirname, F_OK) != 0)
    {
        snprintf(errbuf, PATH_MAX * 2 - 1, "Can't access '%s'.", dirname);
        errbuf[PATH_MAX * 2 - 1] = '\0';
        errstr = MXS_STRDUP_A(errbuf);
        goto retblock;
    }

    if (rd && !path_is_readable(dirname))
    {
        snprintf(errbuf, PATH_MAX * 2 - 1, "MaxScale doesn't have read permission "
                 "to '%s'.", dirname);
        errbuf[PATH_MAX * 2 - 1] = '\0';
        errstr = MXS_STRDUP_A(errbuf);
        goto retblock;
    }

    if (wr && !path_is_writable(dirname))
    {
        snprintf(errbuf, PATH_MAX * 2 - 1, "MaxScale doesn't have write permission "
                 "to '%s'.", dirname);
        errbuf[PATH_MAX * 2 - 1] = '\0';
        errstr = MXS_STRDUP_A(errbuf);
        goto retblock;
    }

retblock:
    return errstr;
}


/**
 * @node Provides error printing for non-formatted error strings.
 *
 * @param do_log Specifies whether printing to log is enabled
 *
 * @param do_stderr Specifies whether printing to stderr is enabled
 *
 * @param logstr String to be printed to log
 *
 * @param fprstr String to be printed to stderr
 *
 * @param eno Errno, if it is set, zero, otherwise
 */
static void print_log_n_stderr(
    bool        do_log,   /*< is printing to log enabled */
    bool        do_stderr,/*< is printing to stderr enabled */
    const char* logstr,   /*< string to be printed to log */
    const char* fprstr,   /*< string to be printed to stderr */
    int         eno)      /*< errno, if it is set, zero, otherwise */
{
    if (do_log)
    {
        if (mxs_log_init(NULL, get_logdir(), MXS_LOG_TARGET_FS))
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_ERROR("%s%s%s%s",
                      logstr,
                      eno == 0 ? "" : " (",
                      eno == 0 ? "" : strerror_r(eno, errbuf, sizeof(errbuf)),
                      eno == 0 ? "" : ")");
        }
    }
    if (do_stderr)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr,
                "* Error: %s%s%s%s\n",
                fprstr,
                eno == 0 ? "" : " (",
                eno == 0 ? "" : strerror_r(eno, errbuf, sizeof(errbuf)),
                eno == 0 ? "" : ")");
    }
}

/**
 * Check that a path refers to a readable file.
 *
 * @param absolute_pathname The path to check.
 * @return True if the path refers to a readable file. is readable
 */
static bool is_file_and_readable(const char* absolute_pathname)
{
    bool rv = false;

    struct stat info;

    if (stat(absolute_pathname, &info) == 0)
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
            const char FORMAT[] = "'%s' does not refer to a regular file.";
            char buff[sizeof(FORMAT) + strlen(absolute_pathname)];
            snprintf(buff, sizeof(buff), FORMAT, absolute_pathname);
            print_log_n_stderr(true, true, buff, buff, 0);
        }
    }
    else
    {
        int eno = errno;
        errno = 0;
        const char FORMAT[] = "Could not access '%s'.";
        char buff[sizeof(FORMAT) + strlen(absolute_pathname)];
        snprintf(buff, sizeof(buff), FORMAT, absolute_pathname);
        print_log_n_stderr(true, true, buff, buff, eno);
    }

    return rv;
}

/**
 * Check if the file or directory is readable
 * @param absolute_pathname Path of the file or directory to check
 * @return True if file is readable
 */
static bool path_is_readable(const char* absolute_pathname)
{
    bool succp = true;

    if (access(absolute_pathname, R_OK) != 0)
    {
        int eno = errno;
        errno = 0;
        char buff[PATH_MAX + 100];
        snprintf(buff, sizeof(buff), "Opening file '%s' for reading failed.", absolute_pathname);
        print_log_n_stderr(true, true, buff, buff, eno);
        succp = false;
    }
    return succp;
}

/**
 * Check if the file or directory is writable
 * @param absolute_pathname Path of the file or directory to check
 * @return True if file is writable
 */
static bool path_is_writable(const char* absolute_pathname)
{
    bool succp = true;

    if (access(absolute_pathname, W_OK) != 0)
    {
        int eno = errno;
        errno = 0;
        char buff[PATH_MAX + 100];
        snprintf(buff, sizeof(buff), "Opening file '%s' for writing failed.", absolute_pathname);
        print_log_n_stderr(true, true, buff, buff, eno);
        succp = false;
    }
    return succp;
}


/**
 * @node Expand path expression and if fname is provided, concatenate
 * it to path to for an absolute pathname. If fname is provided
 * its readability is tested.
 *
 * Parameters:
 * @param output_path memory address where expanded path is stored,
 * if output_path != NULL
 *
 * @param relative_path path to be expanded
 *
 * @param fname file name to be concatenated to the path, may be NULL
 *
 * @return expanded path and if fname was NULL, absolute pathname of it.
 * Both return value and *output_path are NULL in case of failure.
 *
 *
 */
static char* get_expanded_pathname(char** output_path,
                                   const char* relative_path,
                                   const char* fname)
{
    char* cnf_file_buf = NULL;
    char* expanded_path;

    if (relative_path == NULL)
    {
        goto return_cnf_file_buf;
    }

    expanded_path = (char*)MXS_MALLOC(PATH_MAX);

    if (!expanded_path)
    {
        if (output_path)
        {
            *output_path = NULL;
        }
        goto return_cnf_file_buf;
    }

    /*<
     * Expand possible relative pathname to absolute path
     */
    if (realpath(relative_path, expanded_path) == NULL)
    {
        int eno = errno;
        errno = 0;
        char buff[PATH_MAX + 100];

        snprintf(buff, sizeof(buff), "Failed to read the directory '%s'.",  relative_path);
        print_log_n_stderr(true, true, buff, buff, eno);

        MXS_FREE(expanded_path);
        if (output_path)
        {
            *output_path = NULL;
        }
        goto return_cnf_file_buf;
    }

    if (fname != NULL)
    {
        /*<
         * Concatenate an absolute filename and test its existence and
         * readability.
         */
        size_t pathlen = strnlen(expanded_path, PATH_MAX) +
                         1 + strnlen(fname, PATH_MAX) + 1;
        cnf_file_buf = (char*)MXS_MALLOC(pathlen);

        if (cnf_file_buf == NULL)
        {
            MXS_FREE(expanded_path);
            expanded_path = NULL;
            goto return_cnf_file_buf;
        }
        snprintf(cnf_file_buf, pathlen, "%s/%s", expanded_path, fname);

        if (!path_is_readable(cnf_file_buf))
        {
            MXS_FREE(expanded_path);
            MXS_FREE(cnf_file_buf);
            expanded_path = NULL;
            cnf_file_buf = NULL;
            goto return_cnf_file_buf;
        }
    }
    else
    {
        /*<
         * If only directory was provided, check that it is
         * readable.
         */
        if (!path_is_readable(expanded_path))
        {
            MXS_FREE(expanded_path);
            expanded_path = NULL;
            goto return_cnf_file_buf;
        }
    }

    if (output_path == NULL)
    {
        MXS_FREE(expanded_path);
    }
    else
    {
        *output_path = expanded_path;
    }

return_cnf_file_buf:

    return cnf_file_buf;
}

static void usage(void)
{
    fprintf(stderr,
            "\nUsage : %s [OPTION]...\n\n"
            "  -c, --config-check          validate configuration file and exit\n"
            "  -d, --nodaemon              enable running in terminal process\n"
            "  -f, --config=FILE           relative or absolute pathname of config file\n"
            "  -l, --log=[file|shm|stdout] log to file, shared memory or stdout\n"
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
            "  -N, --language=PATH         path to errmsg.sys file\n"
            "  -P, --piddir=PATH           path to PID file directory\n"
            "  -R, --basedir=PATH          base path for all other paths\n"
            "  -U, --user=USER             user ID and group ID of specified user are used to\n"
            "                              run MaxScale\n"
            "  -s, --syslog=[yes|no]       log messages to syslog (default:yes)\n"
            "  -S, --maxlog=[yes|no]       log messages to MaxScale log (default: yes)\n"
            "  -G, --log_augmentation=0|1  augment messages with the name of the function\n"
            "                              where the message was logged (default: 0)\n"
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
            "MaxScale documentation: https://mariadb.com/kb/en/mariadb-enterprise/mariadb-maxscale-21/ \n",
            progname,
            get_configdir(), default_cnf_fname,
            get_configdir(), get_logdir(), get_cachedir(), get_libdir(),
            get_datadir(), get_execdir(), get_langdir(), get_piddir(),
            get_config_persistdir(), get_module_configdir(), get_connector_plugindir());
}


/**
 * The entry point of each worker thread.
 *
 * @param arg The thread argument.
 */
void worker_thread_main(void* arg)
{
    if (qc_thread_init(QC_INIT_SELF))
    {
        if (modules_thread_init())
        {
            poll_waitevents(arg);

            modules_thread_finish();
        }
        else
        {
            MXS_ERROR("Could not perform thread initialization for all modules. Thread exits.");
        }

        qc_thread_end(QC_INIT_SELF);
    }
    else
    {
        MXS_ERROR("Could not perform thread initialization for the "
                  "internal query classifier. Thread exits.");
    }
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
        int e = errno;
        errno = 0;

        static const char FORMAT[] = "Failed to delete signal %s from the signal set of MaxScale. Exiting.";
        char message[sizeof(FORMAT) + 16]; // Enough for any "SIG..." string.

        sprintf(message, FORMAT, signame);

        print_log_n_stderr(true, true, message, message, e);
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
        int e = errno;
        errno = 0;
        static const char message[] = "Failed to initialize set the signal set for MaxScale. Exiting.";
        print_log_n_stderr(true, true, message, message, e);
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
        int e = errno;
        errno = 0;
        static const char message[] = "Failed to set the signal set for MaxScale. Exiting";
        print_log_n_stderr(true, true, message, message, e);
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
static bool configure_signal(int signum, const char* signame, void (*handler)(int))
{
    int rc = signal_set(signum, handler);

    if (rc != 0)
    {
        static const char FORMAT[] = "Failed to set signal handler for %s. Exiting.";
        char message[sizeof(FORMAT) + 16]; // Enough for any "SIG..." string.

        sprintf(message, FORMAT, signame);

        print_log_n_stderr(true, true, message, message, 0);
    }

    return rc == 0;
}

/**
 * Configures signal handling of MaxScale.
 *
 * @return True, if all signals could be configured, false otherwise.
 */
bool configure_signals(void)
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

    if (!configure_signal(SIGCHLD, "SIGCHLD", sigchld_handler))
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
 * Set the directories of MaxScale relative to a basedir
 *
 * @param basedir The base directory relative to which the other are set.
 *
 * @return True if the directories could be set, false otherwise.
 */
bool set_dirs(const char *basedir)
{
    bool rv = true;
    char *path;

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_LOG_SUBPATH, true, false)))
    {
        set_logdir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_CACHE_SUBPATH, true, true)))
    {
        set_cachedir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_LIB_SUBPATH, true, false)))
    {
        set_libdir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_CONFIG_SUBPATH, true, false)))
    {
        set_configdir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_MODULE_CONFIG_SUBPATH, true, false)))
    {
        set_module_configdir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_DATA_SUBPATH, true, false)))
    {
        set_datadir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_EXEC_SUBPATH, true, false)))
    {
        set_execdir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_LANG_SUBPATH, true, false)))
    {
        set_langdir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir, "var/" MXS_DEFAULT_PID_SUBPATH, true, true)))
    {
        set_piddir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir, MXS_DEFAULT_DATA_SUBPATH "/"
                                    MXS_DEFAULT_CONFIG_PERSIST_SUBPATH, true, true)))
    {
        set_config_persistdir(path);
    }

    if (rv && (rv = handle_path_arg(&path, basedir,
                                    "var/" MXS_DEFAULT_CONNECTOR_PLUGIN_SUBPATH, true, true)))
    {
        set_connector_plugindir(path);
    }

    return rv;
}

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
int main(int argc, char **argv)
{
    int      rc = MAXSCALE_SHUTDOWN;
    int      l;
    int      i;
    intptr_t thread_id;
    int      n_threads; /*< number of epoll listener threads */
    int      n_services;
    int      eno = 0;   /*< local variable for errno */
    int      opt;
    int      daemon_pipe[2] = { -1, -1};
    bool     parent_process;
    int      child_status;
    THREAD*   threads = NULL;   /*< thread list */
    char*    cnf_file_path = NULL;        /*< conf file, to be freed */
    char*    cnf_file_arg = NULL;         /*< conf filename from cmd-line arg */
    THREAD    log_flush_thr;
    char*    tmp_path;
    int      option_index;
    MXS_CONFIG* cnf = config_get_global_options();
    ss_dassert(cnf);
    int      *syslog_enabled = &cnf->syslog; /** Log to syslog */
    int      *maxlog_enabled = &cnf->maxlog; /** Log with MaxScale */
    int      *log_to_shm = &cnf->log_to_shm; /** Log to shared memory */
    ssize_t  log_flush_timeout_ms = 0;
    sigset_t sigpipe_mask;
    sigset_t saved_mask;
    bool to_stdout = false;
    void   (*exitfunp[4])(void) = { mxs_log_finish, cleanup_process_datadir, write_footer, NULL };
    int numlocks = 0;
    bool pid_file_created = false;

    // NOTE: These are set here since global_defaults() is called inside config_load().
    *syslog_enabled = 1;
    *maxlog_enabled = 1;
    *log_to_shm = 0;
    cnf->config_check = false;

    maxscale_reset_starttime();

    sigemptyset(&sigpipe_mask);
    sigaddset(&sigpipe_mask, SIGPIPE);
    progname = *argv;
    snprintf(datadir, PATH_MAX, "%s", default_datadir);
    datadir[PATH_MAX] = '\0';
    file_write_header(stderr);
    /*<
     * Register functions which are called at exit.
     */
    for (i = 0; exitfunp[i] != NULL; i++)
    {
        l = atexit(*exitfunp);

        if (l != 0)
        {
            const char* fprerr = "Failed to register exit functions for MaxScale";
            print_log_n_stderr(false, true, NULL, fprerr, 0);
            rc = MAXSCALE_INTERNALERROR;
            goto return_main;
        }
    }

    while ((opt = getopt_long(argc, argv, "dcf:l:vVs:S:?L:D:C:B:U:A:P:G:N:E:F:M:H:",
                              long_options, &option_index)) != -1)
    {
        bool succp = true;

        switch (opt)
        {
        case 'd':
            /*< Debug mode, maxscale runs in this same process */
            daemon_mode = false;
            break;

        case 'f':
            /*<
             * Simply copy the conf file argument. Expand or validate
             * it when MaxScale home directory is resolved.
             */
            if (optarg[0] != '-')
            {
                cnf_file_arg = strndup(optarg, PATH_MAX);
            }
            if (cnf_file_arg == NULL)
            {
                const char* logerr =
                    "Configuration file argument "
                    "identifier \'-f\' was specified but "
                    "the argument didn't specify\n  a valid "
                    "configuration file or the argument "
                    "was missing.";
                print_log_n_stderr(true, true, logerr, logerr, 0);
                usage();
                succp = false;
            }
            break;

        case 'v':
            rc = EXIT_SUCCESS;
            printf("MaxScale %s\n", MAXSCALE_VERSION);
            goto return_main;

        case 'V':
            rc = EXIT_SUCCESS;
            printf("MaxScale %s - %s\n", MAXSCALE_VERSION, maxscale_commit);
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
            goto return_main;

        case 'l':
            if (strncasecmp(optarg, "file", PATH_MAX) == 0)
            {
                *log_to_shm = false;
                log_to_shm_configured = true;
            }
            else if (strncasecmp(optarg, "shm", PATH_MAX) == 0)
            {
                *log_to_shm = true;
                log_to_shm_configured = true;
            }
            else if (strncasecmp(optarg, "stdout", PATH_MAX) == 0)
            {
                to_stdout = true;
                *log_to_shm = false;
                log_to_shm_configured = true;
            }
            else
            {
                const char* logerr =
                    "Configuration file argument "
                    "identifier \'-l\' was specified but "
                    "the argument didn't specify\n  a valid "
                    "configuration file or the argument "
                    "was missing.";
                print_log_n_stderr(true, true, logerr, logerr, 0);
                usage();
                succp = false;
            }
            break;
        case 'L':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, false))
            {
                set_logdir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;
        case 'N':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, false))
            {
                set_langdir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;
        case 'P':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, true))
            {
                set_piddir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;
        case 'D':
            snprintf(datadir, PATH_MAX, "%s", optarg);
            datadir[PATH_MAX] = '\0';
            set_datadir(MXS_STRDUP_A(optarg));
            datadir_defined = true;
            break;
        case 'C':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, false))
            {
                set_configdir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;
        case 'B':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, false))
            {
                set_libdir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;
        case 'A':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, true))
            {
                set_cachedir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;

        case 'E':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, false))
            {
                set_execdir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;
        case 'H':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, false))
            {
                set_connector_plugindir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;
        case 'F':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, true))
            {
                set_config_persistdir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;

        case 'M':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, true))
            {
                set_module_configdir(tmp_path);
            }
            else
            {
                succp = false;
            }
            break;

        case 'R':
            if (handle_path_arg(&tmp_path, optarg, NULL, true, false))
            {
                succp = set_dirs(tmp_path);
                free(tmp_path);
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
                        *maxlog_enabled = config_truth_value(tok);
                        maxlog_configured = true;
                    }
                }
                else
                {
                    *maxlog_enabled = config_truth_value(optarg);
                    maxlog_configured = true;
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
                        *syslog_enabled = config_truth_value(tok);
                        syslog_configured = true;
                    }
                }
                else
                {
                    *syslog_enabled = config_truth_value(optarg);
                    syslog_configured = true;
                }
            }
            break;
        case 'U':
            if (set_user(optarg) != 0)
            {
                succp = false;
            }
            break;
        case 'G':
            set_log_augmentation(optarg);
            break;
        case '?':
            usage();
            rc = EXIT_SUCCESS;
            goto return_main;

        case 'c':
            cnf->config_check = true;
            break;

        default:
            usage();
            succp = false;
            break;
        }

        if (!succp)
        {
            rc = MAXSCALE_BADARG;
            goto return_main;
        }
    }

    if (cnf->config_check)
    {
        daemon_mode = false;
        to_stdout = true;
    }

    if (!daemon_mode)
    {
        fprintf(stderr,
                "Info : MaxScale will be run in the terminal process.\n");
#if defined(SS_DEBUG)
        fprintf(stderr,
                "\tSee "
                "the log from the following log files : \n\n");
#endif
    }
    else
    {
        if (pipe(daemon_pipe) == -1)
        {
            fprintf(stderr, "Error: Failed to create pipe for inter-process communication: %d %s",
                    errno, strerror(errno));
            rc = MAXSCALE_INTERNALERROR;
            goto return_main;
        }

        /*<
         * Maxscale must be daemonized before opening files, initializing
         * embedded MariaDB and in general, as early as possible.
         */

        if (!disable_signals())
        {
            rc = MAXSCALE_INTERNALERROR;
            goto return_main;
        }

        /** Daemonize the process and wait for the child process to notify
         * the parent process of its exit status. */
        parent_process = daemonize();

        if (parent_process)
        {
            close(daemon_pipe[1]);
            int nread = read(daemon_pipe[0], (void*)&child_status, sizeof(int));
            close(daemon_pipe[0]);

            if (nread == -1)
            {
                const char* logerr = "Failed to read data from child process pipe.";
                print_log_n_stderr(true, true, logerr, logerr, errno);
                exit(MAXSCALE_INTERNALERROR);
            }
            else if (nread == 0)
            {
                /** Child process has exited or closed write pipe */
                const char* logerr = "No data read from child process pipe.";
                print_log_n_stderr(true, true, logerr, logerr, 0);
                exit(MAXSCALE_INTERNALERROR);
            }

            exit(child_status);
        }

        /** This is the child process and we can close the read end of
         * the pipe. */
        close(daemon_pipe[0]);
    }
    /*<
     * Set signal handlers for SIGHUP, SIGTERM, SIGINT and critical signals like SIGSEGV.
     */
    if (!configure_signals())
    {
        static const char* logerr = "Failed to configure signal handlers. Exiting.";

        print_log_n_stderr(true, true, logerr, logerr, 0);
        rc = MAXSCALE_INTERNALERROR;
        goto return_main;
    }
    eno = pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask);

    if (eno != 0)
    {
        const char* logerr = "Failed to initialise signal mask for MaxScale. Exiting.";
        print_log_n_stderr(true, true, logerr, logerr, eno);
        rc = MAXSCALE_INTERNALERROR;
        goto return_main;
    }

    /** Initialize the random number generator */
    random_jkiss_init();

    if (!utils_init())
    {
        const char* logerr = "Failed to initialise utility library.";
        print_log_n_stderr(true, true, logerr, logerr, eno);
        rc = MAXSCALE_INTERNALERROR;
        goto return_main;
    }

    /** OpenSSL initialization */
    if (!HAVE_OPENSSL_THREADS)
    {
        const char* logerr = "OpenSSL library does not support multi-threading";
        print_log_n_stderr(true, true, logerr, logerr, eno);
        rc = MAXSCALE_INTERNALERROR;
        goto return_main;
    }
    SSL_library_init();
    SSL_load_error_strings();
    OPENSSL_add_all_algorithms_noconf();

#ifndef OPENSSL_1_1
    numlocks = CRYPTO_num_locks();
    if ((ssl_locks = (SPINLOCK*)MXS_MALLOC(sizeof(SPINLOCK) * (numlocks + 1))) == NULL)
    {
        rc = MAXSCALE_INTERNALERROR;
        goto return_main;
    }

    for (i = 0; i < numlocks + 1; i++)
    {
        spinlock_init(&ssl_locks[i]);
    }
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

    /**
     * Resolve the full pathname for configuration file and check for
     * read accessibility.
     */
    char pathbuf[PATH_MAX + 1];
    snprintf(pathbuf, PATH_MAX, "%s", get_configdir());
    pathbuf[PATH_MAX] = '\0';
    if (pathbuf[strlen(pathbuf) - 1] != '/')
    {
        strcat(pathbuf, "/");
    }

    if (!resolve_maxscale_conf_fname(&cnf_file_path, pathbuf, cnf_file_arg))
    {
        rc = MAXSCALE_BADCONFIG;
        goto return_main;
    }

    if (!sniff_configuration(cnf_file_path))
    {
        rc = MAXSCALE_BADCONFIG;
        goto return_main;
    }

    /**
     * Init Log Manager for MaxScale.
     */
    {
        bool succp;

        if (!cnf->config_check && mkdir(get_logdir(), 0777) != 0 && errno != EEXIST)
        {
            fprintf(stderr,
                    "Error: Cannot create log directory: %s\n",
                    default_logdir);
            goto return_main;
        }

        mxs_log_set_syslog_enabled(*syslog_enabled);
        mxs_log_set_maxlog_enabled(*maxlog_enabled);

        mxs_log_target_t log_target = MXS_LOG_TARGET_FS;

        if (to_stdout)
        {
            log_target = MXS_LOG_TARGET_STDOUT;
        }
        else if (*log_to_shm)
        {
            log_target = MXS_LOG_TARGET_SHMEM;
        }

        succp = mxs_log_init(NULL, get_logdir(), log_target);

        if (!succp)
        {
            rc = MAXSCALE_BADCONFIG;
            goto return_main;
        }
    }

    if (daemon_mode)
    {
        if (!change_cwd())
        {
            rc = MAXSCALE_INTERNALERROR;
            goto return_main;
        }
    }

    MXS_NOTICE("MariaDB MaxScale %s started", MAXSCALE_VERSION);
    MXS_NOTICE("MaxScale is running in process %i", getpid());

#ifdef SS_DEBUG
    MXS_NOTICE("Commit: %s", MAXSCALE_COMMIT);
#endif

    if (!cnf->config_check)
    {
        /*
         * Set the data directory. We use a unique directory name to avoid conflicts
         * if multiple instances of MaxScale are being run on the same machine.
         */
        if (create_datadir(get_datadir(), datadir))
        {
            set_process_datadir(datadir);
        }
        else
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Cannot create data directory '%s': %d %s\n",
                      datadir, errno, strerror_r(errno, errbuf, sizeof(errbuf)));
            goto return_main;
        }
    }

    if (!daemon_mode)
    {
        fprintf(stderr,
                "Configuration file : %s\n"
                "Log directory      : %s\n"
                "Data directory     : %s\n"
                "Module directory   : %s\n"
                "Service cache      : %s\n\n",
                cnf_file_path,
                get_logdir(),
                get_datadir(),
                get_libdir(),
                get_cachedir());
    }

    if (!(*syslog_enabled) && !(*maxlog_enabled))
    {
        fprintf(stderr, "warning: Both MaxScale and Syslog logging disabled.\n");
    }

    MXS_NOTICE("Configuration file: %s", cnf_file_path);
    MXS_NOTICE("Log directory: %s", get_logdir());
    MXS_NOTICE("Data directory: %s", get_datadir());
    MXS_NOTICE("Module directory: %s", get_libdir());
    MXS_NOTICE("Service cache: %s", get_cachedir());

    if (!config_load(cnf_file_path))
    {
        const char* fprerr =
            "Failed to open, read or process the MaxScale configuration "
            "file. Exiting. See the error log for details.";
        print_log_n_stderr(false, true, fprerr, fprerr, 0);
        MXS_ERROR("Failed to open, read or process the MaxScale configuration file %s. "
                  "Exiting.",
                  cnf_file_path);
        rc = MAXSCALE_BADCONFIG;
        goto return_main;
    }

    if (!qc_setup(cnf->qc_name, cnf->qc_args))
    {
        const char* logerr = "Failed to initialise query classifier library.";
        print_log_n_stderr(true, true, logerr, logerr, eno);
        rc = MAXSCALE_INTERNALERROR;
        goto return_main;
    }

    if (!cnf->config_check)
    {
        /** Check if a MaxScale process is already running */
        if (pid_file_exists())
        {
            /** There is a process with the PID of the maxscale.pid file running.
             * Assuming that this is an already running MaxScale process, we
             * should exit with an error code.  */
            rc = MAXSCALE_ALREADYRUNNING;
            goto return_main;
        }

        /* Write process pid into MaxScale pidfile */
        if (write_pid_file() != 0)
        {
            rc = MAXSCALE_ALREADYRUNNING;
            goto return_main;
        }

        pid_file_created = true;
    }

    /** Initialize statistics */
    ts_stats_init();

    /* Init MaxScale poll system */
    poll_init();

    dcb_global_init();

    /* Initialize the internal query classifier. The plugin will be initialized
     * via the module initialization below.
     */
    if (!qc_process_init(QC_INIT_SELF))
    {
        MXS_ERROR("Failed to initialize the internal query classifier.");
        rc = MAXSCALE_INTERNALERROR;
        goto return_main;
    }

    /* Init MaxScale modules */
    if (!modules_process_init())
    {
        MXS_ERROR("Failed to initialize all modules at startup. Exiting.");
        rc = MAXSCALE_BADCONFIG;
        goto return_main;
    }

    /** Start all monitors */
    monitorStartAll();

    /** Start the services that were created above */
    n_services = service_launch_all();

    if (n_services == 0)
    {
        const char* logerr = "Failed to start all MaxScale services. Exiting.";
        print_log_n_stderr(true, true, logerr, logerr, 0);
        rc = MAXSCALE_NOSERVICES;
        goto return_main;
    }

    if (cnf->config_check)
    {
        MXS_NOTICE("Configuration was successfully verified.");
        rc = MAXSCALE_SHUTDOWN;
        goto return_main;
    }

    /*<
     * Start periodic log flusher thread.
     */
    log_flush_timeout_ms = 1000;

    if (thread_start(&log_flush_thr, log_flush_cb, (void *) &log_flush_timeout_ms) == NULL)
    {
        const char* logerr = "Failed to start log flushing thread.";
        print_log_n_stderr(true, true, logerr, logerr, 0);
        rc = MAXSCALE_INTERNALERROR;
        goto return_main;
    }

    /*
     * Start the housekeeper thread
     */
    if (!hkinit())
    {
        const char* logerr = "Failed to start housekeeper thread.";
        print_log_n_stderr(true, true, logerr, logerr, 0);
        rc = MAXSCALE_INTERNALERROR;
        goto return_main;
    }

    /*<
     * Start the polling threads, note this is one less than is
     * configured as the main thread will also poll.
     */
    n_threads = config_threadcount();
    threads = (THREAD*)MXS_CALLOC(n_threads, sizeof(THREAD));
    /*<
     * Start server threads.
     */
    for (thread_id = 0; thread_id < n_threads - 1; thread_id++)
    {
        if (thread_start(&threads[thread_id], worker_thread_main,
                         (void *)(thread_id + 1)) == NULL)
        {
            const char* logerr = "Failed to start worker thread.";
            print_log_n_stderr(true, true, logerr, logerr, 0);
            rc = MAXSCALE_INTERNALERROR;
            goto return_main;
        }
    }

    MXS_NOTICE("MaxScale started with %d server threads.", config_threadcount());
    /**
     * Successful start, notify the parent process that it can exit.
     */
    ss_dassert(rc == MAXSCALE_SHUTDOWN);
    if (daemon_mode)
    {
        write_child_exit_code(daemon_pipe[1], rc);
    }

    /*<
     * Serve clients.
     */
    poll_waitevents((void *)0);

    /*<
     * Wait for the housekeeper to finish.
     */
    hkfinish();

    /*<
     * Wait server threads' completion.
     */
    for (thread_id = 0; thread_id < n_threads - 1; thread_id++)
    {
        thread_wait(threads[thread_id]);
    }

    /*<
     * Destroy the router and filter instances of all services.
     */
    service_destroy_instances();

    /*<
     * Wait the flush thread.
     */
    thread_wait(log_flush_thr);

    /*< Stop all the monitors */
    monitorStopAll();

    /*< Call finish on all modules. */
    modules_process_finish();

    /* Finalize the internal query classifier. The plugin was finalized
     * via the module finalizarion above.
     */
    qc_process_end(QC_INIT_SELF);

    log_exit_status();
    MXS_NOTICE("MaxScale is shutting down.");

    utils_end();
    cleanup_process_datadir();
    MXS_NOTICE("MaxScale shutdown completed.");

    unload_all_modules();

    ERR_free_strings();
    EVP_cleanup();

return_main:
    if (pid_file_created)
    {
        unlock_pidfile();
        unlink_pidfile();
    }

    mxs_log_flush_sync();

    if (daemon_mode && rc != MAXSCALE_SHUTDOWN)
    {
        /** Notify the parent process that an error has occurred */
        write_child_exit_code(daemon_pipe[1], rc);
    }

    MXS_FREE(cnf_file_arg);

    if (threads)
    {
        MXS_FREE(threads);
    }
    if (cnf_file_path)
    {
        MXS_FREE(cnf_file_path);
    }

    return rc;
} /*< End of main */

/*<
 * Shutdown MaxScale server
 */
int maxscale_shutdown()
{
    static int n_shutdowns = 0;

    int n = atomic_add(&n_shutdowns, 1);

    if (n == 0)
    {
        service_shutdown();
        poll_shutdown();
        hkshutdown();
        log_flush_shutdown();
    }

    return n + 1;
}

static void log_flush_shutdown(void)
{
    do_exit = true;
}


/**
 * Periodic log flusher to ensure that log buffers are
 * written to log even if block buffer used for temporarily
 * storing log contents are not full.
 *
 * @param arg - Flush frequency in milliseconds
 *
 *
 */
static void log_flush_cb(void* arg)
{
    ssize_t timeout_ms = *(ssize_t *)arg;
    struct timespec ts1;

    ts1.tv_sec = timeout_ms / 1000;
    ts1.tv_nsec = (timeout_ms % 1000) * 1000000;

    MXS_NOTICE("Started MaxScale log flusher.");
    while (!do_exit)
    {
        mxs_log_flush();
        nanosleep(&ts1, NULL);
    }
    MXS_NOTICE("Finished MaxScale log flusher.");
}

static void unlock_pidfile()
{
    if (pidfd != PIDFD_CLOSED)
    {
        if (flock(pidfd, LOCK_UN | LOCK_NB) != 0)
        {
            char logbuf[STRING_BUFFER_SIZE + PATH_MAX];
            const char* logerr = "Failed to unlock PID file '%s'.";
            snprintf(logbuf, sizeof(logbuf), logerr, pidfile);
            print_log_n_stderr(true, true, logbuf, logbuf, errno);
        }
        close(pidfd);
    }
}

/**
 * Unlink pid file, called at program exit
 */
static void unlink_pidfile(void)
{
    if (strlen(pidfile))
    {
        if (unlink(pidfile))
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            fprintf(stderr,
                    "MaxScale failed to remove pidfile %s: error %d, %s\n",
                    pidfile,
                    errno,
                    strerror_r(errno, errbuf, sizeof(errbuf)));
        }
    }
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
    char pathbuf[PATH_MAX + 1];
    char logbuf[STRING_BUFFER_SIZE + PATH_MAX];
    char pidbuf[STRING_BUFFER_SIZE];
    pid_t pid;
    bool lock_failed = false;

    snprintf(pathbuf, PATH_MAX, "%s/maxscale.pid", get_piddir());
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
            const char* logerr = "Failed to open PID file '%s'.";
            snprintf(logbuf, sizeof(logbuf), logerr, pathbuf);
            print_log_n_stderr(true, true, logbuf, logbuf, errno);
            return true;
        }
        if (flock(fd, LOCK_EX | LOCK_NB))
        {
            if (errno != EWOULDBLOCK)
            {
                const char* logerr = "Failed to lock PID file '%s'.";
                snprintf(logbuf, sizeof(logbuf), logerr, pathbuf);
                print_log_n_stderr(true, true, logbuf, logbuf, errno);
                close(fd);
                return true;
            }
            lock_failed = true;
        }

        pidfd = fd;
        b = read(fd, pidbuf, sizeof(pidbuf));

        if (b == -1)
        {
            const char* logerr = "Failed to read from PID file '%s'.";
            snprintf(logbuf, sizeof(logbuf), logerr, pathbuf);
            print_log_n_stderr(true, true, logbuf, logbuf, errno);
            unlock_pidfile();
            return true;
        }
        else if (b == 0)
        {
            /** Empty file */
            const char* logerr =
                "PID file read from '%s'. File was empty.\n"
                "If the file is the correct PID file and no other MaxScale processes "
                "are running, please remove it manually and start MaxScale again.";
            snprintf(logbuf, sizeof(logbuf), logerr, pathbuf);
            print_log_n_stderr(true, true, logbuf, logbuf, errno);
            unlock_pidfile();
            return true;
        }

        pidbuf[(size_t)b < sizeof(pidbuf) ? (size_t)b : sizeof(pidbuf) - 1] = '\0';
        pid = strtol(pidbuf, NULL, 0);

        if (pid < 1)
        {
            /** Bad PID */
            const char* logerr =
                "PID file read from '%s'. File contents not valid.\n"
                "If the file is the correct PID file and no other MaxScale processes "
                "are running, please remove it manually and start MaxScale again.";
            snprintf(logbuf, sizeof(logbuf), logerr, pathbuf);
            print_log_n_stderr(true, true, logbuf, logbuf, errno);
            unlock_pidfile();
            return true;
        }

        if (kill(pid, 0) == -1)
        {
            if (errno == ESRCH)
            {
                /** no such process, old PID file */
                if (lock_failed)
                {
                    const char* logerr =
                        "Locking the PID file '%s' failed. "
                        "Read PID from file and no process found with PID %d. "
                        "Confirm that no other process holds the lock on the PID file.";
                    snprintf(logbuf, sizeof(logbuf), logerr, pathbuf, pid);
                    print_log_n_stderr(true, true, logbuf, logbuf, 0);
                    close(fd);
                }
                return lock_failed;
            }
            else
            {
                const char* logerr = "Failed to check the existence of process %d read from file '%s'";
                snprintf(logbuf, sizeof(logbuf), logerr, pid, pathbuf);
                print_log_n_stderr(true, true, logbuf, logbuf, errno);
                unlock_pidfile();
            }
        }
        else
        {
            const char* logerr =
                "MaxScale is already running. Process id: %d. "
                "Use another location for the PID file to run multiple "
                "instances of MaxScale on the same machine.";
            snprintf(logbuf, sizeof(logbuf), logerr, pid);
            print_log_n_stderr(true, true, logbuf, logbuf, 0);
            unlock_pidfile();
        }
    }
    else
    {
        const char* logerr =
            "Cannot open PID file '%s', no read permissions. "
            "Please confirm that the user running MaxScale has read permissions on the file.";
        snprintf(logbuf, sizeof(logbuf), logerr, pathbuf);
        print_log_n_stderr(true, true, logbuf, logbuf, errno);
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
    if (!mxs_mkdir_all(get_piddir(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))
    {
        MXS_ERROR("Failed to create PID directory.");
        return 1;
    }

    char logbuf[STRING_BUFFER_SIZE + PATH_MAX];
    char pidstr[STRING_BUFFER_SIZE];

    snprintf(pidfile, PATH_MAX, "%s/maxscale.pid", get_piddir());

    if (pidfd == PIDFD_CLOSED)
    {
        int fd = -1;

        fd = open(pidfile, O_WRONLY | O_CREAT, 0777);
        if (fd == -1)
        {
            const char* logerr = "Failed to open PID file '%s'.";
            snprintf(logbuf, sizeof(logbuf), logerr, pidfile);
            print_log_n_stderr(true, true, logbuf, logbuf, errno);
            return -1;
        }

        if (flock(fd, LOCK_EX | LOCK_NB))
        {
            if (errno == EWOULDBLOCK)
            {
                snprintf(logbuf, sizeof(logbuf),
                         "Failed to lock PID file '%s', another process is holding a lock on it. "
                         "Please confirm that no other MaxScale process is using the same "
                         "PID file location.", pidfile);
            }
            else
            {
                snprintf(logbuf, sizeof(logbuf), "Failed to lock PID file '%s'.", pidfile);
            }
            print_log_n_stderr(true, true, logbuf, logbuf, errno);
            close(fd);
            return -1;
        }
        pidfd = fd;
    }

    /* truncate pidfile content */
    if (ftruncate(pidfd, 0))
    {
        const char* logerr = "MaxScale failed to truncate PID file '%s'.";
        snprintf(logbuf, sizeof(logbuf), logerr, pidfile);
        print_log_n_stderr(true, true, logbuf, logbuf, errno);
        unlock_pidfile();
        return -1;
    }

    snprintf(pidstr, sizeof(pidstr) - 1, "%d", getpid());

    if (pwrite(pidfd, pidstr, strlen(pidstr), 0) != (ssize_t)strlen(pidstr))
    {
        const char* logerr = "MaxScale failed to write into PID file '%s'.";
        snprintf(logbuf, sizeof(logbuf), logerr, pidfile);
        print_log_n_stderr(true, true, logbuf, logbuf, errno);
        unlock_pidfile();
        return -1;
    }

    /* success */
    return 0;
}

bool handle_path_arg(char** dest, const char* path, const char* arg, bool rd, bool wr)
{
    char pathbuffer[PATH_MAX + 2];
    char* errstr;
    bool rval = false;

    if (path == NULL && arg == NULL)
    {
        return rval;
    }

    if (path)
    {
        snprintf(pathbuffer, PATH_MAX, "%s", path);
        if (pathbuffer[strlen(path) - 1] != '/')
        {
            strcat(pathbuffer, "/");
        }
        if (arg && strlen(pathbuffer) + strlen(arg) + 1 < PATH_MAX)
        {
            strcat(pathbuffer, arg);
        }

        if ((errstr = check_dir_access(pathbuffer, rd, wr)) == NULL)
        {
            *dest = MXS_STRDUP_A(pathbuffer);
            rval = true;
        }
        else
        {
            print_log_n_stderr(true, true, errstr, errstr, 0);
            MXS_FREE(errstr);
            errstr = NULL;
        }
    }

    return rval;
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
 * Pre-parse the configuration file for various directory paths.
 * @param data Parameter passed by inih
 * @param section Section name
 * @param name Parameter name
 * @param value Parameter value
 * @return 0 on error, 1 when successful
 */
static int cnf_preparser(void* data, const char* section, const char* name, const char* value)
{
    MXS_CONFIG* cnf = config_get_global_options();
    char *tmp;
    /** These are read from the configuration file. These will not override
     * command line parameters but will override default values. */
    if (strcasecmp(section, "maxscale") == 0)
    {
        if (strcmp(name, "logdir") == 0)
        {
            if (strcmp(get_logdir(), default_logdir) == 0)
            {
                if (handle_path_arg(&tmp, (char*)value, NULL, true, true))
                {
                    set_logdir(tmp);
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "libdir") == 0)
        {
            if (strcmp(get_libdir(), default_libdir) == 0)
            {
                if (handle_path_arg(&tmp, (char*)value, NULL, true, false))
                {
                    set_libdir(tmp);
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "piddir") == 0)
        {
            if (strcmp(get_piddir(), default_piddir) == 0)
            {
                if (handle_path_arg(&tmp, (char*)value, NULL, true, true))
                {
                    set_piddir(tmp);
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "datadir") == 0)
        {
            if (!datadir_defined)
            {
                if (handle_path_arg(&tmp, (char*)value, NULL, true, false))
                {
                    snprintf(datadir, PATH_MAX, "%s", tmp);
                    datadir[PATH_MAX] = '\0';
                    set_datadir(tmp);
                    datadir_defined = true;
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "cachedir") == 0)
        {
            if (strcmp(get_cachedir(), default_cachedir) == 0)
            {
                if (handle_path_arg((char**)&tmp, (char*)value, NULL, true, false))
                {
                    set_cachedir(tmp);
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "language") == 0)
        {
            if (strcmp(get_langdir(), default_langdir) == 0)
            {
                if (handle_path_arg((char**)&tmp, (char*)value, NULL, true, false))
                {
                    set_langdir(tmp);
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "execdir") == 0)
        {
            if (strcmp(get_execdir(), default_execdir) == 0)
            {
                if (handle_path_arg((char**)&tmp, (char*)value, NULL, true, false))
                {
                    set_execdir(tmp);
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "connector_plugindir") == 0)
        {
            if (strcmp(get_connector_plugindir(), default_connector_plugindir) == 0)
            {
                if (handle_path_arg((char**)&tmp, (char*)value, NULL, true, false))
                {
                    set_connector_plugindir(tmp);
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "persistdir") == 0)
        {
            if (strcmp(get_config_persistdir(), default_config_persistdir) == 0)
            {
                if (handle_path_arg((char**)&tmp, (char*)value, NULL, true, false))
                {
                    set_config_persistdir(tmp);
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "module_configdir") == 0)
        {
            if (strcmp(get_module_configdir(), default_module_configdir) == 0)
            {
                if (handle_path_arg((char**)&tmp, (char*)value, NULL, true, false))
                {
                    set_module_configdir(tmp);
                }
                else
                {
                    return 0;
                }
            }
        }
        else if (strcmp(name, "syslog") == 0)
        {
            if (!syslog_configured)
            {
                cnf->syslog = config_truth_value((char*)value);
            }
        }
        else if (strcmp(name, "maxlog") == 0)
        {
            if (!maxlog_configured)
            {
                cnf->maxlog = config_truth_value((char*)value);
            }
        }
        else if (strcmp(name, "log_augmentation") == 0)
        {
            set_log_augmentation(value);
        }
        else if (strcmp(name, "log_to_shm") == 0)
        {
            if (!log_to_shm_configured)
            {
                cnf->log_to_shm = config_truth_value((char*)value);
            }
        }
    }

    return 1;
}

static int set_user(const char* user)
{
    errno = 0;
    struct passwd *pwname;
    int rval;

    pwname = getpwnam(user);
    if (pwname == NULL)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        printf("Error: Failed to retrieve user information for '%s': %d %s\n",
               user, errno, errno == 0 ? "User not found" : strerror_r(errno, errbuf, sizeof(errbuf)));
        return -1;
    }

    rval = setgid(pwname->pw_gid);
    if (rval != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        printf("Error: Failed to change group to '%d': %d %s\n",
               pwname->pw_gid, errno, strerror_r(errno, errbuf, sizeof(errbuf)));
        return rval;
    }

    rval = setuid(pwname->pw_uid);
    if (rval != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        printf("Error: Failed to change user to '%s': %d %s\n",
               pwname->pw_name, errno, strerror_r(errno, errbuf, sizeof(errbuf)));
        return rval;
    }
    if (prctl(PR_GET_DUMPABLE) == 0)
    {
        if (prctl(PR_SET_DUMPABLE , 1) == -1)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            printf("Error: Failed to set dumpable flag on for the process '%s': %d %s\n",
                   pwname->pw_name, errno, strerror_r(errno, errbuf, sizeof(errbuf)));
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
    if (write(fd, &code, sizeof (int)) == -1)
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

    if (chdir(get_logdir()) != 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        MXS_ERROR("Failed to change working directory to '%s': %d, %s. "
                  "Trying to change working directory to '/'.",
                  get_logdir(), errno, strerror_r(errno, errbuf, sizeof (errbuf)));
        if (chdir("/") != 0)
        {
            MXS_ERROR("Failed to change working directory to '/': %d, %s",
                      errno, strerror_r(errno, errbuf, sizeof (errbuf)));
            rval = false;
        }
        else
        {
            MXS_WARNING("Using '/' instead of '%s' as the current working directory.",
                        get_logdir());
        }
    }
    else
    {
        MXS_NOTICE("Working directory: %s", get_logdir());
    }

    return rval;
}

/**
 * @brief Log a message about the last received signal
 */
static void log_exit_status()
{
    switch (last_signal)
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
 * @return True if context is that of the parent process, false if that of the
 *         child process.
 */
static bool daemonize(void)
{
    pid_t pid;

    pid = fork();

    if (pid < 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "fork() error %s\n", strerror_r(errno, errbuf, sizeof(errbuf)));
        exit(1);
    }

    if (pid != 0)
    {
        /* exit from main */
        return true;
    }

    if (setsid() < 0)
    {
        char errbuf[MXS_STRERROR_BUFLEN];
        fprintf(stderr, "setsid() error %s\n", strerror_r(errno, errbuf, sizeof(errbuf)));
        exit(1);
    }
    return false;
}

/**
 * Sniffs the configuration file, primarily for various directory paths,
 * so that certain settings take effect immediately.
 *
 * @param filepath The path of the configuration file.
 *
 * @return True, if the sniffing succeeded, false otherwise.
 */
static bool sniff_configuration(const char* filepath)
{
    int rv = ini_parse(filepath, cnf_preparser, NULL);

    if (rv != 0)
    {
        const char FORMAT_SYNTAX[] =
            "Error: Failed to pre-parse configuration file %s. Error on line %d.";
        const char FORMAT_OPEN[] =
            "Error: Failed to pre-parse configuration file %s. Failed to open file.";
        const char FORMAT_MALLOC[] =
            "Error: Failed to pre-parse configuration file %s. Memory allocation failed.";

        // We just use the largest one.
        char errorbuffer[sizeof(FORMAT_MALLOC) + strlen(filepath) + UINTLEN(abs(rv))];

        if (rv > 0)
        {
            snprintf(errorbuffer, sizeof(errorbuffer), FORMAT_SYNTAX, filepath, rv);
        }
        else if (rv == -1)
        {
            snprintf(errorbuffer, sizeof(errorbuffer), FORMAT_OPEN, filepath);
        }
        else
        {
            snprintf(errorbuffer, sizeof(errorbuffer), FORMAT_MALLOC, filepath);
        }

        print_log_n_stderr(true, true, errorbuffer, errorbuffer, 0);
    }

    return rv == 0;
}

/**
 * Calls init on all loaded modules.
 *
 * @return True, if all modules were successfully initialized.
 */
static bool modules_process_init()
{
    bool initialized = false;

    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->process_init)
        {
            int rc = (module->process_init)();

            if (rc != 0)
            {
                break;
            }
        }
    }

    if (module)
    {
        // If module is non-NULL it means that the initialization failed for
        // that module. We now need to call finish on all modules that were
        // successfully initialized.
        MXS_MODULE* failed_module = module;
        i = mxs_module_iterator_get(NULL);

        while ((module = mxs_module_iterator_get_next(&i)) != failed_module)
        {
            if (module->process_finish)
            {
                (module->process_finish)();
            }
        }
    }
    else
    {
        initialized = true;
    }

    return initialized;
}

/**
 * Calls process_finish on all loaded modules.
 */
static void modules_process_finish()
{
    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->process_finish)
        {
            (module->process_finish)();
        }
    }
}

/**
 * Calls thread_init on all loaded modules.
 *
 * @return True, if all modules were successfully initialized.
 */
static bool modules_thread_init()
{
    bool initialized = false;

    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->thread_init)
        {
            int rc = (module->thread_init)();

            if (rc != 0)
            {
                break;
            }
        }
    }

    if (module)
    {
        // If module is non-NULL it means that the initialization failed for
        // that module. We now need to call finish on all modules that were
        // successfully initialized.
        MXS_MODULE* failed_module = module;
        i = mxs_module_iterator_get(NULL);

        while ((module = mxs_module_iterator_get_next(&i)) != failed_module)
        {
            if (module->thread_finish)
            {
                (module->thread_finish)();
            }
        }
    }
    else
    {
        initialized = true;
    }

    return initialized;
}

/**
 * Calls thread_finish on all loaded modules.
 */
static void modules_thread_finish()
{
    MXS_MODULE_ITERATOR i = mxs_module_iterator_get(NULL);
    MXS_MODULE* module = NULL;

    while ((module = mxs_module_iterator_get_next(&i)) != NULL)
    {
        if (module->thread_finish)
        {
            (module->thread_finish)();
        }
    }
}
