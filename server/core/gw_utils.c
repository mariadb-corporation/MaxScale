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
 * @file gw_utils.c - A set if utility functions useful within the context
 * of the gateway.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 03-06-2013   Massimiliano Pinto      gateway utils
 * 12-06-2013   Massimiliano Pinto      gw_read_gwbuff
 *                                      with error detection
 *                                      and its handling
 * 01-07-2013   Massimiliano Pinto      Removed session->backends
 *                                      from gw_read_gwbuff()
 * 25-09-2013   Massimiliano Pinto      setipaddress uses getaddrinfo
 * 06-02-2014   Mark Riddoch            Added parse_bindconfig
 * 10-02-2014   Massimiliano Pinto      Added return code to setipaddress
 * 02-09-2014   Martin Brampton         Replace C++ comment with C comment
 * 02-03-2016   Martin Brampton         Remove default from parse_bindconfig
 *
 *@endverbatim
 */

#include <gw.h>
#include <dcb.h>
#include <session.h>

#include <skygw_utils.h>
#include <log_manager.h>

SPINLOCK tmplock = SPINLOCK_INIT;

/*
 * Set IP address in socket structure in_addr
 *
 * @param a     Pointer to a struct in_addr into which the address is written
 * @param p     The hostname to lookup
 * @return      1 on success, 0 on failure
 */
int
setipaddress(struct in_addr *a, char *p)
{
#ifdef __USE_POSIX
    struct addrinfo *ai = NULL, hint;
    int rc;
    struct sockaddr_in *res_addr;
    memset(&hint, 0, sizeof (hint));

    hint.ai_socktype = SOCK_STREAM;

    /*
     * This is for the listening socket, matching INADDR_ANY only for now.
     * For future specific addresses bind, a dedicated routine woulbd be better
     */

    if (strcmp(p, "0.0.0.0") == 0)
    {
        hint.ai_flags = AI_PASSIVE;
        hint.ai_family = AF_UNSPEC;
        if ((rc = getaddrinfo(p, NULL, &hint, &ai)) != 0)
        {
            MXS_ERROR("Failed to obtain address for host %s, %s",
                      p,
                      gai_strerror(rc));

            return 0;
        }
    }
    else
    {
        hint.ai_flags = AI_CANONNAME;
        hint.ai_family = AF_INET;

        if ((rc = getaddrinfo(p, NULL, &hint, &ai)) != 0)
        {
            MXS_ERROR("Failed to obtain address for host %s, %s",
                      p,
                      gai_strerror(rc));

            return 0;
        }
    }

    /* take the first one */
    if (ai != NULL)
    {
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

    if (h == NULL)
    {
        if ((a->s_addr = inet_addr(p)) == -1)
        {
            MXS_ERROR("gethostbyname failed for [%s]", p);

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
 * Daemonize the process by forking and putting the process into the
 * background.
 */
bool gw_daemonize(void)
{
    pid_t pid;

    pid = fork();

    if (pid < 0)
    {
        char errbuf[STRERROR_BUFLEN];
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
        char errbuf[STRERROR_BUFLEN];
        fprintf(stderr, "setsid() error %s\n", strerror_r(errno, errbuf, sizeof(errbuf)));
        exit(1);
    }
    return false;
}

/**
 * Parse the bind config data. This is passed in a string as address:port.
 *
 * The address may be either a . separated IP address or a hostname to
 * lookup. The address 0.0.0.0 is the wildcard address for SOCKADR_ANY.
 * The ':' and port are required.
 *
 * @param config        The bind address and port separated by a ':'
 * @param addr          The sockaddr_in in which the data is written
 * @return              0 on failure
 */
int
parse_bindconfig(const char *config, struct sockaddr_in *addr)
{
    char buf[strlen(config) + 1];
    strcpy(buf, config);

    char *port = strrchr(buf, ':');
    short pnum;
    if (port)
    {
        *port = 0;
        port++;
        pnum = atoi(port);
    }
    else
    {
        return 0;
    }

    if (!strcmp(buf, "0.0.0.0"))
    {
        addr->sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else
    {
        if (!inet_aton(buf, &addr->sin_addr))
        {
            struct hostent *hp = gethostbyname(buf);

            if (hp)
            {
                bcopy(hp->h_addr, &(addr->sin_addr.s_addr), hp->h_length);
            }
            else
            {
                MXS_ERROR("Failed to lookup host '%s'.", buf);
                return 0;
            }
        }
    }

    addr->sin_family = AF_INET;
    addr->sin_port = htons(pnum);
    return 1;
}

/**
 * Return the number of processors available.
 * @return Number of processors or 1 if the required definition of _SC_NPROCESSORS_CONF
 * is not found
 */
long get_processor_count()
{
    long processors = 1;
#ifdef _SC_NPROCESSORS_ONLN
    if ((processors = sysconf(_SC_NPROCESSORS_ONLN)) <= 0)
    {
        MXS_WARNING("Unable to establish the number of available cores. Defaulting to 1.");
        processors = 1;
    }
#else
#error _SC_NPROCESSORS_ONLN not available.
#endif
    return processors;
}
