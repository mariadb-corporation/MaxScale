/*
 * This file is distributed as part of the SkySQL Gateway. It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 * 
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * Copyright SkySQL Ab 2013
 * 
 */

/**
 * @file gateway.c - The gateway entry point.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 23-05-2013	Massimiliano Pinto	epoll loop test
 * 12-06-2013	Mark Riddoch		Add the -p option to set the
 * 					listening port
 *					and bind addr is 0.0.0.0
 * 19/06/13	Mark Riddoch		Extract the epoll functionality 
 * 21/06/13	Mark Riddoch		Added initial config support
 * 27/06/13
 * 28/06/13 Vilho Raatikka		Added necessary headers, example functions and
 * 					calls to log manager and to query classifier.
 *					Put example code behind SS_DEBUG macros.
 * 05/02/14	Mark Riddoch		Addition of version string
 * 29/06/14	Massimiliano Pinto	Addition of pidfile
 *
 * @endverbatim
 */
#define _XOPEN_SOURCE 700
#include <ftw.h>
#include <string.h>
#include <gw.h>
#include <unistd.h>
#include <service.h>
#include <server.h>
#include <dcb.h>
#include <session.h>
#include <modules.h>
#include <config.h>
#include <poll.h>

#include <stdlib.h>
#include <unistd.h>
#include <mysql.h>
#include <monitor.h>
#include <version.h>
#include <maxscale.h>

#include <sys/stat.h>
#include <sys/types.h>

# include <skygw_utils.h>
# include <log_manager.h>

/** for procname */
#define _GNU_SOURCE

extern char *program_invocation_name;
extern char *program_invocation_short_name;

/**
 * Variable holding the enabled logfiles information.
 * Used from log users to check enabled logs prior calling
 * actual library calls such as skygw_log_write.
 */
extern int lm_enabled_logfiles_bitmask;

/*
 * Server options are passed to the mysql_server_init. Each gateway must have a unique
 * data directory that is passed to the mysql_server_init, therefore the data directory
 * is not fixed here and will be updated elsewhere.
 */
static char* server_options[] = {
    "SkySQL Gateway",
    "--no-defaults",
    "--datadir=",
    "--language=",
    "--skip-innodb",
    "--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

const char* default_cnf_fname = "etc/MaxScale.cnf";

static char* server_groups[] = {
    "embedded",
    "server",
    "server",
    "embedded",
    "server",
    "server",
    NULL
};

/* The data directory we created for this gateway instance */
static char	datadir[PATH_MAX+1] = "";

/* The data directory we created for this gateway instance */
static char	pidfile[PATH_MAX+1] = "";

/**
 * exit flag for log flusher.
 */
static bool do_exit = FALSE;

/**
 * Flag to indicate whether libmysqld is successfully initialized.
 */
static bool libmysqld_started = FALSE;

/**
 * If MaxScale is started to run in daemon process the value is true.
 */
static bool     daemon_mode = true;

static void log_flush_shutdown(void);
static void log_flush_cb(void* arg);
static int write_pid_file(char *); /* write MaxScale pidfile */
static void unlink_pidfile(void); /* remove pidfile */
static void libmysqld_done(void);
static bool file_write_header(FILE* outfile);
static bool file_write_footer(FILE* outfile);
static void write_footer(void);
static int ntfw_cb(const char*, const struct stat*, int, struct FTW*);
static bool file_is_readable(char* absolute_pathname);
static bool file_is_writable(char* absolute_pathname);
static void usage(void);
static char* get_expanded_pathname(
        char** abs_path,
        char* input_path,
        const char* fname);
static void print_log_n_stderr(
        bool do_log,
        bool do_stderr,
        char* logstr,
        char*  fprstr,
        int eno);
static bool resolve_maxscale_conf_fname(
        char** cnf_full_path,
        char*  home_dir,
        char*  cnf_file_arg);
static bool resolve_maxscale_homedir(
        char** p_home_dir);
/**
 * Handler for SIGHUP signal. Reload the configuration for the
 * gateway.
 */
static void sighup_handler (int i)
{
	LOGIF(LM, (skygw_log_write(
                LOGFILE_MESSAGE,
                "Refreshing configuration following SIGHUP\n")));
	config_reload();
}

static void sigterm_handler (int i) {
        extern void shutdown_server();
        
	LOGIF(LE, (skygw_log_write_flush(
                LOGFILE_ERROR,
                "MaxScale received signal SIGTERM. Exiting.")));
	shutdown_server();
}

static void
sigint_handler (int i)
{
        extern void shutdown_server();

	LOGIF(LE, (skygw_log_write_flush(
                LOGFILE_ERROR,
                "MaxScale received signal SIGINT. Shutting down.")));
	shutdown_server();
	fprintf(stderr, "\n\nShutting down MaxScale\n\n");
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
static int signal_set (int sig, void (*handler)(int)) {
	static struct sigaction sigact;
	static int err;
        int rc = 0;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = handler;
	GW_NOINTR_CALL(err = sigaction(sig, &sigact, NULL));

        if (err < 0)
        {
                int eno = errno;
                errno = 0;
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed call sigaction() in %s due to %d, %s.",
                        program_invocation_short_name,
                        eno,
                        strerror(eno))));
                rc = 1;
	}
        return rc;
}



/**
 * Cleanup the temporary data directory we created for the gateway
 */
int ntfw_cb(
        const char*        filename,
        const struct stat* filestat,
        int                fileflags,
        struct FTW*        pfwt)
{
        int rc = remove(filename);

        if (rc != 0)
        {
                int eno = errno;
                errno = 0;
                
                LOGIF(LE, (skygw_log_write(
                        LOGFILE_ERROR,
                        "Error : Failed to remove the data directory %s of "
                        "MaxScale due to %d, %s.",
                        datadir,
                        eno,
                        strerror(eno))));
        }
        return rc;
}

void datadir_cleanup()
{
        int depth = 1;
        int flags = FTW_CHDIR|FTW_DEPTH|FTW_MOUNT;
        int rc;

        if (datadir[0] != 0 && access(datadir, F_OK) == 0)
        {
                rc = nftw(datadir, ntfw_cb, depth, flags);
        }
}

static void libmysqld_done(void)
{
        if (libmysqld_started) {
            mysql_library_end();
        }
}


static void write_footer(void)
{
        file_write_footer(stdout);
}

static bool file_write_footer(
        FILE*       outfile)
{
        bool        succp = false;
        size_t      wbytes1;
        size_t      len1;
        const char* header_buf1;

        header_buf1 = "------------------------------------------------------"
            "\n\n"; 
        len1 = strlen(header_buf1);
        wbytes1=fwrite((void*)header_buf1, len1, 1, outfile);

        succp = true;

        return succp;
}

static bool file_write_header(
        FILE*       outfile)
{
        bool        succp = false;
        size_t      wbytes1;
        size_t      wbytes2;
        size_t      wbytes3;
        size_t      len1;
        size_t      len2;
        size_t      len3;
        const char* header_buf1;
        char*       header_buf2 = NULL;
        const char* header_buf3;
        time_t*     t  = NULL;
        struct tm*  tm = NULL;

        if ((t = (time_t *)malloc(sizeof(time_t))) == NULL) {
                goto return_succp;
        }
        
        if ((tm = (struct tm *)malloc(sizeof(struct tm))) == NULL) {
                goto return_succp;
        }
        
        *t = time(NULL); 
        *tm = *localtime(t);
        
        header_buf1 = "\n\nSkySQL MaxScale " MAXSCALE_VERSION "\t";
        header_buf2 = strdup(asctime(tm));

        if (header_buf2 == NULL) {
                goto return_succp;
        }
        header_buf3 = "------------------------------------------------------\n"; 

        len1 = strlen(header_buf1);
        len2 = strlen(header_buf2);
        len3 = strlen(header_buf3);
#if defined(LAPTOP_TEST)
        usleep(DISKWRITE_LATENCY);
#else
        wbytes1=fwrite((void*)header_buf1, len1, 1, outfile);
        wbytes2=fwrite((void*)header_buf2, len2, 1, outfile);
        wbytes3=fwrite((void*)header_buf3, len3, 1, outfile);
#endif
        
        succp = true;

return_succp:
        if (tm != NULL) { 
                free(tm);
        }
        if (t != NULL) {
                free(t);
        }
        if (header_buf2 != NULL) {
                free(header_buf2);
        }
        return succp;
}

static bool resolve_maxscale_conf_fname(
        char** cnf_full_path,
        char*  home_dir,
        char*  cnf_file_arg)
{
        bool  succp = false;
        
        if (cnf_file_arg != NULL)
        {
                char* home_etc_dir;
                /*<
                 * 1. argument is valid full pathname
                 * '- /home/jdoe/MaxScale/myconf.cnf'
                 */
                if (file_is_readable(cnf_file_arg))
                {
                        *cnf_full_path = cnf_file_arg;
                        succp = true;
                        goto return_succp;
                }
                /*<
                 * 2. argument is file name only and file is located in home
                 * directory.
                 * '-f MaxScale.cnf' 
                 */
                home_etc_dir = (char*)malloc(strlen(home_dir)+strlen("/etc")+1);
                snprintf(home_etc_dir,
                         strlen(home_dir)+strlen("/etc")+1,
                         "%s/etc",
                         home_dir);
                *cnf_full_path = get_expanded_pathname(NULL,
                                                       home_etc_dir,
                                                       cnf_file_arg);
                free(home_etc_dir);

                if (*cnf_full_path != NULL)
                {
                         if (file_is_readable(*cnf_full_path))
                         {
                                 succp = true;
                                 goto return_succp;
                         }
                         else
                         {
                                 char* logstr = "Found config file but wasn't "
                                         "able to read it.";
                                 int eno = errno;
                                 print_log_n_stderr(true, true, logstr, logstr, eno);
                                 goto return_succp;
                         }                        
                }
                /*<
                 * 3. argument is valid relative pathname
                 * '-f ../myconf.cnf'
                 */
                if (realpath(cnf_file_arg, *cnf_full_path) != NULL)
                {
                         if (file_is_readable(*cnf_full_path))
                         {
                                 succp = true;
                                 goto return_succp;
                         }
                         else
                         {
                                 char* logstr = "Failed to open read access to "
                                         "config file.";
                                 int eno = errno;
                                 print_log_n_stderr(true, true, logstr, logstr, eno);
                                 goto return_succp;
                         }
                }
                else
                {
                        char* logstr = "Failed to expand config file name to "
                                "complete path.";
                        int eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, logstr, logstr, eno);
                        goto return_succp;
                }
        }
        else /*< default config file name is used */
        {
                *cnf_full_path = get_expanded_pathname(NULL, home_dir, default_cnf_fname);

                if (*cnf_full_path != NULL)
                {
                         if (file_is_readable(*cnf_full_path))
                         {
                                 succp = true;
                                 goto return_succp;
                         }
                         else
                         {
                                 char* logstr = "Found config file but wasn't "
                                         "able to read it.";
                                 int eno = errno;
                                 print_log_n_stderr(true, true, logstr, logstr, eno);
                                 goto return_succp;
                         }                        
                }
                goto return_succp;
        }
return_succp:
        return succp;
}


static bool resolve_maxscale_homedir(
        char** p_home_dir)
{
        bool  succp = false;
        char* tmp;
        char* log_context = NULL;
        
        ss_dassert(*p_home_dir == NULL);

        if (*p_home_dir != NULL)
        {
                log_context = strdup("Command-line argument");
                goto check_home_dir;
        }
        /*<
         * 1. if home dir wasn't specified by a command-line argument,
         *    read env. variable MAXSCALE_HOME.
         */
        if (getenv("MAXSCALE_HOME") != NULL)
        {
                tmp = strndup(getenv("MAXSCALE_HOME"), PATH_MAX);
                get_expanded_pathname(p_home_dir, tmp, NULL);
        
                if (*p_home_dir != NULL)
                {
                        log_context = strdup("MAXSCALE_HOME");
                        goto check_home_dir;
                }
                free(tmp);
        }
        else
        {
                fprintf(stderr, "\n*\n* Warning : MAXSCALE_HOME environment variable "
                        "is not set.\n*\n");
                LOGIF(LE, (skygw_log_write_flush(
                                   LOGFILE_ERROR,
                                   "Warning : MAXSCALE_HOME environment "
                                   "variable is not set.")));
        }
        /*<
         * 2. if home dir wasn't specified in MAXSCALE_HOME,
         *    try access /etc/MaxScale/
         */
        tmp = strdup("/etc/MaxScale");                
        get_expanded_pathname(p_home_dir, tmp, NULL);

        if (*p_home_dir != NULL)
        {
                log_context = strdup("/etc/MaxScale");
                goto check_home_dir;
        }
        free(tmp);
        /*<
         * 3. if /etc/MaxScale/MaxScale.cnf didn't exist or wasn't accessible, home
         *    isn't specified. Thus, try to access $PWD/MaxScale.cnf .
         */
        tmp = strndup(getenv("PWD"), PATH_MAX);
        get_expanded_pathname(p_home_dir, tmp, default_cnf_fname);

        if (*p_home_dir != NULL)
        {
                log_context = strdup("Current working directory");
                goto check_home_dir;
        }

check_home_dir:
        if (*p_home_dir != NULL)
        {
                if (!file_is_readable(*p_home_dir))
                {
                        char* tailstr = "MaxScale doesn't have read permission "
                                "to MAXSCALE_HOME.";
                        char* logstr = (char*)malloc(strlen(log_context)+
                                                     1+
                                                     strlen(tailstr)+
                                                     1);
                        snprintf(logstr,
                                 strlen(log_context)+
                                 1+
                                 strlen(tailstr)+1,
                                 "%s:%s",
                                 log_context,
                                 tailstr);
                        print_log_n_stderr(true, true, logstr, logstr, 0);
                        free(logstr);
                        goto return_succp;
                }
                
                if (!file_is_writable(*p_home_dir))
                {
                        char* tailstr = "MaxScale doesn't have write permission "
                                "to MAXSCALE_HOME. Exiting.";
                        char* logstr = (char*)malloc(strlen(log_context)+
                                                     1+
                                                     strlen(tailstr)+
                                                     1);
                        snprintf(logstr,
                                 strlen(log_context)+
                                 1+
                                 strlen(tailstr)+1,
                                 "%s:%s",
                                 log_context,
                                 tailstr);
                        print_log_n_stderr(true, true, logstr, logstr, 0);
                        free(logstr);
                        goto return_succp;
                }
                
                if (!daemon_mode)
                {
                        fprintf(stderr,
                                "Using %s as MAXSCALE_HOME = %s\n",
                                log_context,
                                tmp);
                }
                succp = true;
                goto return_succp;
        }
        
return_succp:
        free (tmp);

        if (log_context != NULL)
        {
                free(log_context);
        }
        
        if (!succp)
        {
                char* logstr = "MaxScale was unable to locate home directory "
                        "with read and write permissions. \n*\n* Exiting.";
                print_log_n_stderr(true, true, logstr, logstr, 0);
                usage();
        }
        return succp;
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
        bool     do_log,   /*< is printing to log enabled */
        bool     do_stderr,/*< is printing to stderr enabled */
        char*    logstr,   /*< string to be printed to log */
        char*    fprstr,   /*< string to be printed to stderr */
        int      eno)      /*< errno, if it is set, zero, otherwise */
{
        char* log_err = "Error :";
        char* fpr_err = "*\n* Error :";
        char* fpr_end   = "\n*\n";
        
        if (do_log) {
                LOGIF(LE, (skygw_log_write_flush(
                                   LOGFILE_ERROR,
                                   "%s %s %s %s",
                                   log_err,
                                   logstr,
                                   eno == 0 ? " " : "Error :",
                                   eno == 0 ? " " : strerror(eno))));
        }
        if (do_stderr) {
                fprintf(stderr,
                        "%s %s %s %s %s",
                        fpr_err,
                        fprstr,
                        eno == 0 ? " " : "Error :",
                        eno == 0 ? " " : strerror(eno),
                        fpr_end);
        }
}

static bool file_is_readable(
        char* absolute_pathname)
{
        bool succp = true;

        if (access(absolute_pathname, R_OK) != 0)
        {
                int eno = errno;
                errno = 0;

                if (!daemon_mode)
                {
                        fprintf(stderr,
                                "*\n* Warning : Failed to read the configuration "
                                "file %s. %s.\n*\n",
                                absolute_pathname,
                                strerror(eno));
                }
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Warning : Failed to read the configuration file %s due "
                        "to %d, %s.",
                        absolute_pathname,
                        eno,
                        strerror(eno))));
                succp = false;
        }
        return succp;
}

static bool file_is_writable(
        char* absolute_pathname)
{
        bool succp = true;

        if (access(absolute_pathname, W_OK) != 0)
        {
                int eno = errno;
                errno = 0;

                if (!daemon_mode)
                {
                        fprintf(stderr,
                                "*\n* Error : unable to open file %s for write "
                                "due %d, %s.\n*\n",
                                absolute_pathname,
                                eno,
                                strerror(eno));
                }
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : unable to open file %s for write due "
                        "to %d, %s.",
                        absolute_pathname,
                        eno,
                        strerror(eno))));
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
static char* get_expanded_pathname(
        char** output_path,
        char*  relative_path,
        const char*  fname)
{
        char*  cnf_file_buf = NULL;
        char*  expanded_path;
        
        if (relative_path == NULL)
        {
                goto return_cnf_file_buf;
        }
                
        expanded_path = (char*)malloc(PATH_MAX);
        
        /*<
         * Expand possible relative pathname to absolute path
         */
        if (realpath(relative_path, expanded_path) == NULL)
        {
                int eno = errno;
                errno = 0;
                
                fprintf(stderr,
                        "*\n* Warning : Failed to read the "
                        "directory %s. %s.\n*\n",
                        relative_path,
                        strerror(eno));
                
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Warning : Failed to read the "
                        "directory %s, due "
                        "to %d, %s.",
                        relative_path,
                        eno,
                        strerror(eno))));
                free(expanded_path);
                *output_path = NULL;
                goto return_cnf_file_buf;
        }

        if (fname != NULL)
        {
                /*<
                 * Concatenate an absolute filename and test its existence and
                 * readability.
                 */
                size_t pathlen = strnlen(expanded_path, PATH_MAX)+
                        1+
                        strnlen(fname, PATH_MAX)+
                        1;
                cnf_file_buf = (char*)malloc(pathlen);

                if (cnf_file_buf == NULL)
                {
                        free(expanded_path);
                        expanded_path = NULL;
                        goto return_cnf_file_buf;
                }
                snprintf(cnf_file_buf, pathlen, "%s/%s", expanded_path, fname);

                if (!file_is_readable(cnf_file_buf))
                {
                        free(expanded_path);
                        free(cnf_file_buf);
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
                if (!file_is_readable(expanded_path))
                {
                        free(expanded_path);
                        expanded_path = NULL;
                        goto return_cnf_file_buf;
                }
        }
        
        if (output_path == NULL)
        {
                free(expanded_path);
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
                "*\n* Usage : maxscale [-h] | [-d] [-c <home "
                "directory>] [-f <config file name>]\n* where:\n* "
                "-h help\n* -d enable running in terminal process (default:disabled)\n* "
                "-c relative|absolute MaxScale home directory\n* "
                "-f relative|absolute pathname of MaxScale configuration file (default:MAXSCALE_HOME/etc/MaxScale.cnf)\n*\n");
}

/** 
 * The main entry point into the gateway 
 *
 * @param argc The argument count
 * @param argv The array of arguments themselves
 *
 * @return 0 in success, 1 otherwise
 *
 * 
 * @details Logging and error printing:
 * ---
 * What is printed to the terminal is something that the user can understand,
 * and/or something what the user can do for. For example, fix configuration.
 * More detailed messages are printed to error log, and optionally to trace
 * and debug log.
 *
 * As soon as process switches to daemon process, stderr printing is stopped -
 * except when it comes to command-line arguments processing.
 * This is not obvious solution because stderr is often directed to somewhere,
 * but currently this is the case.
 *
 * The configuration file is by default \<maxscale home\>/etc/MaxScale.cnf
 * The name of configuration file and its location can be specified by
 * command-line argument.
 * 
 * \<maxscale home\> is resolved in the following order:
 * 1. from '-c <dir>' command-line argument
 * 2. from MAXSCALE_HOME environment variable
 * 3. /etc/ if MaxScale.cnf is found from there
 * 4. current working directory if MaxScale.cnf is found from there
 *
 * \<config filename\> is resolved in the following order:
 * 1. from '-f \<config filename\>' command-line argument
 * 2. by using default value "MaxScale.cnf"
 *
 */
int main(int argc, char **argv)
{
        int      rc = MAXSCALE_SHUTDOWN;
        int 	 l;
        int	 i;
        int      n;
        int      n_threads; /*< number of epoll listener threads */ 
        int      n_services;
        int      eno = 0;   /*< local variable for errno */
        int      opt;
        void**	 threads;   /*< thread list */
        char	 mysql_home[PATH_MAX+1];
        char	 datadir_arg[10+PATH_MAX+1];  /*< '--datadir='  + PATH_MAX */
        char     language_arg[11+PATH_MAX+1]; /*< '--language=' + PATH_MAX */
        char*    home_dir = NULL;             /*< home dir, to be freed */
        char*    cnf_file_path = NULL;        /*< conf file, to be freed */
        char*    cnf_file_arg = NULL;         /*< conf filename from cmd-line arg */
        void*    log_flush_thr = NULL;
        ssize_t  log_flush_timeout_ms = 0;
        sigset_t sigset;
        sigset_t sigpipe_mask;
        sigset_t saved_mask;
        void   (*exitfunp[4])(void) = {skygw_logmanager_exit,
                                       datadir_cleanup,
                                       write_footer,
                                       NULL};
        sigemptyset(&sigpipe_mask);
        sigaddset(&sigpipe_mask, SIGPIPE);

#if defined(SS_DEBUG)
        memset(conn_open, 0, sizeof(bool)*10240);
        memset(dcb_fake_write_errno, 0, sizeof(unsigned char)*10240);
        memset(dcb_fake_write_ev, 0, sizeof(__int32_t)*10240);
        fail_next_backend_fd = false;
        fail_next_client_fd = false;
        fail_next_accept = 0;
        fail_accept_errno = 0;
#endif
        file_write_header(stderr);
        /*<
         * Register functions which are called at exit except libmysqld-related,
         * which must be registered later to avoid ordering issues.
         */
        for (i=0; exitfunp[i] != NULL; i++)
        {
                l = atexit(*exitfunp);

                if (l != 0)
                {
                        char* fprerr = "Failed to register exit functions for MaxScale";
                        print_log_n_stderr(false, true, NULL, fprerr, 0);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
        }
        while ((opt = getopt(argc, argv, "dc:f:h")) != -1)
        {
                bool succp = true;
                
                switch (opt) {
                case 'd':
                        /*< Debug mode, maxscale runs in this same process */
                        daemon_mode = false;
                        break;
                        
                case 'c':
                        /*<
                         * Create absolute path pointing to MaxScale home
                         * directory. User-provided home directory may be
                         * either absolute or relative. If latter, it is
                         * expanded and stored in home_dir if succeed.
                         */
                        if (optarg[0] != '-')
                        {
				struct stat sb;

				if (stat(optarg, &sb) != -1
					&& (! S_ISDIR(sb.st_mode)))
				{
					char* logerr = "Home directory argument "
						"identifier \'-c\' was specified but "
						"the argument didn't specify a valid "
						"a directory.";
					print_log_n_stderr(true, true, logerr, logerr, 0);
					usage();
					succp = false;
				} 
				else
				{
                                	get_expanded_pathname(&home_dir, optarg, NULL);
				}
                        }

                        if (home_dir != NULL)
                        {
                                /*<
                                 * MAXSCALE_HOME is set.
                                 * It is used to assist in finding the modules
                                 * to be loaded into MaxScale.
                                 */
                                setenv("MAXSCALE_HOME", home_dir, 1);
                        }
                        else
                        {
                                char* logerr = "Home directory argument "
                                        "identifier \'-c\' was specified but "
                                        "the argument didn't specify \n  a valid "
                                        "home directory or the argument was "
                                        "missing.";
                                print_log_n_stderr(true, true, logerr, logerr, 0);
                                usage();
                                succp = false;
                        }
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
                                char* logerr = "Configuration file argument "
                                        "identifier \'-f\' was specified but "
                                        "the argument didn't specify\n  a valid "
                                        "configuration file or the argument "
                                        "was missing.";
                                print_log_n_stderr(true, true, logerr, logerr, 0);
                                usage();
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
                        rc = MAXSCALE_BADARG;
                        goto return_main;
                }
        }

        if (!daemon_mode)
        {
                fprintf(stderr,
                        "Info : MaxScale will be run in the terminal process.\n See "
                        "the log from the following log files : \n\n");
        }
        else 
        {
                /*<
                 * Maxscale must be daemonized before opening files, initializing
                 * embedded MariaDB and in general, as early as possible.
                 */
                int r;
                int eno = 0;
                char* fprerr = "Failed to initialize set the signal "
                        "set for MaxScale. Exiting.";

                fprintf(stderr,
                        "Info :  MaxScale will be run in a daemon process.\n\tSee "
                        "the log from the following log files : \n\n");
                
                r = sigfillset(&sigset);

                if (r != 0)
                {
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, fprerr, eno);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
                r = sigdelset(&sigset, SIGHUP);

                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGHUP from the "
                                "signal set of MaxScale. Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
                r = sigdelset(&sigset, SIGTERM);

                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGTERM from the "
                                "signal set of MaxScale. Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
                r = sigprocmask(SIG_SETMASK, &sigset, NULL);

                if (r != 0) {
                        char* logerr = "Failed to set the signal set for MaxScale."
                                " Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
                gw_daemonize();
        }
        /*<
         * Set signal handlers for SIGHUP, SIGTERM, and SIGINT.
         */
        {
                char* fprerr = "Failed to initialize signal handlers. Exiting.";
                char* logerr = NULL;
                l = signal_set(SIGHUP, sighup_handler);

                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGHUP. Exiting.");
                        goto sigset_err;
                }
                l = signal_set(SIGTERM, sigterm_handler);

                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGTERM. Exiting.");
                        goto sigset_err;
                }
                l = signal_set(SIGINT, sigint_handler);

                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGINT. Exiting.");
                        goto sigset_err;
                }
        sigset_err:
                if (l != 0)
                {
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, !daemon_mode, logerr, fprerr, eno);
                        free(logerr);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
        }
        eno = pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask);

        if (eno != 0)
        {
                char* logerr = "Failed to initialise signal mask for MaxScale. "
                        "Exiting.";
                print_log_n_stderr(true, true, logerr, logerr, eno);
                rc = MAXSCALE_INTERNALERROR;
                goto return_main;
        }

	/* register exit function for embedded MySQL library */
        l = atexit(libmysqld_done);

        if (l != 0) {
                char* fprerr = "Failed to register exit function for\n* "
                        "embedded MySQL library.\n* Exiting.";
                char* logerr = "Failed to register exit function libmysql_done "
                        "for MaxScale. Exiting.";
                print_log_n_stderr(true, true, logerr, fprerr, 0);
                rc = MAXSCALE_INTERNALERROR;
                goto return_main;                
        }

        /*<
         * If MaxScale home directory wasn't set by command-line argument.
         * Next, resolve it from environment variable and further on,
         * try to use default.
         */
        if (home_dir == NULL)
        {
                if (!resolve_maxscale_homedir(&home_dir))
                {
                        ss_dassert(home_dir == NULL);
                        rc = MAXSCALE_HOMELESS;
                        goto return_main;
                }
                sprintf(mysql_home, "%s/mysql", home_dir);
                setenv("MYSQL_HOME", mysql_home, 1);
        }

        /*<
         * Init Log Manager for MaxScale.
         * If $MAXSCALE_HOME is set then write the logs into $MAXSCALE_HOME/log.
         * The skygw_logmanager_init expects to take arguments as passed to main
         * and proesses them with getopt, therefore we need to give it a dummy
         * argv[0]
         */
        {
                char 	buf[1024];
                char	*argv[8];

                sprintf(buf, "%s/log", home_dir);
                mkdir(buf, 0777);
                argv[0] = "MaxScale";
                argv[1] = "-j";
                argv[2] = buf;
                argv[3] = "-s"; /*< store to shared memory */
                argv[4] = "LOGFILE_DEBUG,LOGFILE_TRACE";   /*< ..these logs to shm */
                argv[5] = "-l"; /*< write to syslog */
                argv[6] = "LOGFILE_MESSAGE,LOGFILE_ERROR"; /*< ..these logs to syslog */
                argv[7] = NULL;
                skygw_logmanager_init(7, argv);
        }

        /*<
         * Resolve the full pathname for configuration file and check for
         * read accessibility.
         */
        if (!resolve_maxscale_conf_fname(&cnf_file_path, home_dir, cnf_file_arg))
        {
                ss_dassert(cnf_file_path == NULL);
                rc = MAXSCALE_BADCONFIG;
                goto return_main;
        }

        /*<
         * Set a data directory for the mysqld library, we use
         * a unique directory name to avoid clauses if multiple
         * instances of the gateway are beign run on the same
         * machine.
         */
        sprintf(datadir, "%s/data%d", home_dir, getpid());
        mkdir(datadir, 0777);

        if (!daemon_mode)
        {
                fprintf(stderr,
                        "Home directory     : %s"
                        "\nConfiguration file : %s"
                        "\nData directory     : %s\n\n",
                        home_dir,
                        cnf_file_path,
                        datadir);
        }
        LOGIF(LM, (skygw_log_write_flush(
                           LOGFILE_MESSAGE,
                           "Home directory      : %s",
                           home_dir)));
        LOGIF(LM, (skygw_log_write_flush(
                           LOGFILE_MESSAGE,
                           "Data directory      : %s",
                           datadir)));
        LOGIF(LM, (skygw_log_write_flush(
                           LOGFILE_MESSAGE,
                           "Configuration file  : %s",
                           cnf_file_path)));

        /*< Update the server options */
        for (i = 0; server_options[i]; i++)
        {
                if (!strcmp(server_options[i], "--datadir="))
                {
                        snprintf(datadir_arg, 10+PATH_MAX+1, "--datadir=%s", datadir);
                        server_options[i] = datadir_arg;
                }
                else if (!strcmp(server_options[i], "--language="))
                {
                        snprintf(language_arg,
                                 11+PATH_MAX+1,
                                 "--language=%s/mysql",
                                 home_dir);
                        server_options[i] = language_arg;
                }
        }

        if (mysql_library_init(num_elements, server_options, server_groups))
        {
                if (!daemon_mode)
                {
                        char* fprerr = "Failed to initialise the MySQL library. "
                                "Exiting.";
                        print_log_n_stderr(false, true, fprerr, fprerr, 0);

                        if (mysql_errno(NULL) == 2000)
                        {
                                if (strncmp(mysql_error(NULL),
                                            "Unknown MySQL error",
                                            strlen("Unknown MySQL error")) != 0)
                                {
                                        fprintf(stderr,
                                                "*\n* Error : MySQL Error should "
                                                "be \"Unknown MySQL error\" "
                                                "instead of\n* %s\n* Hint "
                                                ":\n* Ensure that you have "
                                                "MySQL error messages file, errmsg.sys in "
                                                "\n* %s/mysql\n* Ensure that Embedded "
                                                "Server Library version matches "
                                                "exactly with that of the errmsg.sys "
                                                "file.\n*\n",
                                                mysql_error(NULL),
                                                home_dir);
                                }
                                else
                                {
                                        fprintf(stderr,
                                                "*\n* Error : MySQL Error %d, %s"
                                                "\n*\n",
                                                mysql_errno(NULL),
                                                mysql_error(NULL));
                                }
                        }
                }
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : mysql_library_init failed. It is a "
                        "mandatory component, required by router services and "
                        "the MaxScale core. Error %d, %s, %s : %d. Exiting.",
                        mysql_errno(NULL),
                        mysql_error(NULL),
                        __FILE__,
                        __LINE__)));
                rc = MAXSCALE_NOLIBRARY;
                goto return_main;
        }
        libmysqld_started = TRUE;

        if (!config_load(cnf_file_path))
        {
                char* fprerr = "Failed to load MaxScale configuration "
                        "file. Exiting.";
                print_log_n_stderr(false, !daemon_mode, fprerr, fprerr, 0);
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to load MaxScale configuration file %s. "
                        "Exiting.",
                        cnf_file_path)));
                rc = MAXSCALE_BADCONFIG;
                goto return_main;
        }
        LOGIF(LM, (skygw_log_write(
                LOGFILE_MESSAGE,
                "SkySQL MaxScale %s (C) SkySQL Ab 2013,2014",
		MAXSCALE_VERSION))); 
        LOGIF(LM, (skygw_log_write(
                LOGFILE_MESSAGE,
                "MaxScale is running in process  %i",
                getpid())));

	/* Write process pid into MaxScale pidfile */
	write_pid_file(home_dir);

	/* Init MaxScale poll system */
        poll_init();
    
	/** 
	 * Init mysql thread context for main thread as well. Needed when users
	 * are queried from backends.
	 */
	mysql_thread_init();
	
        /** Start the services that were created above */
        n_services = serviceStartAll();
        
	if (n_services == 0)
        {
                char* logerr = "Failed to start any MaxScale services. Exiting.";
                print_log_n_stderr(true, !daemon_mode, logerr, logerr, 0);
                rc = MAXSCALE_NOSERVICES;
                goto return_main;
        }
        /*<
         * Start periodic log flusher thread.
         */
        log_flush_timeout_ms = 1000;
        log_flush_thr = thread_start(
                log_flush_cb,
                (void *)&log_flush_timeout_ms);
        /*<
         * Start the polling threads, note this is one less than is
         * configured as the main thread will also poll.
         */
        n_threads = config_threadcount();
        threads = (void **)calloc(n_threads, sizeof(void *));
        /*<
         * Start server threads.
         */
        for (n = 0; n < n_threads - 1; n++)
        {
                threads[n] = thread_start(poll_waitevents, (void *)(n + 1));
        }
        LOGIF(LM, (skygw_log_write(LOGFILE_MESSAGE,
                        "MaxScale started with %d server threads.",
                                   config_threadcount())));
        /*<
         * Serve clients.
         */
        poll_waitevents((void *)0);

        /*<
         * Wait server threads' completion.
         */
        for (n = 0; n < n_threads - 1; n++)
        {
                thread_wait(threads[n]);
        }
        free(threads);
        free(home_dir);
        free(cnf_file_path);
        
        /*<
         * Wait the flush thread.
         */
        thread_wait(log_flush_thr);

        /*< Stop all the monitors */
        monitorStopAll();
	
        LOGIF(LM, (skygw_log_write(
                           LOGFILE_MESSAGE,
                           "MaxScale is shutting down.")));
	/** Release mysql thread context*/
	mysql_thread_end();
	
        datadir_cleanup();
        LOGIF(LM, (skygw_log_write(
                           LOGFILE_MESSAGE,
                           "MaxScale shutdown completed.")));

	/* Remove Pidfile */
	unlink_pidfile();
	
return_main:
        return rc;
} /*< End of main */

/*<
 * Shutdown MaxScale server
 */
void
        shutdown_server()
{
        poll_shutdown();
        log_flush_shutdown();
}

static void log_flush_shutdown(void)
{
        do_exit = TRUE;
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
static void log_flush_cb(
        void* arg)
{
        ssize_t timeout_ms = *(ssize_t *)arg;

        LOGIF(LM, (skygw_log_write(LOGFILE_MESSAGE,
                                   "Started MaxScale log flusher.")));
        while (!do_exit) {
            skygw_log_flush(LOGFILE_ERROR);
            skygw_log_flush(LOGFILE_MESSAGE);
            skygw_log_flush(LOGFILE_TRACE);
            skygw_log_flush(LOGFILE_DEBUG);
            usleep(timeout_ms*1000);
        }
        LOGIF(LM, (skygw_log_write(LOGFILE_MESSAGE,
                                   "Finished MaxScale log flusher.")));
}

/** 
 * Unlink pid file, called at program exit
 */
static void unlink_pidfile(void)
{
	if (strlen(pidfile)) {
		if (unlink(pidfile)) {
			fprintf(stderr, "MaxScale failed to remove pidfile %s: error %d, %s\n", pidfile, errno, strerror(errno));
		}
	}
}

/** 
 * Write process pid into pidfile anc close it
 * Parameters:
 * @param home_dir The MaxScale home dir
 * @return 0 on success, 1 on failure
 *
 */

static int write_pid_file(char *home_dir) {

	int fd = -1;

        snprintf(pidfile, PATH_MAX, "%s/log/maxscale.pid", home_dir);

        fd = open(pidfile, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (fd == -1) {
                fprintf(stderr, "MaxScale failed to open pidFile %s: error %d, %s\n", pidfile, errno, strerror(errno));
		return 1;
        } else {
                char pidstr[50]="";
		/* truncate pidfile content */
                if (ftruncate(fd, 0) == -1) {
                        fprintf(stderr, "MaxScale failed to truncate pidfile %s: error %d, %s\n", pidfile, errno, strerror(errno));
                }

                snprintf(pidstr, sizeof(pidstr)-1, "%d", getpid());

                if (pwrite(fd, pidstr, strlen(pidstr), 0) != (ssize_t)strlen(pidstr)) {
                        fprintf(stderr, "MaxScale failed to write into pidfile %s: error %d, %s\n", pidfile, errno, strerror(errno));
			/* close file and return */
                	close(fd);
			return 1;
                }

		/* close file */
                close(fd);

                fprintf(stderr, "MaxScale PID %s in pidfile %s\n", pidstr, pidfile);
        }

	/* success */
	return 0;
}
