/*
 * This file is distributed as part of the MariaDB Corporation MaxScale. It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
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
#include <my_config.h>
#include <ftw.h>
#include <string.h>
#include <strings.h>
#include <gw.h>
#include <unistd.h>
#include <time.h>
#include <getopt.h>
#include <service.h>
#include <server.h>
#include <dcb.h>
#include <session.h>
#include <modules.h>
#include <maxconfig.h>
#include <poll.h>
#include <housekeeper.h>
#include <service.h>
#include <memlog.h>

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

#include <execinfo.h>

#include <ini.h>

/** for procname */
#if !defined(_GNU_SOURCE)
#  define _GNU_SOURCE
#endif

time_t	MaxScaleStarted;

extern char *program_invocation_name;
extern char *program_invocation_short_name;

/**
 * Variable holding the enabled logfiles information.
 * Used from log users to check enabled logs prior calling
 * actual library calls such as skygw_log_write.
 */
/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

/*
 * Server options are passed to the mysql_server_init. Each gateway must have a unique
 * data directory that is passed to the mysql_server_init, therefore the data directory
 * is not fixed here and will be updated elsewhere.
 */
static char* server_options[] = {
    "MariaDB Corporation MaxScale",
    "--no-defaults",
    "--datadir=",
    "--language=",
    "--skip-innodb",
    "--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

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
static bool	datadir_defined = false; /*< If the datadir was already set */
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

const char *progname = NULL;
static struct option long_options[] = {
  {"homedir",  required_argument, 0, 'c'},
  {"config",   required_argument, 0, 'f'},
  {"nodaemon", no_argument,       0, 'd'},
  {"log",      required_argument, 0, 'l'},
  {"logdir",   required_argument, 0, 'L'},
  {"datadir",  required_argument, 0, 'D'},
  {"configdir",required_argument, 0, 'C'},
  {"piddir",required_argument, 0, 'P'},
  {"libdir",required_argument, 0, 'B'},
  {"cachedir",required_argument, 0, 'A'},
  {"language",required_argument, 0, 'N'},
  {"syslog",   required_argument, 0, 's'},
  {"maxscalelog",required_argument,0,'S'},
  {"user",required_argument,0,'U'},
  {"version",  no_argument,       0, 'v'},
  {"help",     no_argument,       0, '?'},
  {0, 0, 0, 0}
};
static int cnf_preparser(void* data, const char* section, const char* name, const char* value);
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
bool handle_path_arg(char** dest, char* path, char* arg, bool rd, bool wr);
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

static char* check_dir_access(char* dirname,bool,bool);
static int set_user();

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

/**
 * Handler for SIGUSR1 signal. A SIGUSR1 signal will cause 
 * maxscale to rotate all log files.
 */
static void sigusr1_handler (int i)
{
	LOGIF(LM, (skygw_log_write(
                LOGFILE_MESSAGE,
                "Log file flush following reception of SIGUSR1\n")));
	skygw_log_rotate(LOGFILE_ERROR);
	skygw_log_rotate(LOGFILE_MESSAGE);
	skygw_log_rotate(LOGFILE_TRACE);
	skygw_log_rotate(LOGFILE_DEBUG);
}

static void sigterm_handler (int i) {
        extern void shutdown_server();
        
	LOGIF(LE, (skygw_log_write_flush(
                LOGFILE_ERROR,
                "MaxScale received signal SIGTERM. Exiting.")));
	skygw_log_sync_all();
	shutdown_server();
}

static void
sigint_handler (int i)
{
        extern void shutdown_server();

	LOGIF(LE, (skygw_log_write_flush(
                LOGFILE_ERROR,
                "MaxScale received signal SIGINT. Shutting down.")));
	skygw_log_sync_all();
	shutdown_server();
	fprintf(stderr, "\n\nShutting down MaxScale\n\n");
}


int fatal_handling = 0;

static int signal_set (int sig, void (*handler)(int));

static void
sigfatal_handler (int i)
{
	if (fatal_handling) {
		fprintf(stderr, "Fatal signal %d while backtracing\n", i);
		_exit(1);
	}
	fatal_handling = 1;

	fprintf(stderr, "\n\nMaxScale received fatal signal %d\n", i);

	LOGIF(LE, (skygw_log_write_flush(
                LOGFILE_ERROR,
                "Fatal: MaxScale received fatal signal %d. Attempting backtrace.", i)));

	{
		void *addrs[128];
		int n, count = backtrace(addrs, 128);
		char** symbols = backtrace_symbols( addrs, count );

		if (symbols) {
			for( n = 0; n < count; n++ ) {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"  %s\n", symbols[n])));
			}
			free(symbols);
		} else {
			fprintf(stderr, "\nresolving symbols to error log failed, writing call trace to stderr:\n");
			backtrace_symbols_fd(addrs, count, fileno(stderr));
		}
	}

	skygw_log_sync_all();

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

        if (datadir[0] != 0 && access(datadir, F_OK) == 0)
        {
                nftw(datadir, ntfw_cb, depth, flags);
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
        size_t      len1;
        const char* header_buf1;

        header_buf1 = "------------------------------------------------------"
            "\n\n"; 
        len1 = strlen(header_buf1);
        fwrite((void*)header_buf1, len1, 1, outfile);

        succp = true;

        return succp;
}

static bool file_write_header(
        FILE*       outfile)
{
        bool        succp = false;
        size_t      len1;
        size_t      len2;
        size_t      len3;
        const char* header_buf1;
        char*       header_buf2 = NULL;
        const char* header_buf3;
        time_t*     t  = NULL;
        struct tm*  tm = NULL;
#if defined(LAPTOP_TEST)
	struct timespec ts1;
	ts1.tv_sec = 0;
	ts1.tv_nsec = DISKWRITE_LATENCY*1000000;
#endif

#if !defined(SS_DEBUG)
	return true;
#endif	

        if ((t = (time_t *)malloc(sizeof(time_t))) == NULL) {
                goto return_succp;
        }
        
        if ((tm = (struct tm *)malloc(sizeof(struct tm))) == NULL) {
                goto return_succp;
        }
        
        *t = time(NULL); 
        *tm = *localtime(t);
        
        header_buf1 = "\n\nMariaDB Corporation MaxScale " MAXSCALE_VERSION "\t";
        header_buf2 = strdup(asctime(tm));

        if (header_buf2 == NULL) {
                goto return_succp;
        }
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
                *cnf_full_path = get_expanded_pathname(NULL,
                                                       home_dir,
                                                       cnf_file_arg);

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
                else 
		{
			/** Allocate memory for use of realpath */
			*cnf_full_path = (char *)malloc(PATH_MAX+1);
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

/**
 * Check read and write accessibility to a directory.
 * @param dirname	directory to be checked
 * 
 * @return NULL if directory can be read and written, an error message if either 
 * 	read or write is not permitted. 
 */
static char* check_dir_access(
	char* dirname, bool rd, bool wr)
{
    char errbuf[PATH_MAX*2];
	char* errstr = NULL;
	
	if (dirname == NULL)
	{
		errstr = strdup("Directory argument is NULL");
		goto retblock;
	}

	if(access(dirname,F_OK) != 0)
	{
	    sprintf(errbuf,"Can't access '%s'.",dirname);
		errstr = strdup(errbuf);
		goto retblock;
	}

	if (rd && !file_is_readable(dirname))
	{
	    sprintf(errbuf,"MaxScale doesn't have read permission "
				"to '%s'.",dirname);
		errstr = strdup(errbuf);
		goto retblock;
	}
	
	if (wr && !file_is_writable(dirname))
	{
	    	    sprintf(errbuf,"MaxScale doesn't have write permission "
				"to '%s'.",dirname);
		errstr = strdup(errbuf);
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
		LOGIF(LE,(skygw_log_sync_all()));
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
			ss_dassert(cnf_file_buf != NULL);
			
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Error : Memory allocation failed due to %s.", 
				strerror(errno))));		
			
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
                "\nUsage : %s [OPTION]...\n\n"
		"  -d, --nodaemon             enable running in terminal process (default:disabled)\n"
                "  -f, --config=FILE          relative|absolute pathname of MaxScale configuration file\n"
		"                             (default:/etc/maxscale.cnf)\n"
		"  -l, --log=[file|shm]       log to file or shared memory (default: shm)\n"
		"  -L, --logdir=PATH          path to log file directory\n"
		"                             (default: /var/log/maxscale)\n"
		"  -A, --cachedir=PATH        path to cache directory\n"
		"                             (default: /var/cache/maxscale)\n"
		"  -B, --libdir=PATH          path to module directory\n"
		"                             (default: /usr/lib64/maxscale)\n"
		"  -C, --configdir=PATH       path to configuration file directory\n"
		"                             (default: /etc/)\n"
		"  -D, --datadir=PATH         path to data directory, stored embedded mysql tables\n"
		"                             (default: /var/cache/maxscale)\n"
		"  -P, --piddir=PATH	      path to PID file directory\n"
		"                             (default: /var/run/maxscale)\n"
		"  -U, --user=USER	      run MaxScale as another user.\n"
		"                             The user ID and group ID of this user are used to run MaxScale.\n"
		"  -s, --syslog=[yes|no]      log messages to syslog (default:yes)\n"
		"  -S, --maxscalelog=[yes|no] log messages to MaxScale log (default: yes)\n"
		"  -v, --version              print version info and exit\n"
                "  -?, --help                 show this help\n"
		, progname);
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
 * The configuration file is by default /etc/maxscale.cnf
 * The name of configuration file and its location can be specified by
 * command-line argument.
 * 
 * \<config filename\> is resolved in the following order:
 * 1. from '-f \<config filename\>' command-line argument
 * 2. by using default value "maxscale.cnf"
 *
 */
int main(int argc, char **argv)
{
        int      rc = MAXSCALE_SHUTDOWN;
        int 	 l;
        int	 i;
        int      n;
	intptr_t thread_id;
        int      n_threads; /*< number of epoll listener threads */ 
        int      n_services;
        int      eno = 0;   /*< local variable for errno */
        int      opt;
        void**	 threads = NULL;   /*< thread list */
        char	 mysql_home[PATH_MAX+1];
        char	 datadir_arg[10+PATH_MAX+1];  /*< '--datadir='  + PATH_MAX */
        char     language_arg[11+PATH_MAX+1]; /*< '--language=' + PATH_MAX */
        char*    home_dir = NULL;             /*< home dir, to be freed */
        char*    cnf_file_path = NULL;        /*< conf file, to be freed */
        char*    cnf_file_arg = NULL;         /*< conf filename from cmd-line arg */
        void*    log_flush_thr = NULL;
	char* tmp_path;
	char* tmp_var;
	int      option_index;
	int	 logtofile = 0;	      	      /* Use shared memory or file */
	int	 syslog_enabled = 1; /** Log to syslog */
	int	 maxscalelog_enabled = 1; /** Log with MaxScale */
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
	progname = *argv;
        sprintf(datadir, "%s", default_datadir);
#if defined(FAKE_CODE)
        memset(conn_open, 0, sizeof(bool)*10240);
        memset(dcb_fake_write_errno, 0, sizeof(unsigned char)*10240);
        memset(dcb_fake_write_ev, 0, sizeof(__int32_t)*10240);
        fail_next_backend_fd = false;
        fail_next_client_fd = false;
        fail_next_accept = 0;
        fail_accept_errno = 0;
#endif /* FAKE_CODE */
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

        while ((opt = getopt_long(argc, argv, "dc:f:l:vs:S:?L:D:C:B:U:A:P:",
				 long_options, &option_index)) != -1)
        {
                bool succp = true;

                switch (opt) {
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
			
		case 'v':
		  rc = EXIT_SUCCESS;
          printf("%s\n",MAXSCALE_VERSION);
                  goto return_main;		  

		case 'l':
			if (strncasecmp(optarg, "file", PATH_MAX) == 0)
				logtofile = 1;
			else if (strncasecmp(optarg, "shm", PATH_MAX) == 0)
				logtofile = 0;
			else
			{
                                char* logerr = "Configuration file argument "
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
	
		    if(handle_path_arg(&tmp_path,optarg,NULL,true,false))
		    {
			logdir = tmp_path;
		    }

		    break;
		case 'N':
		    if(handle_path_arg(&tmp_path,optarg,NULL,true,false))
		    {
			langdir = tmp_path;
		    }
		    break;
		case 'P':
		    if(handle_path_arg(&tmp_path,optarg,NULL,true,true))
		    {
			piddir = tmp_path;
		    }
		    break;
		case 'D':
		    sprintf(datadir,"%s",optarg);
		    maxscaledatadir = strdup(optarg);
		    datadir_defined = true;
		    break;
		case 'C':
		    if(handle_path_arg(&tmp_path,optarg,NULL,true,false))
		    {
			configdir = tmp_path;
		    }
		    break;
		case 'B':
		    if(handle_path_arg(&tmp_path,optarg,NULL,true,false))
		    {
			libdir = tmp_path;
		    }
		    break;
		case 'A':
		    if(handle_path_arg(&tmp_path,optarg,NULL,true,true))
		    {
			cachedir = tmp_path;
		    }
		    break;
		case 'S':
		    if(strstr(optarg,"="))
		    {
			strtok(optarg,"= ");
			maxscalelog_enabled = config_truth_value(strtok(NULL,"= "));
		    }
		    else
		    {
			maxscalelog_enabled = config_truth_value(optarg);
		    }
		    break;
		case 's':
		    if(strstr(optarg,"="))
		    {
			strtok(optarg,"= ");
			syslog_enabled = config_truth_value(strtok(NULL,"= "));
		    }
		    else
		    {
			syslog_enabled = config_truth_value(optarg);
		    }
		    break;
		case 'U':
		    if(set_user(optarg) != 0)
		    {
			succp = false;
		    }
		    break;
		case '?':
		  usage();
		  rc = EXIT_SUCCESS;
                  goto return_main;		  
                        
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
                        "Info : MaxScale will be run in the terminal process.\n");
#if defined(SS_DEBUG)
                fprintf(stderr,
                        "\tSee "
                        "the log from the following log files : \n\n");
#endif
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
#if defined(SS_DEBUG)
                fprintf(stderr,
                        "Info :  MaxScale will be run in a daemon process.\n\tSee "
                        "the log from the following log files : \n\n");
#endif
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
                r = sigdelset(&sigset, SIGUSR1);

                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGUSR1 from the "
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
                r = sigdelset(&sigset, SIGSEGV);

                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGSEGV from the "
                                "signal set of MaxScale. Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
                r = sigdelset(&sigset, SIGABRT);

                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGABRT from the "
                                "signal set of MaxScale. Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
                r = sigdelset(&sigset, SIGILL);

                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGILL from the "
                                "signal set of MaxScale. Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
                r = sigdelset(&sigset, SIGFPE);

                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGFPE from the "
                                "signal set of MaxScale. Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
#ifdef SIGBUS
                r = sigdelset(&sigset, SIGBUS);

                if (r != 0)
                {
                        char* logerr = "Failed to delete signal SIGBUS from the "
                                "signal set of MaxScale. Exiting.";
                        eno = errno;
                        errno = 0;
                        print_log_n_stderr(true, true, fprerr, logerr, eno);
                        rc = MAXSCALE_INTERNALERROR;
                        goto return_main;
                }
#endif
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
         * Set signal handlers for SIGHUP, SIGTERM, SIGINT and critical signals like SIGSEGV.
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
                l = signal_set(SIGUSR1, sigusr1_handler);

                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGUSR1. Exiting.");
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
                l = signal_set(SIGSEGV, sigfatal_handler);

                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGSEGV. Exiting.");
                        goto sigset_err;
                }
                l = signal_set(SIGABRT, sigfatal_handler);

                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGABRT. Exiting.");
                        goto sigset_err;
                }
                l = signal_set(SIGILL, sigfatal_handler);

                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGILL. Exiting.");
                        goto sigset_err;
                }
                l = signal_set(SIGFPE, sigfatal_handler);

                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGFPE. Exiting.");
                        goto sigset_err;
                }
#ifdef SIGBUS
                l = signal_set(SIGBUS, sigfatal_handler);

                if (l != 0)
                {
                        logerr = strdup("Failed to set signal handler for "
                                        "SIGBUS. Exiting.");
                        goto sigset_err;
                }
#endif
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

        /**
         * Resolve the full pathname for configuration file and check for
         * read accessibility.
         */
	char pathbuf[PATH_MAX+1];
	snprintf(pathbuf,PATH_MAX,"%s",configdir ? configdir:default_configdir);
	if(pathbuf[strlen(pathbuf)-1] != '/')
	    strcat(pathbuf,"/");

        if (!resolve_maxscale_conf_fname(&cnf_file_path, pathbuf, cnf_file_arg))
        {
                ss_dassert(cnf_file_path == NULL);
                rc = MAXSCALE_BADCONFIG;
                goto return_main;
        }

	ini_parse(cnf_file_path,cnf_preparser,NULL);

	if(!datadir_defined)
	    sprintf(datadir,"%s",default_datadir);


        /** Use the cache dir for the mysql folder of the embedded library */
	sprintf(mysql_home, "%s/mysql", cachedir?cachedir:default_cachedir);
	setenv("MYSQL_HOME", mysql_home, 1);


        /**
         * Init Log Manager for MaxScale.
         * The skygw_logmanager_init expects to take arguments as passed to main
         * and proesses them with getopt, therefore we need to give it a dummy
         * argv[0]
         */
        {
                char buf[1024];
                char *argv[8];
		bool succp;
		
		/** Use default log directory /var/log/maxscale/ */
		if(logdir == NULL)
		{

		    if(access(default_logdir,F_OK) != 0)
		    {
			if(mkdir(logdir,0555) != 0)
			{
			    fprintf(stderr,
			     "Error: Cannot create log directory: %s\n",
			     default_logdir);
			    goto return_main;
			}
		    }
		    logdir = strdup(default_logdir);
		}

                argv[0] = "MaxScale";
                argv[1] = "-j";
                argv[2] = logdir;

		if(!syslog_enabled)
		{
		    printf("Syslog logging is disabled.\n");
		}
		
		if(!maxscalelog_enabled)
		{
		    printf("MaxScale logging is disabled.\n");
		}
		logmanager_enable_syslog(syslog_enabled);
		logmanager_enable_maxscalelog(maxscalelog_enabled);

		if (logtofile)
		{
			argv[3] = "-l"; /*< write to syslog */
			/** Logs that should be syslogged */
			argv[4] = "LOGFILE_MESSAGE,LOGFILE_ERROR"
				"LOGFILE_DEBUG,LOGFILE_TRACE"; 
			argv[5] = NULL;
			succp = skygw_logmanager_init(5, argv);
		}
		else
		{
			argv[3] = "-s"; /*< store to shared memory */
			argv[4] = "LOGFILE_DEBUG,LOGFILE_TRACE"; /*< to shm */
			argv[5] = "-l"; /*< write to syslog */
			argv[6] = "LOGFILE_MESSAGE,LOGFILE_ERROR"; /*< to syslog */
			argv[7] = NULL;
			succp = skygw_logmanager_init(7, argv);
		}
		
		if (!succp)
		{
			rc = MAXSCALE_BADCONFIG;
			goto return_main;
		}
        }



	    if(cachedir == NULL)
		cachedir = strdup(default_cachedir);
	    if(langdir == NULL)
		langdir = strdup(default_langdir);
	    if(libdir == NULL)
		libdir = strdup(default_libdir);
	/**
         * Set a data directory for the mysqld library, we use
         * a unique directory name to avoid clauses if multiple
         * instances of the gateway are beign run on the same
         * machine.
         */

	if(datadir[strlen(datadir)-1] != '/')
	    strcat(datadir,"/");
	strcat(datadir,"data");
		if(mkdir(datadir, 0777) != 0){

			if(errno != EEXIST){
				fprintf(stderr,
						"Error: Cannot create data directory: %s\n",datadir);
				goto return_main;
			}
		}

        sprintf(datadir, "%s/data%d", datadir, getpid());

		if(mkdir(datadir, 0777) != 0){

			if(errno != EEXIST){
				fprintf(stderr,
						"Error: Cannot create data directory: %s\n",datadir);
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
                        logdir,
                        datadir,
			libdir,
			cachedir);
        }

        LOGIF(LM,
	      (skygw_log_write_flush(
		LOGFILE_MESSAGE,
			       "Configuration file: %s",
			       cnf_file_path)));
        LOGIF(LM,
	      (skygw_log_write_flush(
		LOGFILE_MESSAGE,
			       "Log directory: %s/",
			       logdir)));
	LOGIF(LM,
	 (skygw_log_write_flush(
		LOGFILE_MESSAGE,
			  "Data directory: %s",
			  datadir)));
	LOGIF(LM,
	 (skygw_log_write_flush(LOGFILE_MESSAGE,
			  "Module directory: %s",
			  libdir)));
	LOGIF(LM,
	 (skygw_log_write_flush(LOGFILE_MESSAGE,
			  "Service cache: %s",
			  cachedir)));

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
                                 "--language=%s",
                                 langdir);
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

	if(libdir == NULL)
	    libdir = strdup(default_libdir);

        if (!config_load(cnf_file_path))
        {
                char* fprerr = "Failed to load MaxScale configuration "
                        "file. Exiting. See the error log for details.";
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
                "MariaDB Corporation MaxScale %s (C) MariaDB Corporation Ab 2013-2014",
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

	/*
	 * Start the housekeeper thread
	 */
	hkinit();

        /*<
         * Start the polling threads, note this is one less than is
         * configured as the main thread will also poll.
         */
        n_threads = config_threadcount();
        threads = (void **)calloc(n_threads, sizeof(void *));
        /*<
         * Start server threads.
         */
        for (thread_id = 0; thread_id < n_threads - 1; thread_id++)
        {
                threads[thread_id] = thread_start(poll_waitevents, (void *)(thread_id + 1));
        }
        LOGIF(LM, (skygw_log_write(LOGFILE_MESSAGE,
                        "MaxScale started with %d server threads.",
                                   config_threadcount())));

	MaxScaleStarted = time(0);
        /*<
         * Serve clients.
         */
        poll_waitevents((void *)0);

        /*<
         * Wait server threads' completion.
         */
        for (thread_id = 0; thread_id < n_threads - 1; thread_id++)
        {
                thread_wait(threads[thread_id]);
        }        
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

	unload_all_modules();
	/* Remove Pidfile */
	unlink_pidfile();
	
return_main:
	if (threads)
		free(threads);
	if (home_dir)
		free(home_dir);
	if (cnf_file_path)
		free(cnf_file_path);

        return rc;
} /*< End of main */

/*<
 * Shutdown MaxScale server
 */
void
shutdown_server()
{
	service_shutdown();
	poll_shutdown();
	hkshutdown();
	memlog_flush_all();
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
	struct timespec ts1;

	ts1.tv_sec = timeout_ms/1000;
	ts1.tv_nsec = (timeout_ms%1000)*1000000;
	
        LOGIF(LM, (skygw_log_write(LOGFILE_MESSAGE,
                                   "Started MaxScale log flusher.")));
        while (!do_exit) {
            skygw_log_flush(LOGFILE_ERROR);
            skygw_log_flush(LOGFILE_MESSAGE);
            skygw_log_flush(LOGFILE_TRACE);
            skygw_log_flush(LOGFILE_DEBUG);
	    nanosleep(&ts1, NULL);
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
		if (unlink(pidfile)) 
		{
			fprintf(stderr, 
				"MaxScale failed to remove pidfile %s: error %d, %s\n", 
				pidfile, 
				errno, 
				strerror(errno));
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

        snprintf(pidfile, PATH_MAX, "%smaxscale.pid",piddir?piddir:default_piddir);

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
        }

	/* success */
	return 0;
}

int
MaxScaleUptime()
{
	return time(0) - MaxScaleStarted;
}

bool handle_path_arg(char** dest, char* path, char* arg, bool rd, bool wr)
{
        char pathbuffer[PATH_MAX+2];
	char* errstr;
	bool rval = false;

	if(path == NULL && arg == NULL)
	    return rval;

	if(path)
	{
	    snprintf(pathbuffer,PATH_MAX,"%s",path);
	    if(pathbuffer[strlen(path) - 1] != '/')
	    {
		strcat(pathbuffer,"/");
	    }
	    if(arg && strlen(pathbuffer) + strlen(arg) + 1 < PATH_MAX)
		strcat(pathbuffer,arg);

	    if((errstr = check_dir_access(pathbuffer,rd,wr)) == NULL)
	    {
		*dest = strdup(pathbuffer);
		rval = true;
	    }
	    else
	    {
		fprintf(stderr,"%s\n",errstr);
		free(errstr);
		errstr = NULL;
	    }
	}

	return rval;
}

/**
 * Pre-parse the MaxScale.cnf for config, log and module directories.
 * @param data Parameter passed by inih
 * @param section Section name
 * @param name Parameter name
 * @param value Parameter value
 * @return 1 in all cases
 */
static int cnf_preparser(void* data, const char* section, const char* name, const char* value)
{

    char pathbuffer[PATH_MAX];
    char* errstr;

    /** These are read from the configuration file. These will not override
     * command line parameters but will override default values. */
    if(strcasecmp(section,"maxscale") == 0)
    {
	if(strcmp(name, "logdir") == 0)
	{
	    if(logdir == NULL)
		handle_path_arg(&logdir,(char*)value,NULL,true,true);
	}
	else if(strcmp(name, "libdir") == 0)
	{
	    if(libdir == NULL)
		handle_path_arg(&libdir,(char*)value,NULL,true,false);
	}
	else if(strcmp(name, "piddir") == 0)
	{
	    if(piddir == NULL)
		handle_path_arg(&piddir,(char*)value,NULL,true,true);
	}
	else if(strcmp(name, "datadir") == 0)
	{
	    if(!datadir_defined)
	    {
		char* tmp;
		if(handle_path_arg(&tmp,(char*)value,NULL,true,false))
		{
		    sprintf(datadir,"%s",tmp);
		    maxscaledatadir = strdup(tmp);
		    datadir_defined = true;
		    free(tmp);
		}
	    }
	}
	else if(strcmp(name, "cachedir") == 0)
	{
	    if(cachedir == NULL)
		handle_path_arg((char**)&cachedir,(char*)value,NULL,true,false);
	}
	else if(strcmp(name, "language") == 0)
	{
	    if(langdir == NULL)
		handle_path_arg((char**)&langdir,(char*)value,NULL,true,false);
	}
    }

    return 1;
}

static int set_user(char* user)
{
    errno = 0;
    struct passwd *pwname;
    int rval;

    pwname = getpwnam(user);
    if(pwname == NULL)
    {
	printf("Error: Failed to retrieve user information for '%s': %d %s\n",
	 user,errno,errno == 0 ? "User not found" : strerror(errno));
	return -1;
    }
    
    rval = setgid(pwname->pw_gid);
    if(rval != 0)
    {
	printf("Error: Failed to change group to '%d': %d %s\n",
	 pwname->pw_gid,errno,strerror(errno));
	return rval;
    }

    rval = setuid(pwname->pw_uid);
    if(rval != 0)
    {
	printf("Error: Failed to change user to '%s': %d %s\n",
	 pwname->pw_name,errno,strerror(errno));
	return rval;
    }
#ifdef SS_DEBUG
    else
    {
	printf("Running MaxScale as: %s %d:%d\n",pwname->pw_name,pwname->pw_uid,pwname->pw_gid);
    }
#endif


    return rval;
}
