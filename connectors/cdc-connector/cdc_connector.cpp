/* Copyright (c) 2017, MariaDB Corporation. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301  USA
 */

#include "cdc_connector.h"

#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <iostream>
#include <jansson.h>
#include <netdb.h>
#include <openssl/sha.h>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>


#define CDC_CONNECTOR_VERSION "1.0.0"

#define ERRBUF_SIZE 512
#define READBUF_SIZE 32 * 1024

static const char OK_RESPONSE[] = "OK\n";

static const char CLOSE_MSG[] = "CLOSE";
static const char REGISTER_MSG[] = "REGISTER UUID=CDC_CONNECTOR-" CDC_CONNECTOR_VERSION ", TYPE=";
static const char REQUEST_MSG[] = "REQUEST-DATA ";

namespace
{

static std::string bin2hex(const uint8_t *data, size_t len)
{
    std::string result;
    static const char hexconvtab[] = "0123456789abcdef";

    for (size_t i = 0; i < len; i++)
    {
        result += hexconvtab[data[i] >> 4];
        result += hexconvtab[data[i] & 0x0f];
    }

    return result;
}

std::string generateAuthString(const std::string& user, const std::string& password)
{
    uint8_t digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const uint8_t*>(password.c_str()), password.length(), digest);

    std::string auth_str = user;
    auth_str += ":";

    std::string part1 = bin2hex((uint8_t*)auth_str.c_str(), auth_str.length());
    std::string part2 = bin2hex(digest, sizeof(digest));

    return part1 + part2;
}


std::string json_to_string(json_t* json)
{
    std::stringstream ss;

    switch (json_typeof(json))
    {
    case JSON_STRING:
        ss << json_string_value(json);
        break;

    case JSON_INTEGER:
        ss << json_integer_value(json);
        break;

    case JSON_REAL:
        ss << json_real_value(json);
        break;

    case JSON_TRUE:
        ss << "true";
        break;

    case JSON_FALSE:
        ss << "false";
        break;

    case JSON_NULL:
        break;

    default:
        break;

    }

    return ss.str();
}

// Helper class for closing objects
template <class T> class Closer
{
public:

    Closer(T t):
        m_t(t),
        m_close(false)
    {
    }

    ~Closer()
    {
        if (m_close)
        {
            close(m_t);
        }
    }

    /**
     * Release the stored value
     *
     * Releasing the value prevents it from being closed when the class is deleted
     *
     * @return A copy of the stored value
     */
    T release()
    {
        m_close = false;
        return m_t;
    }

private:
    T m_t;
    bool m_close;

    void close(T t);
};

template <> void Closer<struct addrinfo*>::close(struct addrinfo* ai)
{
    freeaddrinfo(ai);
}

template <> void Closer<int>::close(int fd)
{
    ::close(fd);
}

}

namespace CDC
{

/**
 * Public functions
 */

Connection::Connection(const std::string& address,
                       uint16_t port,
                       const std::string& user,
                       const std::string& password,
                       int timeout) :
    m_fd(-1),
    m_port(port),
    m_address(address),
    m_user(user),
    m_password(password),
    m_timeout(timeout),
    m_connected(false)
{
    m_buf_ptr = m_buffer.begin();
}

Connection::~Connection()
{
    close();
}

bool Connection::connect(const std::string& table, const std::string& gtid)
{
    bool rval = false;

    try
    {
        if (m_connected)
        {
            m_error = "Already connected";
            return false;
        }

        m_error.clear();

        struct addrinfo *ai = NULL, hint = {};
        hint.ai_socktype = SOCK_STREAM;
        hint.ai_family = AF_UNSPEC;
        hint.ai_flags = AI_ALL;

        if (getaddrinfo(m_address.c_str(), NULL, &hint, &ai) != 0 || ai == NULL)
        {
            char err[ERRBUF_SIZE];
            m_error = "Invalid address (";
            m_error += m_address;
            m_error += "): ";
            m_error += strerror_r(errno, err, sizeof(err));
            return false;
        }

        Closer<struct addrinfo*> c_ai(ai);
        struct sockaddr_in remote = {};
        memcpy(&remote, ai->ai_addr, ai->ai_addrlen);
        remote.sin_port = htons(m_port);
        remote.sin_family = AF_INET;

        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if (fd == -1)
        {
            char err[ERRBUF_SIZE];
            m_error = "Failed to create socket: ";
            m_error += strerror_r(errno, err, sizeof(err));
            return false;
        }

        Closer<int> c_fd(fd);
        int fl;

        if (::connect(fd, (struct sockaddr*)&remote, sizeof(remote)) == -1)
        {
            char err[ERRBUF_SIZE];
            m_error = "Failed to connect: ";
            m_error += strerror_r(errno, err, sizeof(err));
        }
        else if ((fl = fcntl(fd, F_GETFL, 0)) == -1 ||
                 fcntl(fd, F_SETFL, fl | O_NONBLOCK) == -1)
        {
            char err[ERRBUF_SIZE];
            m_error = "Failed to set socket non-blocking: ";
            m_error += strerror_r(errno, err, sizeof(err));
        }
        else if (do_auth() && do_registration())
        {
            std::string req_msg(REQUEST_MSG);
            req_msg += table;

            if (gtid.length())
            {
                req_msg += " ";
                req_msg += gtid;
            }

            if (nointr_write(req_msg.c_str(), req_msg.length()) == -1)
            {
                char err[ERRBUF_SIZE];
                m_error = "Failed to write request: ";
                m_error += strerror_r(errno, err, sizeof(err));
            }
            else if ((m_first_row = read()))
            {
                rval = true;
                m_connected = true;
                m_fd = c_fd.release();
            }
        }
    }
    catch (const std::exception& ex)
    {
        m_error = "Exception caught: ";
        m_error += ex.what();
    }

    return rval;
}

void Connection::close()
{
    m_error.clear();

    if (m_fd != -1)
    {
        nointr_write(CLOSE_MSG, sizeof(CLOSE_MSG) - 1);
        ::close(m_fd);
        m_fd = -1;
    }
}

static inline bool is_schema(json_t* json)
{
    bool rval = false;
    json_t* j = json_object_get(json, "fields");

    if (j && json_is_array(j) && json_array_size(j))
    {
        rval = json_object_get(json_array_get(j, 0), "name") != NULL;
    }

    return rval;
}

void Connection::process_schema(json_t* json)
{
    SValueVector keys(new ValueVector);
    SValueVector types(new ValueVector);

    json_t* arr = json_object_get(json, "fields");
    size_t i;
    json_t* v;

    json_array_foreach(arr, i, v)
    {
        json_t* name = json_object_get(v, "name");
        json_t* type = json_object_get(v, "real_type");
        json_t* length = json_object_get(v, "length");
        if (type == NULL)
        {
            // Use the Avro type for generated columns
            type = json_object_get(v, "type");
        }
        std::string nameval = name ? json_string_value(name) : "";
        std::string typeval = type ? (json_is_string(type) ? json_string_value(type) : "varchar(50)") : "undefined";

        if (json_is_integer(length))
        {
            int l = json_integer_value(length);
            if (l > 0)
            {
                std::stringstream ss;
                ss << "(" << l << ")";
                typeval += ss.str();
            }
        }

        keys->push_back(nameval);
        types->push_back(typeval);
    }

    m_keys = keys;
    m_types = types;
}

SRow Connection::process_row(json_t* js)
{
    ValueVector values;
    values.reserve(m_keys->size());
    m_error.clear();

    for (ValueVector::iterator it = m_keys->begin();
         it != m_keys->end(); it++)
    {
        json_t* v = json_object_get(js, it->c_str());

        if (v)
        {
            values.push_back(json_to_string(v));
        }
        else
        {
            m_error = "No value for key found: ";
            m_error += *it;
            break;
        }
    }

    SRow rval;

    if (m_error.empty())
    {
        rval = SRow(new Row(m_keys, m_types, values));
    }

    return rval;
}

SRow Connection::read()
{
    m_error.clear();
    SRow rval;
    std::string row;

    if (m_first_row)
    {
        rval.swap(m_first_row);
        assert(!m_first_row);
    }
    else if (read_row(row))
    {
        json_error_t err;
        json_t* js = json_loads(row.c_str(), JSON_ALLOW_NUL, &err);

        if (js)
        {
            if (is_schema(js))
            {
                m_schema = row;
                process_schema(js);
                rval = Connection::read();
            }
            else
            {
                rval = process_row(js);
            }

            json_decref(js);
        }
        else
        {
            m_error = "Failed to parse JSON: ";
            m_error += err.text;
        }
    }

    return rval;
}

/**
 * Private functions
 */

bool Connection::do_auth()
{
    bool rval = false;
    std::string auth_str = generateAuthString(m_user, m_password);

    /** Send the auth string */
    if (nointr_write(auth_str.c_str(), auth_str.length()) == -1)
    {
        char err[ERRBUF_SIZE];
        m_error = "Failed to write authentication data: ";
        m_error += strerror_r(errno, err, sizeof(err));
    }
    else
    {
        /** Read the response */
        char buf[READBUF_SIZE];
        int bytes;

        if ((bytes = nointr_read(buf, sizeof(buf))) == -1)
        {
            char err[ERRBUF_SIZE];
            m_error = "Failed to read authentication response: ";
            m_error += strerror_r(errno, err, sizeof(err));
        }
        else if (memcmp(buf, OK_RESPONSE, sizeof(OK_RESPONSE) - 1) != 0)
        {
            buf[bytes] = '\0';
            m_error = "Authentication failed: ";
            m_error += bytes > 0 ? buf : "Request timed out";

        }
        else
        {
            rval = true;
        }
    }

    return rval;
}

bool Connection::do_registration()
{
    bool rval = false;
    std::string reg_msg(REGISTER_MSG);
    reg_msg += "JSON";

    /** Send the registration message */
    if (nointr_write(reg_msg.c_str(), reg_msg.length()) == -1)
    {
        char err[ERRBUF_SIZE];
        m_error = "Failed to write registration message: ";
        m_error += strerror_r(errno, err, sizeof(err));
    }
    else
    {
        /** Read the response */
        char buf[READBUF_SIZE];
        int bytes;

        if ((bytes = nointr_read(buf, sizeof(buf))) == -1)
        {
            char err[ERRBUF_SIZE];
            m_error = "Failed to read registration response: ";
            m_error += strerror_r(errno, err, sizeof(err));
        }
        else if (memcmp(buf, OK_RESPONSE, sizeof(OK_RESPONSE) - 1) != 0)
        {
            buf[bytes] = '\0';
            m_error = "Registration failed: ";
            m_error += buf;
        }
        else
        {
            rval = true;
        }
    }

    return rval;
}

bool Connection::is_error(const char* str)
{
    bool rval = false;

    if (str[0] == 'E' && str[1] == 'R' && str[2] == 'R')
    {
        m_error = "MaxScale responded with an error: ";
        m_error += str;
        rval = true;
    }

    return rval;
}

bool Connection::read_row(std::string& dest)
{
    bool rval = true;

    while (true)
    {
        if (!m_buffer.empty())
        {
            std::vector<char>::iterator it = std::find(m_buf_ptr, m_buffer.end(), '\n');
            if (it != m_buffer.end())
            {
                dest.assign(m_buf_ptr, it);
                m_buf_ptr = it + 1;
                break;
            }
        }

        char buf[READBUF_SIZE + 1];
        int rc = nointr_read(&buf, sizeof(buf));

        if (rc == -1)
        {
            rval = false;
            char err[ERRBUF_SIZE];
            m_error = "Failed to read row: ";
            m_error += strerror_r(errno, err, sizeof(err));
            break;
        }
        else if (rc == 0)
        {
            rval = false;
            m_error = CDC::TIMEOUT;
            break;
        }

        if (!m_connected)
        {
            // This is here to work around a missing newline in MaxScale error messages
            buf[rc] = '\0';

            if (is_error(buf))
            {
                rval = false;
                break;
            }
        }

        m_buffer.erase(m_buffer.begin(), m_buf_ptr);
        assert(std::find(m_buffer.begin(), m_buffer.end(), '\n') == m_buffer.end());
        m_buffer.insert(m_buffer.end(), buf, buf + rc);
        m_buf_ptr = m_buffer.begin();
    }

    if (!m_connected && is_error(dest.c_str()))
    {
        rval = false;
    }

    return rval;
}

#define is_poll_error(e) ((e & (POLLERR | POLLHUP | POLLNVAL)))

static std::string event_to_string(int event)
{
    std::string rval;

    if (event & POLLIN)
    {
        rval += "POLLIN ";
    }
    if (event & POLLPRI)
    {
        rval += "POLLPRI ";
    }
    if (event & POLLOUT)
    {
        rval += "POLLOUT ";
    }
#ifdef POLLRDHUP
    if (event & POLLRDHUP)
    {
        rval += "POLLRDHUP ";
    }
#endif
    if (event & POLLERR)
    {
        rval += "POLLERR ";
    }
    if (event & POLLHUP)
    {
        rval += "POLLHUP ";
    }
    if (event & POLLNVAL)
    {
        rval += "POLLNVAL ";
    }

    return rval;
}

int Connection::wait_for_event(short events)
{
    nfds_t nfds = 1;
    struct pollfd pfd;
    pfd.fd = m_fd;
    pfd.events = events;
    int rc;

    while ((rc = poll(&pfd, nfds, m_timeout * 1000)) < 0 && errno == EINTR)
    {
        ;
    }

    if (rc > 0 && is_poll_error(pfd.revents))
    {
        rc = -1;
        m_error += "Error when waiting event; ";
        m_error += event_to_string(pfd.revents);
    }
    else if (rc < 0)
    {
        char err[ERRBUF_SIZE];
        m_error = "Failed to wait for event: ";
        m_error += strerror_r(errno, err, sizeof(err));
    }

    return rc;
}

int Connection::nointr_read(void *dest, size_t size)
{
    int n_bytes = 0;

    if (wait_for_event(POLLIN) > 0)
    {
        int rc = 0;

        while ((rc = ::read(m_fd, dest, size)) < 0 && errno == EINTR)
        {
            ;
        }

        if (rc == -1 && errno != EWOULDBLOCK && errno != EAGAIN)
        {
            char err[ERRBUF_SIZE];
            m_error = "Failed to read data: ";
            m_error += strerror_r(errno, err, sizeof(err));
            n_bytes = -1;
        }
        else if (rc > 0)
        {
            n_bytes += rc;
        }
    }

    return n_bytes;
}

int Connection::nointr_write(const void *src, size_t size)
{
    int rc = 0;
    int n_bytes = 0;

    if (wait_for_event(POLLOUT) > 0)
    {
        while ((rc = ::write(m_fd, src, size)) < 0 && errno == EINTR)
        {
            ;
        }

        if (rc < 0 && errno != EWOULDBLOCK && errno != EAGAIN)
        {
            char err[ERRBUF_SIZE];
            m_error = "Failed to write data: ";
            m_error += strerror_r(errno, err, sizeof(err));
            n_bytes = -1;
        }
        else if (rc > 0)
        {
            n_bytes += rc;
        }
    }

    return n_bytes;
}

}
