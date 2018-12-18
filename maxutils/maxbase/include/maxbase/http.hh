/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace maxbase
{

namespace http
{

/**
 * Initialize the http library.
 *
 * @return True if successful, false otherwise.
 */
bool init();

/**
 * Finalize the http library.
 */
void finish();

/**
 * RAII class for initializing the http functionality.
 */
class Init
{
public:
    Init()
    {
        if (!mxb::http::init())
        {
            throw std::runtime_error("Could not initialize mxb::http.");
        }
    }
    ~Init()
    {
        mxb::http::finish();
    }
};

enum
{
    DEFAULT_CONNECT_TIMEOUT = 10, // @see https://curl.haxx.se/libcurl/c/CURLOPT_CONNECTTIMEOUT.html
    DEFAULT_TIMEOUT = 10          // @see https://curl.haxx.se/libcurl/c/CURLOPT_TIMEOUT.html
};

struct Config
{
    int connect_timeout_s = DEFAULT_CONNECT_TIMEOUT;
    int timeout_s         = DEFAULT_TIMEOUT;
};

struct Result
{
    enum
    {
        ERROR                = -1, // Some non-specific error occurred.
        COULDNT_RESOLVE_HOST = -2, // The specified host cold not be resolved.
        OPERATION_TIMEDOUT   = -3  // The operation timed out.
    };

    int                                code = 0; // HTTP response code
    std::string                        body;     // Response body
    std::map<std::string, std::string> headers;  // Headers attached to the response
};

/**
 * Do a HTTP GET, when no user/password is required.
 *
 * @param url     The URL to GET.
 * @param config  The config to use.
 *
 * @note The @c url is assumed to be escaped in case it contain arguments
 *       that must be escaped.
 *
 * @return A @c Result.
 */
Result get(const std::string& url, const Config& config = Config());

/**
 * Do a HTTP GET
 *
 * @param url       The URL to GET.
 * @param user      Username to use.
 * @param password  Password for the user.
 * @param config    The config to use.
 *
 * @note The @c url is assumed to be escaped in case it contain arguments
 *       that must be escaped, but @c user and @c pass will always be escaped.
 *
 * @return A @c Result.
 */
Result get(const std::string& url,
           const std::string& user, const std::string& password,
           const Config& config = Config());

/**
 * Do a HTTP GET, when no user/password is required.
 *
 * @param urls    The URLs to GET.
 * @param config  The config to use.
 *
 * @note The @c urls are assumed to be escaped in case they contain arguments
 *       that must be escaped.
 *
 * @return A @c Result.
 */
std::vector<Result> get(const std::vector<std::string>& urls,
                        const Config& config = Config());

/**
 * Do a HTTP GET
 *
 * @param urls      The URLs to GET.
 * @param user      Username to use.
 * @param password  Password for the user.
 * @param config    The config to use.
 *
 * @note The @c urls are assumed to be escaped in case they contain arguments
 *       that must be escaped, but @c user and @c pass will always be escaped.
 *
 * @return Vector of @c Results, as many as there were @c urls.
 */
std::vector<Result> get(const std::vector<std::string>& urls,
                        const std::string& user, const std::string& password,
                        const Config& config = Config());

/**
 * @class mxb::http::Async
 *
 * Class for performing multiple HTTP GETs concurrently and asynchronously.
 * The instance should be viewed as a handle to the operation. If it is
 * copied, both instances refer to the same operation and both instances
 * can be used for driving the GET. However, an instance can *only* be
 * used or copied in the thread where it is created.
 */
class Async
{
public:
    enum status_t
    {
        READY,  // The result is ready.
        ERROR,  // The operation has failed.
        PENDING // The operation is pending.
    };

    class Imp
    {
    public:
        virtual ~Imp();
        virtual status_t status() const = 0;

        virtual status_t perform(long timeout_ms) = 0;

        virtual long wait_no_more_than() const = 0;

        virtual const std::vector<Result>& results() const = 0;
    };

    /**
     * Defalt constructor creates an asynchronous operation whose status is ERROR.
     */
    Async();

    /**
     * Copy constructor; creates an instance that refers to the same operation
     * the argument refers to.
     *
     * @param rhs  An existing @c Async instance.
     */
    Async(const Async& rhs)
        : m_sImp(rhs.m_sImp)
    {
    }

    /**
     * Assigns an asynchronous operation.
     *
     * @param rhs  An other @c Async instance.
     *
     * @return *this.
     */
    Async& operator = (const Async& rhs)
    {
        std::shared_ptr<Imp> sImp(rhs.m_sImp);
        m_sImp.swap(sImp);
        return *this;
    }

    /**
     * Return the status of the operation.
     *
     * @return @c READY|ERROR|PENDING
     */
    status_t status() const
    {
        return m_sImp->status();
    }

    /**
     * Performs a step in the operation.
     *
     * @param timeout_ms  The maximum timeout for waiting for activity on the
     *                    underlying socket descriptors.
     *
     * @return @c READY|ERROR|PENDING
     */
    status_t perform(long timeout_ms = 0)
    {
        return m_sImp->perform(timeout_ms);
    }

    /**
     * How much time to wait at most, before calling perform() again.
     *
     * This value is dependent upon the timeouts that were specified when
     * the operation was initiated. To ensure that operations are not timed
     * out, do not wait as long as this function returns but significantly
     * less.
     *
     * @return Maximum time to wait in milliseconds.
     */
    long wait_no_more_than() const
    {
        return m_sImp->wait_no_more_than();
    }

    /**
     * The result of each operation. This function should not be called
     * before the status is READY.
     *
     * @return Vector of results.
     */
    const std::vector<Result>& results() const
    {
        return m_sImp->results();
    }

public:
    Async(const std::shared_ptr<Imp>& sImp)
        : m_sImp(sImp)
    {
    }

private:
    std::shared_ptr<Imp> m_sImp;
};

/**
 * Return human-readable string for a status value.
 *
 * @param status  A status value.
 *
 * @return The corresponding string.
 */
const char* to_string(Async::status_t status);

/**
 * Do a HTTP GET, asynchronously.
 *
 * @param urls      The URLs to GET.
 * @param config    The config to use.
 *
 * @note The @c urls are assumed to be escaped in case they contain arguments
 *       that must be escaped.
 *
 * @return An Async instance using which the operation can be performed.
 */
Async get_async(const std::vector<std::string>& urls,
                const Config& config = Config());

/**
 * Do a HTTP GET, asynchronously.
 *
 * @param urls      The URLs to GET.
 * @param user      Username to use.
 * @param password  Password for the user.
 * @param config    The config to use.
 *
 * @note The @c urls are assumed to be escaped in case they contains arguments
 *       that must be escaped, but @c user and @c pass will always be escaped.
 *
 * @return An Async instance using which the operation can be performed.
 */
Async get_async(const std::vector<std::string>& urls,
                const std::string& user, const std::string& password,
                const Config& config = Config());

}

}
