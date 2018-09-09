/*
 * Find local ip used as source ip in ip packets.
 * Use getsockname and a udp connection
 */

#include <stdio.h>      // printf
#include <string.h>     // memset
#include <errno.h>      // errno
#include <sys/socket.h> // socket
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // getsockname
#include <unistd.h>     // close

#include "get_my_ip.h"

int get_my_ip(char* remote_ip, char* my_ip)
{

    int dns_port = 53;

    struct sockaddr_in serv;

    int sock = socket (AF_INET, SOCK_DGRAM, 0);

    // Socket could not be created
    if (sock < 0)
    {
        return 1;
    }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr(remote_ip);
    serv.sin_port = htons(dns_port);

    connect(sock, (const struct sockaddr*) &serv, sizeof(serv));

    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    getsockname(sock, (struct sockaddr*) &name, &namelen);

    char buffer[100];
    const char* p = inet_ntop(AF_INET, &name.sin_addr, buffer, 100);

    if (p != NULL)
    {
        // printf("Local ip is : %s \n" , buffer);
        strcpy(my_ip, buffer);
        close(sock);
        return 0;
    }
    else
    {
        // Some error
        printf ("Error number : %d . Error message : %s \n", errno, strerror(errno));
        close(sock);
        return 2;
    }
}
