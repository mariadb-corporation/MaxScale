/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file maxadmin.c  - The MaxScale administration client
 *
 * @verbatim
 * Revision History
 *
 * Date        Who                   Description
 * 13/06/14    Mark Riddoch          Initial implementation
 * 15/06/14    Mark Riddoch          Addition of source command
 * 26/06/14    Mark Riddoch          Fix issue with final OK split across
 *                                   multiple reads
 * 17/05/16    Massimiliano Pinto    Addition of UNIX socket support
 *
 * @endverbatim
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <locale.h>
#include <errno.h>
#include <getopt.h>

#include <version.h>

#ifdef HISTORY
#include <histedit.h>
#endif

#include <maxadmin.h>
/*
 * We need a common.h file that is included by every component.
 */
#if !defined(STRERROR_BUFLEN)
#define STRERROR_BUFLEN 512
#endif

static int connectMaxScale(char *socket);
static int setipaddress(struct in_addr *a, char *p);
static int authMaxScale(int so);
static int sendCommand(int so, char *cmd);
static void DoSource(int so, char *cmd);
static void DoUsage(const char*);
static int isquit(char *buf);
static void PrintVersion(const char *progname);
static void read_inifile(char **, int*);

#ifdef HISTORY

static char *
prompt(EditLine *el __attribute__((__unused__)))
{
    static char prompt[] = "MaxScale> ";

    return prompt;
}
#endif

static struct option long_options[] =
{
    {"socket", required_argument, 0, 'S'},
    {"version", no_argument, 0, 'v'},
    {"help", no_argument, 0, '?'},
    {"emacs", no_argument, 0, 'e'},
    {0, 0, 0, 0}
};

/**
 * The main for the maxadmin client
 *
 * @param argc  Number of arguments
 * @param argv  The command line arguments
 */
int
main(int argc, char **argv)
{
    const char* vi = "vi";
    const char* emacs = "emacs";

    int i, num, rv;
#ifdef HISTORY
    char *buf;
    EditLine *el = NULL;
    Tokenizer *tok;
    History *hist;
    HistEvent ev;
#else
    char buf[1024];
#endif
    char *conn_socket = MAXADMIN_DEFAULT_SOCKET;
    int use_emacs = 0;
    int so;
    int option_index = 0;
    char c;

    read_inifile(&conn_socket, &use_emacs);

    while ((c = getopt_long(argc, argv, "S:v?e",
                            long_options, &option_index)) >= 0)
    {
        switch (c)
        {
            case 'S':
                conn_socket = strdup(optarg);
                break;

            case 'v':
                PrintVersion(*argv);
                exit(EXIT_SUCCESS);

            case 'e':
                use_emacs = 1;
                break;

            case '?':
                DoUsage(*argv);
                exit(optopt ? EXIT_FAILURE : EXIT_SUCCESS);
        }
    }

    /* Connet to MaxScale using UNIX domain socket */
    if ((so = connectMaxScale(conn_socket)) == -1)
    {
        exit(1);
    }

    /* Check for successful authentication */
    if (!authMaxScale(so))
    {
        fprintf(stderr, "Failed to connect to MaxScale, incorrect username.\n");
        exit(1);
    }

    if (optind < argc)
    {
        int i, len = 0;
        char *cmd;

        for (i = optind; i < argc; i++)
        {
            len += strlen(argv[i]) + 1;
        }

        cmd = malloc(len + (2 * argc)); // Allow for quotes
        strncpy(cmd, argv[optind], len + (2 * argc));
        for (i = optind + 1; i < argc; i++)
        {
            strcat(cmd, " ");
            /* Arguments after the second are quoted to allow for names
             * that contain white space
             */
            if (i - optind > 1)
            {
                strcat(cmd, "\"");
                strcat(cmd, argv[i]);
                strcat(cmd, "\"");
            }
            else
            {
                strcat(cmd, argv[i]);
            }
        }

        if (access(cmd, R_OK) == 0)
        {
            DoSource(so, cmd);
        }
        else
        {
            sendCommand(so, cmd);
        }

        free(cmd);
        exit(0);
    }

    (void) setlocale(LC_CTYPE, "");
#ifdef HISTORY
    hist = history_init(); /* Init the builtin history  */
    /* Remember 100 events      */
    history(hist, &ev, H_SETSIZE, 100);

    tok = tok_init(NULL); /* Initialize the tokenizer   */

    /* Initialize editline      */
    el = el_init(*argv, stdin, stdout, stderr);

    if (use_emacs)
    {
        el_set(el, EL_EDITOR, emacs); /** Editor is emacs */
    }
    else
    {
        el_set(el, EL_EDITOR, vi); /* Default editor is vi      */
    }
    el_set(el, EL_SIGNAL, 1); /* Handle signals gracefully  */
    el_set(el, EL_PROMPT, prompt); /* Set the prompt function */

    /* Tell editline to use this history interface  */
    el_set(el, EL_HIST, history, hist);

    /*
     * Bind j, k in vi command mode to previous and next line, instead
     * of previous and next history.
     */
    el_set(el, EL_BIND, "-a", "k", "ed-prev-line", NULL);
    el_set(el, EL_BIND, "-a", "j", "ed-next-line", NULL);

    /*
     * Source the user's defaults file.
     */
    el_source(el, NULL);

    while ((buf = (char *) el_gets(el, &num)) != NULL && num != 0)
    {
#else
    while (printf("MaxScale> ") && fgets(buf, 1024, stdin) != NULL)
    {
        num = strlen(buf);
#endif
        /* Strip trailing \n\r */
        for (i = num - 1; buf[i] == '\r' || buf[i] == '\n'; i--)
        {
            buf[i] = 0;
        }

#ifdef HISTORY
        el_line(el);
        history(hist, &ev, H_ENTER, buf);
#endif

        if (isquit(buf))
        {
            break;
        }
        else if (!strcasecmp(buf, "history"))
        {
#ifdef HISTORY
            for (rv = history(hist, &ev, H_LAST); rv != -1;
                 rv = history(hist, &ev, H_PREV))
            {
                fprintf(stdout, "%4d %s\n",
                        ev.num, ev.str);
            }
#else
            fprintf(stderr, "History not supported in this version.\n");
#endif
        }
        else if (!strncasecmp(buf, "source", 6))
        {
            char *ptr;

            /* Find the filename */
            ptr = &buf[strlen("source")];
            while (*ptr && isspace(*ptr))
            {
                ptr++;
            }

            DoSource(so, ptr);
        }
        else if (*buf)
        {
            if (!sendCommand(so, buf))
            {
                return 0;
            }
        }
    }

#ifdef HISTORY
    el_end(el);
    tok_end(tok);
    history_end(hist);
#endif
    close(so);
    return 0;
}

/**
 * Connect to the MaxScale server
 *
 * @param conn_socket The UNIX socket to connect to
 * @return       The connected socket or -1 on error
 */
static int
connectMaxScale(char *conn_socket)
{
    struct sockaddr_un local_addr;
    int so;
    int keepalive = 1;
    int optval = 1;

    if ((so = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
    {
        char errbuf[STRERROR_BUFLEN];
        fprintf(stderr, "Unable to create socket: %s\n",
                strerror_r(errno, errbuf, sizeof(errbuf)));
        return -1;
    }

    memset(&local_addr, 0, sizeof local_addr);
    local_addr.sun_family = AF_UNIX;
    strncpy(local_addr.sun_path, conn_socket, sizeof(local_addr.sun_path)-1);

    if (connect(so, (struct sockaddr *) &local_addr, sizeof(local_addr)) < 0)
    {
        char errbuf[STRERROR_BUFLEN];
        fprintf(stderr, "Unable to connect to MaxScale at %s: %s\n",
                conn_socket, strerror_r(errno, errbuf, sizeof(errbuf)));
        close(so);
        return -1;
    }

    if (setsockopt(so, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)))
    {
        perror("setsockopt");
    }

    /* Client is sending connection credentials (Pid, User, Group) */
    if (setsockopt(so, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) != 0)
    {
        fprintf(stderr, "SO_PASSCRED failed\n");
        return -1;
    }

    return so;
}

/**
 * Set IP address in socket structure in_addr
 *
 * @param a Pointer to a struct in_addr into which the address is written
 * @param p The hostname to lookup
 * @return  1 on success, 0 on failure
 */
static int
setipaddress(struct in_addr *a, char *p)
{
#ifdef __USE_POSIX
    struct addrinfo *ai = NULL, hint;
    int rc;
    struct sockaddr_in * res_addr;
    memset(&hint, 0, sizeof(hint));

    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags = AI_CANONNAME;
    hint.ai_family = AF_INET;

    if ((rc = getaddrinfo(p, NULL, &hint, &ai)) != 0)
    {
        return 0;
    }

    /* take the first one */
    if (ai != NULL)
    {
        res_addr = (struct sockaddr_in *) (ai->ai_addr);
        memcpy(a, &res_addr->sin_addr, sizeof(struct in_addr));

        freeaddrinfo(ai);

        return 1;
    }
#else
    struct hostent *h;

    spinlock_acquire(&tmplock);
    h = gethostbyname(p);
    spinlock_release(&tmplock);

    if (h == NULL)
    {
        if ((a->s_addr = inet_addr(p)) == -1)
        {
            return 0;
        }
    }
    else
    {
        /* take the first one */
        memcpy(a, h->h_addr, h->h_length);

        return 1;
    }
#endif
    return 0;
}

/**
 * Perform authentication using the maxscaled protocol conventions
 *
 * @param so    The socket connected to MaxScale
 * @return      Non-zero of succesful authentication
 */
static int
authMaxScale(int so)
{
    char buf[MAXADMIN_AUTH_REPLY_LEN];

    if (read(so, buf, MAXADMIN_AUTH_REPLY_LEN) != MAXADMIN_AUTH_REPLY_LEN)
    {
        return 0;
    }

    return strncmp(buf, MAXADMIN_FAILED_AUTH_MESSAGE, MAXADMIN_AUTH_REPLY_LEN);
}

/**
 * Send a command using the MaxScaled protocol, display the return data
 * on standard output.
 *
 * Input terminates with a line containing just the text OK
 *
 * @param so    The socket connect to MaxScale
 * @param cmd   The command to send
 * @return  0 if the connection was closed
 */
static int
sendCommand(int so, char *cmd)
{
    char buf[80];
    int i, j, newline = 1;

    if (write(so, cmd, strlen(cmd)) == -1)
    {
        return 0;
    }

    while (1)
    {
        if ((i = read(so, buf, 80)) <= 0)
        {
            return 0;
        }

        for (j = 0; j < i; j++)
        {
            if (newline == 1 && buf[j] == 'O')
            {
                newline = 2;
            }
            else if ((newline == 2 && buf[j] == 'K' && j == i - 1) ||
                     (j == i - 2 && buf[j] == 'O' && buf[j + 1] == 'K'))
            {
                return 1;
            }
            else if (newline == 2)
            {
                putchar('O');
                putchar(buf[j]);
                newline = 0;
            }
            else if (buf[j] == '\n' || buf[j] == '\r')
            {
                putchar(buf[j]);
                newline = 1;
            }
            else
            {
                putchar(buf[j]);
                newline = 0;
            }
        }
    }
    return 1;
}

/**
 * Read a file of commands and send them to MaxScale
 *
 * @param so        The socket connected to MaxScale
 * @param file      The filename
 */
static void
DoSource(int so, char *file)
{
    char *ptr, *pe;
    char line[132];
    FILE *fp;

    if ((fp = fopen(file, "r")) == NULL)
    {
        fprintf(stderr, "Unable to open command file '%s'.\n",
                file);
        return;
    }

    while ((ptr = fgets(line, 132, fp)) != NULL)
    {
        /* Strip tailing newlines */
        pe = &ptr[strlen(ptr) - 1];
        while (pe >= ptr && (*pe == '\r' || *pe == '\n'))
        {
            *pe = '\0';
            pe--;
        }

        if (*ptr != '#') /* Comment */
        {
            if (!sendCommand(so, ptr))
            {
                break;
            }
        }
    }
    fclose(fp);
    return;
}

/**
 * Print version information
 */
static void
PrintVersion(const char *progname)
{
    printf("%s Version %s\n", progname, MAXSCALE_VERSION);
}

/**
 * Display the --help text.
 */
static void
DoUsage(const char *progname)
{
    PrintVersion(progname);
    printf("The MaxScale administrative and monitor client.\n\n");
    printf("Usage: %s [-S socket] [<command file> | <command>]\n\n", progname);
    printf("  -S|--socket=...        The UNIX socket to connect to, The default is\n");
    printf("            /tmp/maxadmin.sock\n");
    printf("  -v|--version          print version information and exit\n");
    printf("  -?|--help     Print this help text.\n");
    printf("Any remaining arguments are treated as MaxScale commands or a file\n");
    printf("containing commands to execute.\n");
}

/**
 * Check command to see if it is a quit command
 *
 * @param buf   The command buffer
 * @return  Non-zero if the command should cause maxadmin to quit
 */
static int
isquit(char *buf)
{
    char *ptr = buf;

    if (!buf)
    {
        return 0;
    }

    while (*ptr && isspace(*ptr))
    {
        ptr++;
    }

    if (strncasecmp(ptr, "quit", 4) == 0 || strncasecmp(ptr, "exit", 4) == 0)
    {
        return 1;
    }

    return 0;
}

/**
 * Trim whitespace from the right hand end of the string
 *
 * @param str   String to trim
 */
static void
rtrim(char *str)
{
    char *ptr = str + strlen(str);

    if (ptr > str) // step back from the terminating null
    {
        ptr--; // If the string has more characters
    }

    while (ptr >= str && isspace(*ptr))
    {
        *ptr-- = 0;
    }
}

/**
 * Read defaults for hostname, port, user and password from
 * the .maxadmin file in the users home directory.
 *
 * @param hostname  Pointer the hostname to be updated
 * @param port      Pointer to the port to be updated
 * @param user      Pointer to the user to be updated
 * @param passwd    Pointer to the password to be updated
 */
static void
read_inifile(char **conn_socket, int* editor)
{
    char pathname[400];
    char *home, *brkt;
    char *name, *value;
    FILE *fp;
    char line[400];

    if ((home = getenv("HOME")) == NULL)
    {
        return;
    }

    snprintf(pathname, sizeof(pathname), "%s/.maxadmin", home);
    if ((fp = fopen(pathname, "r")) == NULL)
    {
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        rtrim(line);
        if (line[0] == 0 || line[0] == '#')
        {
            continue;
        }

        name = strtok_r(line, "=", &brkt);
        value = strtok_r(NULL, "=", &brkt);

        if (name && value)
        {
            if (strcmp(name, "socket") == 0)
            {
                *conn_socket = strdup(value);
            }
            else if (strcmp(name, "editor") == 0)
            {

                if (strcmp(value, "vi") == 0)
                {
                    *editor = 0;
                }
                else if (strcmp(value, "emacs") == 0)
                {
                    *editor = 1;
                }
                else
                {
                    fprintf(stderr, "WARNING: Unrecognised "
                            "parameter '%s=%s' in .maxadmin file\n", name, value);
                }
            }
            else
            {
                fprintf(stderr, "WARNING: Unrecognised "
                        "parameter '%s' in .maxadmin file\n", name);
            }
        }
        else
        {
            fprintf(stderr, "WARNING: Expected name=value "
                    "parameters in .maxadmin file but found "
                    "'%s'.\n", line);
        }
    }
    fclose(fp);
}
