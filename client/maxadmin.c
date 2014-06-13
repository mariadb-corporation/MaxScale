/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright SkySQL Ab 2014
 */

/**
 * @file maxadmin.c  - The MaxScale administration client
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 13/06/14	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
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

#include <histedit.h>

static int connectMaxScale(char *hostname, char *port);
static int setipaddress(struct in_addr *a, char *p);
static int authMaxScale(int so, char *user, char *password);
static int sendCommand(int so, char *cmd);

static char *
prompt(EditLine *el __attribute__((__unused__)))
{
	static char prompt[] = "MaxScale> ";

	return prompt;
}

int
main(int argc, char **argv)
{
EditLine	*el = NULL;
int		i, num, rv, fatal = 0;
char		*buf;
Tokenizer	*tok;
History		*hist;
HistEvent	ev;
const LineInfo	*li;
char		*hostname = "localhost";
char		*port = "6603";
char		*user = "admin";
char		*passwd = NULL;
int		so, cmdlen;
char		*cmd;

	cmd = malloc(1);
	*cmd = 0;
	cmdlen = 1;

	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-')
		{
			switch (argv[i][1])
			{
			case 'u':	/* User */
				if (argv[i][2])
					user = &(argv[i][2]);
				else if (i + 1 < argc)
					user = argv[++i];
				else
				{
					fprintf(stderr, "Missing username"
						"in -u option.\n");
					fatal = 1;
				}
				break;
			case 'p':	/* Password */
				if (argv[i][2])
					passwd = &(argv[i][2]);
				else if (i + 1 < argc)
					passwd = argv[++i];
				else
				{
					fprintf(stderr, "Missing password "
						"in -p option.\n");
					fatal = 1;
				}
				break;
			case 'h':	/* hostname */
				if (argv[i][2])
					hostname = &(argv[i][2]);
				else if (i + 1 < argc)
					hostname = argv[++i];
				else
				{
					fprintf(stderr, "Missing hostname value "
						"in -h option.\n");
					fatal = 1;
				}
				break;
			case 'P':	/* Port */
				if (argv[i][2])
					port = &(argv[i][2]);
				else if (i + 1 < argc)
					port = argv[++i];
				else
				{
					fprintf(stderr, "Missing Port value "
						"in -P option.\n");
					fatal = 1;
				}
				break;
			}
		}
		else
		{
			cmdlen += strlen(argv[i]) + 1;
			cmd = realloc(cmd, cmdlen);
			strcat(cmd, argv[i]);
			strcat(cmd, " ");
		}
	}

	if (fatal)
		exit(1);

	if (passwd == NULL)
	{
		struct termios tty_attr;
		tcflag_t c_lflag;
	
		if (tcgetattr(STDIN_FILENO, &tty_attr) < 0)
			return -1;

		c_lflag = tty_attr.c_lflag;
		tty_attr.c_lflag &= ~ICANON;
		tty_attr.c_lflag &= ~ECHO;

		if (tcsetattr(STDIN_FILENO, 0, &tty_attr) < 0)
			return -1;

		printf("Password: ");
		passwd = malloc(80);
		fgets(passwd, 80, stdin);

		tty_attr.c_lflag = c_lflag;
		if (tcsetattr(STDIN_FILENO, 0, &tty_attr) < 0)
			return -1;
		i = strlen(passwd);
		if (i > 1)
			passwd[i - 1] = '\0';
		printf("\n");
	}

	if ((so = connectMaxScale(hostname, port)) == -1)
		exit(1);
	if (!authMaxScale(so, user, passwd))
	{
		fprintf(stderr, "Failed to connect to MaxScale. "
				"Incorrect username or password.\n");
		exit(1);
	}

	if (cmdlen > 1)
	{
		cmd[cmdlen - 2] = '\0';
		sendCommand(so, cmd);
		exit(0);
	}

	(void) setlocale(LC_CTYPE, "");

	hist = history_init();		/* Init the builtin history	*/
					/* Remember 100 events		*/
	history(hist, &ev, H_SETSIZE, 100);

	tok  = tok_init(NULL);		/* Initialize the tokenizer	*/

					/* Initialize editline		*/
	el = el_init(*argv, stdin, stdout, stderr);

	el_set(el, EL_EDITOR, "vi");	/* Default editor is vi		*/
	el_set(el, EL_SIGNAL, 1);	/* Handle signals gracefully	*/
	el_set(el, EL_PROMPT, prompt);/* Set the prompt function */

			/* Tell editline to use this history interface	*/
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

	while ((buf = el_gets(el, &num)) != NULL && num != 0)
	{
		/* Strip trailing \n\r */
		for (i = num - 1; buf[i] == '\r' || buf[i] == '\n'; i--)
			buf[i] = 0;

		li = el_line(el);
		history(hist, &ev, H_ENTER, buf);

		if (!strcasecmp(buf, "quit"))
		{
			break;
		}
		else if (!strcasecmp(buf, "history"))
		{
			for (rv = history(hist, &ev, H_LAST); rv != -1;
					rv = history(hist, &ev, H_PREV))
				fprintf(stdout, "%4d %s\n",
					    ev.num, ev.str);
		}
		else if (*buf)
		{
			sendCommand(so, buf);
		}
	}

	el_end(el);
	tok_end(tok);
	history_end(hist);
	close(so);
	return 0;
}

static int
connectMaxScale(char *hostname, char *port)
{
struct sockaddr_in	addr;
int			so;

	if ((so = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "Unable to create socket: %s\n",
				strerror(errno));
		return -1;
	}
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	setipaddress(&addr.sin_addr, hostname);
	addr.sin_port = htons(atoi(port));
	if (connect(so, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		fprintf(stderr, "Unable to connect to MaxScale at %s, %s: %s\n",
				hostname, port, strerror(errno));
		return -1;
	}

	return so;
}


/*
 * Set IP address in socket structure in_addr
 *
 * @param a	Pointer to a struct in_addr into which the address is written
 * @param p	The hostname to lookup
 * @return	1 on success, 0 on failure
 */
static int
setipaddress(struct in_addr *a, char *p)
{
#ifdef __USE_POSIX
	struct addrinfo *ai = NULL, hint;
	int    rc;
	struct sockaddr_in * res_addr;
	memset(&hint, 0, sizeof (hint));

	hint.ai_socktype = SOCK_STREAM;
	hint.ai_flags = AI_CANONNAME;
	hint.ai_family = AF_INET;

	if ((rc = getaddrinfo(p, NULL, &hint, &ai)) != 0) {
		return 0;
	}

        /* take the first one */
	if (ai != NULL) {
		res_addr = (struct sockaddr_in *)(ai->ai_addr);
		memcpy(a, &res_addr->sin_addr, sizeof(struct in_addr));

		freeaddrinfo(ai);

		return 1;
	}
#else
	struct hostent *h;

        spinlock_acquire(&tmplock);
        h = gethostbyname(p);
        spinlock_release(&tmplock);
        
	if (h == NULL) {
		if ((a->s_addr = inet_addr(p)) == -1) {
			return 0;
		}
	} else {
        	/* take the first one */
		memcpy(a, h->h_addr, h->h_length);

		return 1;
	}
#endif
	return 0;
}

static int
authMaxScale(int so, char *user, char *password)
{
char	buf[20];

	read(so, buf, 4);
	write(so, user, strlen(user));
	read(so, buf, 8);
	write(so, password, strlen(password));
	read(so, buf, 6);

	return strncmp(buf, "FAILED", 6);
}

static int
sendCommand(int so, char *cmd)
{
char	buf[80];
int	i;

	write(so, cmd, strlen(cmd));
	while (1)
	{
		if ((i = read(so, buf, 80)) == -1)
			return 0;
		if (i > 1 && buf[i-1] == 'K' && buf[i-2] == 'O')
		{
			write(1, buf, i - 2);
			return 1;
		}
		write(1, buf, i);
	}
	return 1;
}
