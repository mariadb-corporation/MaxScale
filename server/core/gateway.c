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

/* wrapper for sigaction */
static void signal_set (int sig, void (*handler)(int)) {
	static struct sigaction sigact;
	static int err;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = handler;
	GW_NOINTR_CALL(err = sigaction(sig, &sigact, NULL));
	if (err < 0) {
		skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : sigaction() error %s.",
                        strerror(errno));
		exit(1);
	}
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

#if 0
static char* set_home_and_variables(
        int    argc,
        char** argv)
{
        int   i;
        int   n;
        char* home = NULL;
        bool  home_set = FALSE;

        for (i=1; i<argc; i++) {

            if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--path") == 0){
                int j = 0;
                
                while (argv[n][j] == 0 && j<10) j++;

                if (strnlen(&argv[n][j], 1) == 0) {
                    
                if (strnlen(&argv[n][j], 1) > 0 && access(&argv[n][j], R_OK) == 0) {
                    home = strdup(&argv[n][j]);
                    goto return_home;
                }
            }
        }
        if ((home = getenv("MAXSCALE_HOME")) != NULL)
        {
            sprintf(mysql_home, "%s/mysql", home);
            setenv("MYSQL_HOME", mysql_home, 1);
        }
        
return_home:
        return home;

}
#endif

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
 * The main entry point into the gateway
 *
 * @param argc	The argument count
 * @param argv	The arguments themselves
 */
int
main(int argc, char **argv)
{
        int		daemon_mode = 1;
        sigset_t	sigset;
        int		i, n, n_threads, n_services;
        void		**threads;
        char		mysql_home[1024], buf[1024], *home, *cnf_file = NULL;
        char		ddopt[1024];
        void*           log_flush_thr = NULL;
        ssize_t         log_flush_timeout_ms = 0;
        int 	        l;
        sigset_t        sigpipe_mask;
        sigset_t        saved_mask;

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
        
        l = atexit(skygw_logmanager_exit);

        if (l != 0) {
                fprintf(stderr, "* Couldn't register exit function.\n");
        }
        atexit(datadir_cleanup);
        atexit(write_footer);
        
        for (n = 0; n < argc; n++)
        {
                if (strcmp(argv[n], "-d") == 0)
                {
                        /** Debug mode, maxscale runs in this same process */
                        daemon_mode = 0;
                }
                /**
                 * 1. Resolve config file location from command-line argument.
                 */
                if (strncmp(argv[n], "-c", 2) == 0)
                {
                        int s=2;
                    
                        while (argv[n][s] == 0 && s<10) s++;
                    
                        if (s==10) {
                                skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Fatal : Unable to find a MaxScale "
                                        "configuration file, either install one "
                                        "in /etc/MaxScale.cnf, "
                                        "$MAXSCALE_HOME/etc/MaxScale.cnf "
                                        "or use the -c option. Exiting.");
                        }
                        cnf_file = &argv[n][s];
                }
        }
        
        /**
         * Maxscale must be daemonized before opening files, initializing
         * embedded MariaDB and in general, as early as possible.
         */
        if (daemon_mode == 1)
        {
                if (sigfillset(&sigset) != 0) {
                        skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : sigfillset() error %s",
                                strerror(errno));
                        return 1;
                }
            
                if (sigdelset(&sigset, SIGHUP) != 0) {
                        skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : sigdelset(SIGHUP) error %s",
                                strerror(errno));
                }
            
                if (sigdelset(&sigset, SIGTERM) != 0) {
                        skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : sigdelset(SIGTERM) error %s",
                                strerror(errno));
                }
            
                if (sigprocmask(SIG_SETMASK, &sigset, NULL) != 0) {
                        skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : sigprocmask() error %s",
                                strerror(errno));
                }
                gw_daemonize();
        }
            
        signal_set(SIGHUP, sighup_handler);
        signal_set(SIGTERM, sigterm_handler);
        signal_set(SIGINT, sigint_handler);

        if (pthread_sigmask(SIG_BLOCK, &sigpipe_mask, &saved_mask) == -1) {
                perror("pthread_sigmask");
                exit(1);
        }
        l = atexit(libmysqld_done);

        if (l != 0) {
                fprintf(stderr, "Couldn't register exit function.\n");
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Couldn't register exit function.");
        }
        
        if ((home = getenv("MAXSCALE_HOME")) != NULL)
        {
                if (access(home, R_OK) != 0)
                {                    
                        fprintf(stderr,
                                "The configured value of MAXSCALE_HOME '%s' "
                                "does not exist.\n",
                                home);
                        skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Fatal : The configured value of MAXSCALE_HOME "
                                "'%s' does not exist.",
                                home);
                        exit(1);
                }
                sprintf(mysql_home, "%s/mysql", home);
                setenv("MYSQL_HOME", mysql_home, 1);
                /**
                 * 2. Resolve config file location from $MAXSCALE_HOME/etc.
                 */
                if (cnf_file == NULL) {
                        sprintf(buf, "%s/etc/MaxScale.cnf", home);
                        if (access(buf, R_OK) == 0) {
                                cnf_file = buf;
                        }
                }
        }
        /**
         * If not done yet, 
         * 3. Resolve config file location from /etc/MaxScale.
         */
        if (cnf_file == NULL && access("/etc/MaxScale.cnf", R_OK) == 0)
                cnf_file = "/etc/MaxScale.cnf";

        /*
         * Set a data directory for the mysqld library, we use
         * a unique directory name to avoid clauses if multiple
         * instances of the gateway are beign run on the same
         * machine.
         */
        if (home)
        {
                sprintf(datadir, "%s/data%d", home, getpid());
                mkdir(datadir, 0777);
        }
        else
        {
                sprintf(datadir, "/tmp/MaxScale/data%d", getpid());
                mkdir("/tmp/MaxScale", 0777);
                mkdir(datadir, 0777);
        }
        
        /*
         * If $MAXSCALE_HOME is set then write the logs into $MAXSCALE_HOME/log.
         * The skygw_logmanager_init expects to take arguments as passed to main
         * and proesses them with getopt, therefore we need to give it a dummy
         * argv[0]
         */
        if (home)
        {
                char 	buf[1024];
                char	*argv[8];

                sprintf(buf, "%s/log", home);
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

        if (cnf_file == NULL) {
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Fatal : Unable to find a MaxScale configuration "
                        "file, either install one in /etc/MaxScale.cnf, "
                        "$MAXSCALE_HOME/etc/MaxScale.cnf "
                        "or use the -c option. Exiting.");
                fprintf(stderr, "* Unable to find MaxScale configuration file. "
                        "Exiting.\n");
                exit(1);
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
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Fatal : mysql_library_init failed. It is a "
                        "mandatory component, required by router services and "
                        "the MaxScale core. Error %s, %s : %d. Exiting.",
                        mysql_error(NULL),
                        __FILE__,
                        __LINE__);
                fprintf(stderr,
                        "Failed to initialise the MySQL library. Exiting.\n");
                exit(1);
        }
        libmysqld_started = TRUE;

        if (!config_load(cnf_file))
        {
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to load MaxScale configuration file %s. "
                        "Exiting.",
                        cnf_file);
                fprintf(stderr,
                        "* Failed to load MaxScale configuration file. "
                        "Exiting.\n");
                exit(1);
        }

        skygw_log_write(
                LOGFILE_MESSAGE,
                "SkySQL MaxScale (C) SkySQL Ab 2013"); 
        skygw_log_write(
                LOGFILE_MESSAGE,
                "MaxScale is starting, PID %i",
                getpid());
    
        poll_init();
    
        /*
         * Start the services that were created above
         */
        n_services = serviceStartAll();
        if (n_services == 0)
        {
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Fatal : Failed to start any MaxScale services. "
                        "Exiting.");
                fprintf(stderr,
                        "* Failed to start any MaxScale services. Exiting.\n");
                exit(1);
        }
        skygw_log_write(
                LOGFILE_MESSAGE,
                "Started %d services succesfully.",
                n_services);

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

        /**
         * Wait the flush thread.
         */
        thread_wait(log_flush_thr);

        /* Stop all the monitors */
        monitorStopAll();
        skygw_log_write(LOGFILE_MESSAGE, "MaxScale is shutting down.");
        datadir_cleanup();
        skygw_log_write(LOGFILE_MESSAGE, "MaxScale shutdown completed.");        

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
