#pragma once

#include <cstdint>
#include <cstddef>

namespace tcp
{

// A raw TCP connection
class Connection
{
public:
    ~Connection();

    /**
     * Connect to the target server
     *
     * @param host Server hostname
     * @param port Server port
     *
     * @return True if connection was successfully created
     */
    bool connect(const char* host, uint16_t port);

    /**
     * Write to socket
     *
     * @param buf  Buffer to read from
     * @param size Number of bytes to read from @c buf
     *
     * @return Number of written bytes or -1 on error
     */
    int write(void* buf, size_t size);

    /**
     * Read from socket
     *
     * @param buf  Buffer to write to
     * @param size Size of @c buf
     *
     * @return Number of read bytes or -1 on error
     */
    int read(void* buf, size_t size);

private:
    int m_so = -1;
};
}
