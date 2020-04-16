#pragma once

#include <sys/epoll.h>

struct MXB_POLL_DATA;

typedef struct MXB_WORKER
{
} MXB_WORKER;

typedef uint32_t (* mxb_poll_handler_t)(struct MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events);

typedef struct MXB_POLL_DATA
{
    mxb_poll_handler_t handler; /*< Handler for this particular kind of mxb_poll_data. */
    MXB_WORKER*        owner;   /*< Owning worker. */
} MXB_POLL_DATA;

class Worker : public MXB_WORKER
{
public:
    Worker();
    void add_fd(int fd, uint32_t events, MXB_POLL_DATA* pData);
    void run();
private:
    const int m_epoll_fd;               /*< The epoll file descriptor. */
    uint32_t  m_max_events = 42;
};
