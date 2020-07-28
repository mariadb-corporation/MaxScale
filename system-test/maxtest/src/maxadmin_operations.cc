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
 * Copyright MariaDB Corporation Ab 2014
 */

#include "maxadmin_operations.h"

int connectMaxScale(char* hostname, char* port)
{
    struct sockaddr_in addr;
    int so;
    int keepalive = 1;

    if ((so = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        fprintf(stderr,
                "Unable to create socket: %s\n",
                strerror(errno));
        return -1;
    }
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    setipaddress(&addr.sin_addr, hostname);
    addr.sin_port = htons(atoi(port));
    if (connect(so, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        fprintf(stderr,
                "Unable to connect to MaxScale at %s, %s: %s\n",
                hostname,
                port,
                strerror(errno));
        close(so);
        return -1;
    }
    if (setsockopt(so,
                   SOL_SOCKET,
                   SO_KEEPALIVE,
                   &keepalive,
                   sizeof(keepalive )))
    {
        perror("setsockopt");
    }

    return so;
}


int setipaddress(struct in_addr* a, char* p)
{
#ifdef __USE_POSIX
    struct addrinfo* ai = NULL, hint;
    int rc;
    struct sockaddr_in* res_addr;
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
        res_addr = (struct sockaddr_in*)(ai->ai_addr);
        memcpy(a, &res_addr->sin_addr, sizeof(struct in_addr));

        freeaddrinfo(ai);

        return 1;
    }
#else
    struct hostent* h;

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

int authMaxScale(int so, char* user, char* password)
{
    char buf[20];

    if (read(so, buf, 4) != 4)
    {
        return 0;
    }
    write(so, user, strlen(user));
    if (read(so, buf, 8) != 8)
    {
        return 0;
    }
    write(so, password, strlen(password));
    if (read(so, buf, 6) != 6)
    {
        return 0;
    }

    return strncmp(buf, "FAILED", 6);
}

int sendCommand(int so, char* cmd, char* buf)
{
    char buf1[80];
    int i, j, newline = 1;
    int k = 0;

    if (write(so, cmd, strlen(cmd)) == -1)
    {
        return 0;
    }
    while (1)
    {
        if ((i = read(so, buf1, 80)) <= 0)
        {
            return 0;
        }
        for (j = 0; j < i; j++)
        {
            if (newline == 1 && buf1[j] == 'O')
            {
                newline = 2;
            }
            else if (newline == 2 && buf1[j] == 'K' && j == i - 1)
            {
                return 1;
            }
            else if (newline == 2)
            {
                buf[k] = 'O';
                k++;
                buf[k] = buf1[j];
                k++;
                newline = 0;
            }
            else if (buf1[j] == '\n' || buf1[j] == '\r')
            {
                buf[k] = buf1[j];
                k++;
                newline = 1;
            }
            else
            {
                buf[k] = buf1[j];
                k++;
                newline = 0;
            }
        }
    }
    return 1;
}

int get_maxadmin_param_tcp(char* hostname, char* user, char* password, char* cmd, char* param, char* result)
{

    char buf[10240];
    char* port = (char*) "6603";
    int so;

    if ((so = connectMaxScale(hostname, port)) == -1)
    {
        return 1;
    }
    if (!authMaxScale(so, user, password))
    {
        fprintf(stderr,
                "Failed to connect to MaxScale. "
                "Incorrect username or password.\n");
        close(so);
        return 1;
    }

    sendCommand(so, cmd, buf);

    // printf("%s\n", buf);

    char* x = strstr(buf, param);
    if (x == NULL)
    {
        return 1;
    }
    // char f_field[100];
    int param_len = strlen(param);
    int cnt = 0;
    while (x[cnt + param_len] != '\n')
    {
        result[cnt] = x[cnt + param_len];
        cnt++;
    }
    result[cnt] = '\0';
    // sprintf(f_field, "%s %%s", param);
    // sscanf(x, f_field, result);
    close(so);
    return 0;
}

int execute_maxadmin_command_tcp(char* hostname, char* user, char* password, char* cmd)
{

    char buf[10240];
    char* port = (char*) "6603";
    int so;

    if ((so = connectMaxScale(hostname, port)) == -1)
    {
        return 1;
    }
    if (!authMaxScale(so, user, password))
    {
        fprintf(stderr,
                "Failed to connect to MaxScale. "
                "Incorrect username or password.\n");
        close(so);
        return 1;
    }

    sendCommand(so, cmd, buf);

    close(so);
    return 0;
}

int execute_maxadmin_command_print_tcp(char* hostname, char* user, char* password, char* cmd)
{

    char buf[10240];
    char* port = (char*) "6603";
    int so;

    if ((so = connectMaxScale(hostname, port)) == -1)
    {
        return 1;
    }
    if (!authMaxScale(so, user, password))
    {
        fprintf(stderr,
                "Failed to connect to MaxScale. "
                "Incorrect username or password.\n");
        close(so);
        return 1;
    }

    sendCommand(so, cmd, buf);
    printf("%s\n", buf);
    close(so);
    return 0;
}
