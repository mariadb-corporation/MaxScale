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
 * 28/06/13 Vilho Raatikka      Added necessary headers, example functions and
 *                              calls to log manager and to query classifier.
 *                              Put example code behind SS_DEBUG macros.
 *
 * @endverbatim
 */

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
#include <mysql.h>
#include <monitor.h>

#include <sys/stat.h>
#include <sys/types.h>

# include <skygw_utils.h>
# include <log_manager.h>

/** for procname */
#define _GNU_SOURCE

extern char *program_invocation_name;
extern char *program_invocation_short_name;

/*
 * Server options are passed to the mysql_server_init. Each gateway must have a unique
 * data directory that is passed to the mysql_server_init, therefore the data directory
 * is not fixed here and will be updated elsewhere.
 */
static char* server_options[] = {
    "SkySQL Gateway",
    "--datadir=",
    "--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

static char* server_groups[] = {
    "embedded",
    "server",
    "server",
    NULL
};

/* The data directory we created for this gateway instance */
static char	datadir[1024] = "";

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
static void libmysqld_done(void);
static bool file_write_header(FILE* outfile);
static bool file_write_footer(FILE* outfile);
static void write_footer(void);

/**
 * Handler for SIGHUP signal. Reload the configuration for the
 * gateway.
 */
static void sighup_handler (int i)
{
	skygw_log_write(
                LOGFILE_MESSAGE,
                "Refreshing configuration following SIGHUP\n");
	config_reload();
}

static void sigterm_handler (int i) {
        extern void shutdown_server();
        
	skygw_log_write_flush(
                LOGFILE_ERROR,
                "MaxScale received signal SIGTERM. Exiting.");
	shutdown_server();
}

static void
sigint_handler (int i)
{
        extern void shutdown_server();

	skygw_log_write_flush(
                LOGFILE_ERROR,
                "MaxScale received signal SIGINT. Shutting down.");
	shutdown_server();
	fprintf(stderr, "\n\nShutting down MaxScale\n\n");
}

/** 
 * @node Wraps sigaction calls
 *
 * Parameters:
 * @param sig - <usage>
 *          <description>
 *
 * @param void - <usage>
 *          <description>
 *
 * @param handler - <usage>
 *          <description>
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
		skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed call sigaction() in %s due to %d, %s.",
                        program_invocation_short_name,
                        eno,
                        strerror(eno));
                rc = 1;
	}
        return rc;
}



/**
 * Cleanup the temporary data directory we created for the gateway
 */
void
datadir_cleanup()
{
char	buf[1024];

	if (datadir[0] && access(datadir, F_OK) == 0)
	{
		sprintf(buf, "rm -rf %s", datadir);
		system(buf);
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
        
        header_buf1 = "\n\nSkySQL MaxScale\t";
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


/** 
 * @node Provides error printing for non-formatted error strings.
 *
 * Parameters:
 * @param do_log - in, use
 *          is printing to log enabled
 *
 * @param do_stderr - in, use
 *          is printing to stderr enabled
 *
 * @param logstr - in, use
 *          string to be printed to log
 *
 * @param fprstr - in, use
 *          string to be printed to stderr
 *
 * @param eno - in, use
 *          errno, if it is set, zero, otherwise
 *
 * @return void
 * 
 */
static void print_log_n_stderr(
        bool     do_log,   /**<! is printing to log enabled */
        bool     do_stderr,/**<! is printing to stderr enabled */
        char*    logstr,   /**<! string to be printed to log */
        char*    fprstr,   /**<! string to be printed to stderr */
        int      eno)      /**<! errno, if it is set, zero, otherwise */
{
        char* log_err = "Error :";
        char* fpr_err = "*\n* Error :";
        char* fpr_end   = "\n*\n";
        
        if (do_log) {
                skygw_log_write_flush(LOGFILE_ERROR,
                                      "%s %s %s %s",
                                      log_err,
                                      logstr,
                                      eno == 0 ? "" : "error :",
                                      eno == 0 ? "" : strerror(eno));
        }
        if (do_stderr) {
                fprintf(stderr,
                        "%s %s %s %s %s",
                        fpr_err,
                        fprstr,
                        eno == 0 ? "" : "error :",
                        eno == 0 ? "" : strerror(eno),
                        fpr_end);
        }
}


static char* get_config_filename(
        char** new_home,
        char*  home_dir)
{
        char* cnf_file_buf = NULL;

        if (home_dir == NULL)
        {
                goto return_cnf_file_buf;
        }
        *new_home = (char*)malloc(PATH_MAX);

        if (realpath(home_dir, *new_home) == NULL)
        {
                int eno = errno;
                errno = 0;
                
                fprintf(stderr,
                        "*\n* Error : Failed to read the home "
                        "directory %s. %s.\n*\n",
                        home_dir,
                        strerror(eno));
                
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to read the "
                        "home directory %s, due "
                        "to %d, %s.",
                        home_dir,
                        eno,
                        strerror(eno));
                free(*new_home);
                *new_home = NULL;
                goto return_cnf_file_buf;
        }

        cnf_file_buf = (char*) malloc(strlen(*new_home)+
                                      strlen("/etc/")+
                                      strlen("MaxScale.cnf")+
                                      1);
        if (cnf_file_buf == NULL)
        {
                goto return_cnf_file_buf;
        }
        sprintf(cnf_file_buf, "%s/etc/MaxScale.cnf", *new_home);
        
        if (access(cnf_file_buf, R_OK) != 0)
        {
                int eno = errno;
                errno = 0;

                if (!daemon_mode)
                {
                        fprintf(stderr,
                                "*\n* Error : Failed to read the configuration "
                                " file %s. %s.\n*\n",
                                cnf_file_buf,
                                strerror(eno));
                }
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to read the configuration file %s due "
                        "to %d, %s.",
                        cnf_file_buf,
                        eno,
                        strerror(eno));
                free(cnf_file_buf);
                cnf_file_buf = NULL;
                free(*new_home);
                *new_home = NULL;
        }
return_cnf_file_buf:
        return cnf_file_buf;
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
 * MaxScale.cnf - the configuration file is located in <maxscale home>/etc
 * 
 * <maxscale home> is resolved in the following order:
 * 1. from '-c <dir>' command-line argument
 * 2. from MAXSCALE_HOME environment variable
 * 3. /etc/ if MaxScale.cnf is found from there
 * 4. current working directory if MaxScale.cnf is found from there
 *
 * <modules directory> is resolved in the following way:
 * 1. from '-m <dir>' command-line argument
 * 2. from LD_LIBRARY_PATH environment variable
 * 3. implicitly from system's libray directories such as /usr/lib64
 *
 * vraa 25.11.13
 *
 */
int main(int argc, char **argv)
{
        int      rc = 0;
        int 	 l;
        int	 i;
        int      n;
        int      n_threads; /**<! number of epoll listener threads */ 
        int      n_services;
        int      eno = 0;   /**<! local variable for errno */
        int      opt;
        void**	 threads;   /**<! thread list */
        char	 mysql_home[1024];
        char	 ddopt[1024];
        char*    home_dir = NULL; /**<! home dir, to be freed */
        char*    cnf_file = NULL; /**<! conf file, to be freed */
        char*    lib_dir  = NULL; /**<! directory for dyn libs */
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
        /**
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
                        rc = 1;
                        goto return_main;
                }
        }

        while ((opt = getopt(argc, argv, "dc:m:")) != -1)
        {
                bool succp = true;
                
                switch (opt) {
                case 'd':
                        /** Debug mode, maxscale runs in this same process */
                        daemon_mode = false;
                        break;
                        
                case 'c':
                        cnf_file = get_config_filename(&home_dir, optarg);

                        if (cnf_file != NULL)
                        {
                                /**
                                 * Setting MAXSCALE_HOME is unnecessary because
                                 * home_dir gets value too and home_dir is the
                                 * one used in the future, but this is to
                                 * make it easier to understand.
                                 */
                                setenv("MAXSCALE_HOME", home_dir, 1);
                                ss_dfprintf(stderr,
                                            "Set MAXSCALE_HOME=%s\n",
                                            getenv("MAXSCALE_HOME")); 
                        }
                        
                        if (home_dir == NULL)
                        {
                                succp = false;
                                char* logerr = "Home directory argument "
                                        "\'-c\' was specified but "
                                        "MaxScale was unable to "
                                        "locate\n* a readable "
                                        "configuration file.\n*\n* "
                                        "Usage : maxscale [-d] [-c "
                                        "<home directory>]";
                                print_log_n_stderr(true, true, logerr, logerr, 0);
                        }
                        break;
                        
                case 'm':
                {
                        char* ldlib_env = getenv("LD_LIBRARY_PATH");
                        char* new_env;
                        char* abs_path = (char*)malloc(PATH_MAX+1);
                        
                        if (abs_path == NULL)
                        {
                                succp = false;
                                break;
                        }
                        
                        if (realpath(optarg, abs_path) == NULL)
                        {
                                int eno = errno;
                                errno = 0;
                                
                                fprintf(stderr,
                                        "*\n* Error : Failed to read the "
                                        "library directory %s. %s.\n*\n",
                                        optarg,
                                        strerror(eno));
                                
                                skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Failed to read the "
                                        "library directory %s, due "
                                        "to %d, %s.",
                                        optarg,
                                        eno,
                                        strerror(eno));
                                succp = false;
                        }
                        else
                        {
                                new_env = (char*)malloc(strlen(ldlib_env)+
                                                        1+
                                                        strlen(abs_path)+
                                                        1);
                                if (new_env == NULL)
                                {
                                        succp = false;
                                        break;
                                }
                                sprintf(new_env, "%s:%s", ldlib_env, abs_path);
                                setenv("LD_LIBRARY_PATH", new_env, 1);
                                free(new_env);                                
                                ss_dfprintf(stderr,
                                            "Set LD_LIBRARY_PATH=%s\n",
                                            getenv("LD_LIBRARY_PATH")); 
                                
                        }
                        free(abs_path);
                }
                break;
                        
                default:
                        fprintf(stderr,
                                "*\n* Usage : maxscale [-d] [-c <home "
                                "directory>] [-m <modules directory>]\n*\n");
                        succp = false;
                        break;
                }
                
                if (!succp)
                {
                        rc = 1;
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
                /**
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
                /*r=1;/**/
                if (r != 0)
                {
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, fprerr, eno);
                        rc = 1;
                        goto return_main;
                }
                r = sigdelset(&sigset, SIGHUP);
                /*r=1;/**/
                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGHUP from the "
                                "signal set of MaxScale. Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = 1;
                        goto return_main;
                }
                r = sigdelset(&sigset, SIGTERM);
                /*r=1;/**/
                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGTERM from the "
                                "signal set of MaxScale. Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = 1;
                        goto return_main;
                }
                r = sigprocmask(SIG_SETMASK, &sigset, NULL);
                /*r=1;/**/
                if (r != 0) {
                        char* logerr = "Failed to set the signal set for MaxScale."
                                " Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = 1;
                        goto return_main;
                }
                gw_daemonize();
        }
        /**
         * Set signal handlers for SIGHUP, SIGTERM, and SIGINT.
         */
        {
                char* fprerr = "Failed to initialize signal handlers. Exiting.";
                char* logerr = NULL;
                l = signal_set(SIGHUP, sighup_handler);
                /*l=1;/**/
                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGHUP. Exiting.");
                        goto sigset_err;
                }
                l = signal_set(SIGTERM, sigterm_handler);
                /*l=1;/**/
                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGTERM. Exiting.");
                        goto sigset_err;
                }
                l = signal_set(SIGINT, sigint_handler);
                /*l=1; /**/
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
                        rc = 1;
                        goto return_main;
                }
        }
        eno = pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask);
        /*eno=EINTR; /**/
        if (eno != 0)
        {
                char* logerr = "Failed to initialise signal mask for MaxScale. "
                        "Exiting.";
                print_log_n_stderr(true, true, logerr, logerr, eno);
                rc = 1;
                goto return_main;
        }
        l = atexit(libmysqld_done);

        if (l != 0) {
                char* fprerr = "Failed to register exit function for\n* "
                        "embedded MySQL library.\n* Exiting.";
                char* logerr = "Failed to register exit function libmysql_done "
                        "for MaxScale. Exiting.";
                print_log_n_stderr(true, true, logerr, fprerr, 0);
                rc = 1;
                goto return_main;                
        }

        /**
         * 1. if home dir wasn't specified in the command-line argument,
         *    read env. variable MAXSCALE_HOME.
         */
        if (home_dir == NULL)
        {
                char* tmp = getenv("MAXSCALE_HOME");
                cnf_file = get_config_filename(&home_dir, tmp);

                if (!daemon_mode)
                {
                        fprintf(stderr, "Found MAXSCALE_HOME = %s\n", tmp);
                }
                if (tmp != NULL && cnf_file == NULL)
                {
                        char* logerr = "Unable to locate MaxScale.cnf from "
                                "directory pointed to by MAXSCALE_HOME. "
                                "Exiting.";
                        print_log_n_stderr(true, true, logerr, logerr, 0);
                }
        }
        /**
         * 2. if home dir wasn't specified in MAXSCALE_HOME,
         *    try access /etc/MaxScale.cnf.
         */
        if (home_dir == NULL)
        {
                char* tmp = "/etc/MaxScale";
                cnf_file = get_config_filename(&home_dir, tmp);
        }
        /**
         * 3. if /etc/MaxScale.cnf didn't exist or wasn't accessible, home
         *    isn't specified. Thus, try to access $PWD/MaxScale.cnf .
         */
        if (home_dir == NULL)
        {
                char* tmp = getenv("PWD");
                cnf_file = get_config_filename(&home_dir, tmp);
        }

        if (home_dir != NULL)
        {
                ss_dassert(cnf_file != NULL);
                sprintf(mysql_home, "%s/mysql", home_dir);
                setenv("MYSQL_HOME", mysql_home, 1);
        }
        else
        {
                char* logstr = "MaxScale couldn't find home directory including "
                        "readable configuration file.\n*\n* Set home directory as "
                        "follows:\n* - specify it as a \'-c <home directory>\' "
                        "command-line argument,\n* - set it to MAXSCALE_HOME "
                        "environment "
                        "variable, or\n* - create directory /etc/MaxScale and "
                        "copy MaxScale.cnf there, or\n* - change to MaxScale "
                        "home directory, and ensure that MaxScale.cnf is "
                        "located in etc/ directory.\n*\n* Exiting.";
                ss_dassert(cnf_file == NULL);
                print_log_n_stderr(true, true, logstr, logstr, 0);
                rc = 1;
                goto return_main;
        }
        /**
         * Set a data directory for the mysqld library, we use
         * a unique directory name to avoid clauses if multiple
         * instances of the gateway are beign run on the same
         * machine.
         */
        sprintf(datadir, "%s/data%d", home_dir, getpid());
        mkdir(datadir, 0777);
        
        /**
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
                argv[3] = "-s"; /**<! store to shared memory */
                argv[4] = "LOGFILE_DEBUG,LOGFILE_TRACE";   /**<! ..these logs */
                argv[5] = "-l"; /**<! write to syslog */
                argv[6] = "LOGFILE_MESSAGE,LOGFILE_ERROR"; /**<! ..these logs */
                argv[7] = NULL;
                skygw_logmanager_init(7, argv);
        }
        
        /* Update the server options */
        for (i = 0; server_options[i]; i++)
        {
                if (!strcmp(server_options[i], "--datadir="))
                {
                        sprintf(ddopt, "--datadir=%s", datadir);
                        server_options[i] = ddopt;
                }
        }

        if (mysql_library_init(num_elements, server_options, server_groups))
        {
                if (!daemon_mode)
                {
                        char* fprerr = "Failed to initialise the MySQL library. "
                                "Exiting.";
                        print_log_n_stderr(false, true, fprerr, fprerr, 0);
                }
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : mysql_library_init failed. It is a "
                        "mandatory component, required by router services and "
                        "the MaxScale core. Error %s, %s : %d. Exiting.",
                        mysql_error(NULL),
                        __FILE__,
                        __LINE__);
                rc = 1;
                goto return_main;
        }
        libmysqld_started = TRUE;

        if (!config_load(cnf_file))
        {
                char* fprerr = "Failed to load MaxScale configuration "
                        "file. Exiting.";
                print_log_n_stderr(false, !daemon_mode, fprerr, fprerr, 0);
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to load MaxScale configuration file %s. "
                        "Exiting.",
                        cnf_file);
                rc = 1;
                goto return_main;
        }
        skygw_log_write(
                LOGFILE_MESSAGE,
                "SkySQL MaxScale (C) SkySQL Ab 2013"); 
        skygw_log_write(
                LOGFILE_MESSAGE,
                "MaxScale is running in process  %i",
                getpid());
    
        poll_init();
    
        /**
         * Start the services that were created above
         */
        n_services = serviceStartAll();
        if (n_services == 0)
        {
                char* logerr = "Failed to start any MaxScale services. Exiting.";
                print_log_n_stderr(true, !daemon_mode, logerr, logerr, 0);
                rc = 1;
                goto return_main;
        }
        /**
         * Start periodic log flusher thread.
         */
        log_flush_timeout_ms = 1000;
        log_flush_thr = thread_start(
                log_flush_cb,
                (void *)&log_flush_timeout_ms);
        /**
         * Start the polling threads, note this is one less than is
         * configured as the main thread will also poll.
         */
        n_threads = config_threadcount();
        threads = (void **)calloc(n_threads, sizeof(void *));
        /**
         * Start server threads.
         */
        for (n = 0; n < n_threads - 1; n++)
        {
                threads[n] = thread_start(poll_waitevents, (void *)(n + 1));
        }
        skygw_log_write(LOGFILE_MESSAGE,
                        "MaxScale started with %d server threads.",
                        config_threadcount());
        /**
         * Serve clients.
         */
        poll_waitevents((void *)0);
        /**
         * Wait server threads' completion.
         */
        for (n = 0; n < n_threads - 1; n++)
        {
                thread_wait(threads[n]);
        }
        free(threads);
        free(home_dir);
        free(cnf_file);
        
        /**
         * Wait the flush thread.
         */
        thread_wait(log_flush_thr);

        /* Stop all the monitors */
        monitorStopAll();
        skygw_log_write(LOGFILE_MESSAGE, "MaxScale is shutting down.");
        datadir_cleanup();
        skygw_log_write(LOGFILE_MESSAGE, "MaxScale shutdown completed.");        

return_main:
        return 0;
} // End of main

/**
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

static void log_flush_cb(
        void* arg)
{
        ssize_t timeout_ms = *(ssize_t *)arg;

        skygw_log_write(LOGFILE_MESSAGE, "Started MaxScale log flusher.");
        while (!do_exit) {
            skygw_log_flush(LOGFILE_ERROR);
            skygw_log_flush(LOGFILE_MESSAGE);
            skygw_log_flush(LOGFILE_TRACE);
            skygw_log_flush(LOGFILE_DEBUG);
            usleep(timeout_ms*1000);
        }
        skygw_log_write(LOGFILE_MESSAGE, "Finished MaxScale log flusher.");
}
