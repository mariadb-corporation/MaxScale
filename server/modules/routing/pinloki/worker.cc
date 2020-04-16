#include "worker.hh"
#include <iostream>
#include <iomanip>
#include <unistd.h>

Worker::Worker()
    : m_epoll_fd(::epoll_create(1))
{
    if (m_epoll_fd <= 0)
    {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }
}

void Worker::add_fd(int fd, uint32_t events, MXB_POLL_DATA* pData)
{
    struct epoll_event ev;

    ev.events = events;
    ev.data.ptr = pData;

    pData->owner = this;

    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

void Worker::run()
{
    const int timeout = 1000000;

    struct epoll_event events[8 * 1024];
    for (;;)
    {
        auto nfds = epoll_wait(m_epoll_fd, events, m_max_events, timeout);
        const size_t SZ = 8 * 1024;
        char buf[SZ];

        ssize_t len = read(m_epoll_fd, buf, sizeof buf);

        for (int i = 0; i < nfds; ++i)
        {

            MXB_POLL_DATA* pData = (MXB_POLL_DATA*)events[i].data.ptr;
            pData->handler(pData, pData->owner, events[i].events);
        }
    }
}
