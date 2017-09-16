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

#include <assert.h>
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
#include <stdbool.h>
#include <pwd.h>

#include <maxscale/version.h>

#ifdef HISTORY
#include <histedit.h>
#endif

#include <maxscale/maxadmin.h>
/*
 * We need a common.h file that is included by every component.
 */
#if !defined(STRERROR_BUFLEN)
#define STRERROR_BUFLEN 512
#endif

#define MAX_PASSWORD_LEN 80

static int connectUsingUnixSocket(const char *socket);
static int connectUsingInetSocket(const char *hostname, const char *port,
                                  const char *user, const char* password);
static int setipaddress(struct in_addr *a, const char *p);
static bool authUnixSocket(int so);
static bool authInetSocket(int so, const char *user, const char *password);
static int sendCommand(int so, char *cmd);
static void DoSource(int so, char *cmd);
static void DoUsage(const char*);
static int isquit(char *buf);
static void PrintVersion(const char *progname);
static void read_inifile(char **socket,
                         char **hostname, char **port, char **user, char **passwd,
                         int *editor);
static bool getPassword(char *password, size_t length);

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
    {"host", required_argument, 0, 'h'},
    {"user", required_argument, 0, 'u'},
    {"password", optional_argument, 0, 'p'},
    {"port", required_argument, 0, 'P'},
    {"socket", required_argument, 0, 'S'},
    {"version", no_argument, 0, 'v'},
    {"help", no_argument, 0, '?'},
    {"emacs", no_argument, 0, 'e'},
    {"vim", no_argument, 0, 'i'},
    {0, 0, 0, 0}
};

#define MAXADMIN_DEFAULT_HOST "localhost"
#define MAXADMIN_DEFAULT_PORT "6603"
#define MAXADMIN_DEFAULT_USER "admin"
#define MAXADMIN_BUFFER_SIZE 2048

/**
 * The main for the maxadmin client
 *
 * @param argc  Number of arguments
 * @param argv  The command line arguments
 */
int
main(int argc, char **argv)
{
#ifdef HISTORY
    char *buf;
    EditLine *el = NULL;
    Tokenizer *tok;
    History *hist;
    HistEvent ev;
#else
    char buf[MAXADMIN_BUFFER_SIZE];
#endif
    char *hostname = NULL;
    char *port = NULL;
    char *user = NULL;
    char *passwd = NULL;
    char *socket_path = NULL;
    int use_emacs = 1;

    read_inifile(&socket_path, &hostname, &port, &user, &passwd, &use_emacs);

    bool use_inet_socket = false;
    bool use_unix_socket = false;

    int option_index = 0;
    char c;
    while ((c = getopt_long(argc, argv, "h:p::P:u:S:v?ei",
                            long_options, &option_index)) >= 0)
    {
        switch (c)
        {
        case 'h':
            use_inet_socket = true;
            hostname = strdup(optarg);
            break;

        case 'p':
            use_inet_socket = true;
            // If password was not given, ask for it later
            if (optarg != NULL)
            {
                passwd = strdup(optarg);
                memset(optarg, '\0', strlen(optarg));
            }
            break;

        case 'P':
            use_inet_socket = true;
            port = strdup(optarg);
            break;

        case 'u':
            use_inet_socket = true;
            user = strdup(optarg);
            break;

        case 'S':
            use_unix_socket = true;
            socket_path = strdup(optarg);
            break;

        case 'v':
            PrintVersion(*argv);
            exit(EXIT_SUCCESS);

        case 'e':
            use_emacs = 1;
            break;

        case 'i':
            use_emacs = 0;
            break;

        case '?':
            DoUsage(argv[0]);
            exit(optopt ? EXIT_FAILURE : EXIT_SUCCESS);
        }
    }

    if (use_inet_socket && use_unix_socket)
    {
        // Both unix socket path and at least of the internet socket
        // options have been provided.
        printf("\nError: Both socket and network options are provided\n\n");
        DoUsage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if (use_inet_socket || (!socket_path && (hostname || port || user || passwd)))
    {
        // If any of the internet socket options have explicitly been provided, or
        // .maxadmin does not contain "socket" but does contain at least one of
        // the internet socket options, we use an internet socket. Note that if
        // -S is provided, then socket_path will be non-NULL.

        if (!hostname)
        {
            hostname = MAXADMIN_DEFAULT_HOST;
        }

        if (!port)
        {
            port = MAXADMIN_DEFAULT_PORT;
        }

        if (!user)
        {
            user = MAXADMIN_DEFAULT_USER;
        }
    }
    else
    {
        use_unix_socket = true;

        if (!socket_path)
        {
            socket_path = MAXADMIN_DEFAULT_SOCKET;
        }
    }

    int so;

    if (use_unix_socket)
    {
        assert(socket_path);

        if ((so = connectUsingUnixSocket(socket_path)) == -1)
        {
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        assert(hostname && user && port);

        char password[MAX_PASSWORD_LEN];

        if (passwd == NULL)
        {
            if (!getPassword(password, MAX_PASSWORD_LEN))
            {
                exit(EXIT_FAILURE);
            }

            passwd = password;
        }

        if ((so = connectUsingInetSocket(hostname, port, user, passwd)) == -1)
        {
            if (access(MAXADMIN_DEFAULT_SOCKET, R_OK) == 0)
            {
                fprintf(stderr, "Found default MaxAdmin socket in: %s\n", MAXADMIN_DEFAULT_SOCKET);
                fprintf(stderr, "Try connecting with:\n\n\tmaxadmin -S %s\n\n", MAXADMIN_DEFAULT_SOCKET);
            }
            exit(EXIT_FAILURE);
        }
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
        el_set(el, EL_EDITOR, "emacs"); /** Editor is emacs */
    }
    else
    {
        el_set(el, EL_EDITOR, "vi"); /* Default editor is vi      */
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

    int num;
    while ((buf = (char *) el_gets(el, &num)) != NULL && num != 0)
    {
#else
    while (printf("MaxScale> ") && fgets(buf, 1024, stdin) != NULL)
    {
        int num = strlen(buf);
#endif
        /* Strip trailing \n\r */
        for (int i = num - 1; buf[i] == '\r' || buf[i] == '\n'; i--)
        {
            buf[i] = 0;
        }

#ifdef HISTORY
        history(hist, &ev, H_ENTER, buf);
#endif

        if (isquit(buf))
        {
            break;
        }
        else if (!strcasecmp(buf, "history"))
        {
#ifdef HISTORY
            int rv;
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
 * @param socket_path The UNIX socket to connect to
 * @return       The connected socket or -1 on error
 */
static int
connectUsingUnixSocket(const char *socket_path)
{
    int so = -1;

    if ((so = socket(AF_UNIX, SOCK_STREAM, 0)) != -1)
    {
        struct sockaddr_un local_addr;

        memset(&local_addr, 0, sizeof local_addr);
        local_addr.sun_family = AF_UNIX;
        strncpy(local_addr.sun_path, socket_path, sizeof(local_addr.sun_path) - 1);

        if (connect(so, (struct sockaddr *) &local_addr, sizeof(local_addr)) == 0)
        {
            int keepalive = 1;
            if (setsockopt(so, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)))
            {
                fprintf(stderr, "Warning: Could not set keepalive.\n");
            }

            /* Client is sending connection credentials (Pid, User, Group) */
            int optval = 1;
            if (setsockopt(so, SOL_SOCKET, SO_PASSCRED, &optval, sizeof(optval)) == 0)
            {
                if (!authUnixSocket(so))
                {
                    close(so);
                    so = -1;
                }
            }
            else
            {
                char errbuf[STRERROR_BUFLEN];
                fprintf(stderr, "Could not set SO_PASSCRED: %s\n",
                        strerror_r(errno, errbuf, sizeof(errbuf)));
                close(so);
                so = -1;
            }
        }
        else
        {
            char errbuf[STRERROR_BUFLEN];
            fprintf(stderr, "Unable to connect to MaxScale at %s: %s\n",
                    socket_path, strerror_r(errno, errbuf, sizeof(errbuf)));
            close(so);
            so = -1;
        }
    }
    else
    {
        char errbuf[STRERROR_BUFLEN];
        fprintf(stderr, "Unable to create socket: %s\n",
                strerror_r(errno, errbuf, sizeof(errbuf)));
    }

    return so;
}

/**
 * Connect to the MaxScale server
 *
 * @param hostname  The hostname to connect to
 * @param port      The port to use for the connection
 * @return      The connected socket or -1 on error
 */
static int
connectUsingInetSocket(const char *hostname, const char *port,
                       const char *user, const char *passwd)
{
    int so;

    if ((so = socket(AF_INET, SOCK_STREAM, 0)) != -1)
    {
        struct sockaddr_in addr;

        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        setipaddress(&addr.sin_addr, hostname);
        addr.sin_port = htons(atoi(port));

        if (connect(so, (struct sockaddr *) &addr, sizeof(addr)) == 0)
        {
            int keepalive = 1;
            if (setsockopt(so, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)))
            {
                fprintf(stderr, "Warning: Could not set keepalive.\n");
            }

            if (!authInetSocket(so, user, passwd))
            {
                close(so);
                so = -1;
            }
        }
        else
        {
            char errbuf[STRERROR_BUFLEN];
            fprintf(stderr, "Unable to connect to MaxScale at %s, %s: %s\n",
                    hostname, port, strerror_r(errno, errbuf, sizeof(errbuf)));
            close(so);
            so = -1;
        }
    }
    else
    {
        char errbuf[STRERROR_BUFLEN];
        fprintf(stderr, "Unable to create socket: %s\n",
                strerror_r(errno, errbuf, sizeof(errbuf)));
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
setipaddress(struct in_addr *a, const char *p)
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
static bool
authUnixSocket(int so)
{
    char buf[MAXADMIN_AUTH_REPLY_LEN];

    if (read(so, buf, MAXADMIN_AUTH_REPLY_LEN) != MAXADMIN_AUTH_REPLY_LEN)
    {
        fprintf(stderr, "Could not read authentication response from MaxScale.\n");
        return 0;
    }

    bool authenticated = (strncmp(buf, MAXADMIN_AUTH_SUCCESS_REPLY, MAXADMIN_AUTH_REPLY_LEN) == 0);

    if (!authenticated)
    {
        uid_t id = geteuid();
        struct passwd* pw = getpwuid(id);
        fprintf(stderr, "Could connect to MaxScale, but was not authorized.\n"
                "Check that the current user is added to the list of allowed users.\n"
                "To add this user to the list, execute:\n\n"
                "\tsudo maxadmin enable account %s\n\n"
                "This assumes that the root user account is enabled in MaxScale.\n", pw->pw_name);
    }

    return authenticated;
}

/**
 * Perform authentication using the maxscaled protocol conventions
 *
 * @param so        The socket connected to MaxScale
 * @param user      The username to authenticate
 * @param password  The password to authenticate with
 * @return          Non-zero of succesful authentication
 */
static bool
authInetSocket(int so, const char *user, const char *password)
{
    char buf[20];
    size_t len;

    len = MAXADMIN_AUTH_USER_PROMPT_LEN;
    if (read(so, buf, len) != len)
    {
        fprintf(stderr, "Could not read user prompt from MaxScale.\n");
        return false;
    }

    len = strlen(user);
    if (write(so, user, len) != len)
    {
        fprintf(stderr, "Could not write user to MaxScale.\n");
        return false;
    }

    len = MAXADMIN_AUTH_PASSWORD_PROMPT_LEN;
    if (read(so, buf, len) != len)
    {
        fprintf(stderr, "Could not read password prompt from MaxScale.\n");
        return false;
    }

    len = strlen(password);
    if (write(so, password, len) != len)
    {
        fprintf(stderr, "Could not write password to MaxScale.\n");
        return false;
    }

    len = MAXADMIN_AUTH_REPLY_LEN;
    if (read(so, buf, len) != len)
    {
        fprintf(stderr, "Could not read authentication response from MaxScale.\n");
        return false;
    }

    bool authenticated = (strncmp(buf, MAXADMIN_AUTH_SUCCESS_REPLY, MAXADMIN_AUTH_REPLY_LEN) == 0);

    if (!authenticated)
    {
        fprintf(stderr, "Could connect to MaxScale, but was not authorized.\n");
    }

    return authenticated;
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

        if (*ptr != '#' && *ptr != '\0') /* Comment or empty */
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
    printf("Usage: %s [-S socket] <command>\n", progname);
    printf("       %s [-u user] [-p password] [-h hostname] [-P port] <command>\n\n", progname);
    printf("  -S|--socket=...   The UNIX domain socket to connect to, The default is\n");
    printf("                    %s\n", MAXADMIN_DEFAULT_SOCKET);
    printf("  -u|--user=...     The user name to use for the connection, default\n");
    printf("                    is %s.\n", MAXADMIN_DEFAULT_USER);
    printf("  -p|--password=... The user password, if not given the password will\n");
    printf("                    be prompted for interactively\n");
    printf("  -h|--host=...     The maxscale host to connecto to. The default is\n");
    printf("                    %s\n", MAXADMIN_DEFAULT_HOST);
    printf("  -P|--port=...     The port to use for the connection, the default\n");
    printf("                    port is %s.\n", MAXADMIN_DEFAULT_PORT);
    printf("  -v|--version      Print version information and exit\n");
    printf("  -?|--help         Print this help text.\n");
    printf("\n");
    printf("Any remaining arguments are treated as MaxScale commands or a file\n");
    printf("containing commands to execute.\n");
    printf("\n");
    printf("Either a socket or a hostname/port combination should be provided.\n");
    printf("If a port or hostname is provided, but not the other, then the default\n"
           "value is used.\n");
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
 * @param socket    Pointer to the socket to be updated.
 * @param hostname  Pointer to the hostname to be updated
 * @param port      Pointer to the port to be updated
 * @param user      Pointer to the user to be updated
 * @param passwd    Pointer to the password to be updated
 */
static void
read_inifile(char **socket,
             char **hostname, char** port, char **user, char **passwd,
             int* editor)
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
                *socket = strdup(value);
            }
            else if (strcmp(name, "hostname") == 0)
            {
                *hostname = strdup(value);
            }
            else if (strcmp(name, "port") == 0)
            {
                *port = strdup(value);
            }
            else if (strcmp(name, "user") == 0)
            {
                *user = strdup(value);
            }
            else if ((strcmp(name, "passwd") == 0) || (strcmp(name, "password") == 0))
            {
                *passwd = strdup(value);
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

/**
 * Get password
 *
 * @param password Buffer for password.
 * @param len The size of the buffer.
 *
 * @return Whether the password was obtained.
 */
bool getPassword(char *passwd, size_t len)
{
    bool gotten = false;

    struct termios tty_attr;
    tcflag_t c_lflag;

    if (tcgetattr(STDIN_FILENO, &tty_attr) == 0)
    {
        c_lflag = tty_attr.c_lflag;
        tty_attr.c_lflag &= ~ICANON;
        tty_attr.c_lflag &= ~ECHO;

        if (tcsetattr(STDIN_FILENO, 0, &tty_attr) == 0)
        {
            printf("Password: ");
            if (fgets(passwd, len, stdin) == NULL)
            {
                printf("Failed to read password\n");
            }

            tty_attr.c_lflag = c_lflag;

            if (tcsetattr(STDIN_FILENO, 0, &tty_attr) == 0)
            {
                int i = strlen(passwd);

                if (i > 1)
                {
                    passwd[i - 1] = '\0';
                }

                printf("\n");

                gotten = true;
            }
        }
    }

    if (!gotten)
    {
        fprintf(stderr, "Could not configure terminal.\n");
    }

    return gotten;
}
