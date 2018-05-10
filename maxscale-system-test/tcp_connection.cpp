#include "tcp_connection.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>


namespace
{

static void set_port(struct sockaddr_storage *addr, uint16_t port)
{
    if (addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ip = (struct sockaddr_in*)addr;
        ip->sin_port = htons(port);
    }
    else if (addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ip = (struct sockaddr_in6*)addr;
        ip->sin6_port = htons(port);
    }
}

int open_network_socket(struct sockaddr_storage *addr, const char *host, uint16_t port)
{
    struct addrinfo *ai = NULL, hint = {};
    int so = -1, rc = 0;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_family = AF_UNSPEC;
    hint.ai_flags = AI_ALL;

    /* Take the first one */
    if (getaddrinfo(host, NULL, &hint, &ai) == 0 && ai)
    {
        if ((so = socket(ai->ai_family, SOCK_STREAM, 0)) != -1)
        {
            memcpy(addr, ai->ai_addr, ai->ai_addrlen);
            set_port(addr, port);
            freeaddrinfo(ai);
        }
    }

    return so;
}

}

namespace tcp
{

Connection::~Connection()
{
    if (m_so != -1)
    {
        close(m_so);
    }
}

bool Connection::connect(const char* host, uint16_t port)
{
    struct sockaddr_storage addr;

    if ((m_so = open_network_socket(&addr, host, port)) != -1)
    {
        if (::connect(m_so, (struct sockaddr*)&addr, sizeof(addr)) != 0)
        {
            close(m_so);
            m_so = -1;
        }
    }

    return m_so != -1;
}

int Connection::write(void* buf, size_t size)
{
    return ::write(m_so, buf, size);
}

int Connection::read(void* buf, size_t size)
{
    return ::read(m_so, buf, size);
}

}
