/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/http.hh>
#include <algorithm>
#include <array>
#include <chrono>
#include <map>
#include <unordered_map>
#include <utility>
#include <thread>
#include <curl/curl.h>
#include <maxbase/assert.hh>
#include <maxbase/string.hh>
using namespace std;

using std::array;
using std::map;
using std::string;
using std::unordered_map;
using std::vector;

namespace
{

static struct THIS_UNIT
{
    int nInits;
} this_unit =
{
    0
};

using namespace mxb;
using namespace mxb::http;

int translate_curl_code(CURLcode code)
{
    switch (code)
    {
    case CURLE_OK:
        return 0;

    case CURLE_COULDNT_RESOLVE_HOST:
        return Response::COULDNT_RESOLVE_HOST;

    case CURLE_OPERATION_TIMEDOUT:
        return Response::OPERATION_TIMEDOUT;

    default:
        return Response::ERROR;
    }
}

template<class T>
inline int checked_curl_setopt(CURL* pCurl, CURLoption option, T value)
{
    CURLcode rv = curl_easy_setopt(pCurl, option, value);
    mxb_assert(rv == CURLE_OK);

    return rv == CURLE_OK ? 0 : 1;
}

struct ReadCallbackData
{
public:
    ReadCallbackData(const std::string* pBody)
        : body(*pBody)
    {
    }

    const std::string& body;
    size_t             pos = 0;
};

// https://curl.haxx.se/libcurl/c/CURLOPT_READFUNCTION.html
size_t read_callback(char* buffer, size_t size, size_t nitems, void* userdata)
{
    ReadCallbackData* pRcd = static_cast<ReadCallbackData*>(userdata);

    size_t nBytes = size * nitems;

    if (pRcd->pos + nBytes > pRcd->body.size())
    {
        nBytes = pRcd->body.size() - pRcd->pos;
    }

    auto b = pRcd->body.begin() + pRcd->pos;
    auto e = b + nBytes;

    mxb_assert(e <= pRcd->body.end());

    std::copy(b, e, buffer);

    pRcd->pos += nBytes;

    return nBytes;
}

// https://curl.haxx.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    // ptr points to the delivered data, and the size of that data is nmemb;
    // size is always 1.
    mxb_assert(size == 1);

    string* pString = static_cast<string*>(userdata);

    if (nmemb > 0)
    {
        pString->append(ptr, nmemb);
    }

    return nmemb;
}

// https://curl.haxx.se/libcurl/c/CURLOPT_HEADERFUNCTION
size_t header_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    size_t len = size * nmemb;

    if (len > 0)
    {
        map<string, string>* pHeaders = static_cast<std::map<string, string>*>(userdata);

        char* end = ptr + len;
        char* i = std::find(ptr, end, ':');

        if (i != end)
        {
            string key(ptr, i - ptr);
            ++i;
            string value(i, end - i);
            mxb::trim(key);
            mxb::trim(value);
            pHeaders->insert(std::make_pair(key, value));
        }
    }

    return len;
}

enum class CurlOp
{
    GET,
    PUT,
    DELETE,
    POST,
};

CURL* get_easy_curl(CurlOp op,
                    const std::string& url,
                    const std::string& user, const std::string& password,
                    const Config& config,
                    Response* pRes,
                    char* pErrbuf,
                    curl_slist* pHeaders,
                    ReadCallbackData* pRcd)
{
    CURL* pCurl = curl_easy_init();
    mxb_assert(pCurl);

    if (pCurl)
    {
        if (op == CurlOp::PUT)
        {
            // checked_curl_setopt(pCurl, CURLOPT_PUT, 1) cannot be used as it expects
            // a file to be uploaded.
            checked_curl_setopt(pCurl, CURLOPT_CUSTOMREQUEST, "PUT");
        }
        else if (op == CurlOp::DELETE)
        {
            checked_curl_setopt(pCurl, CURLOPT_CUSTOMREQUEST, "DELETE");
        }
        else if (op == CurlOp::POST)
        {
            checked_curl_setopt(pCurl, CURLOPT_CUSTOMREQUEST, "POST");
        }

        if (!config.ssl_verifypeer)
        {
            checked_curl_setopt(pCurl, CURLOPT_SSL_VERIFYPEER, (long)0);
        }

        if (!config.ssl_verifyhost)
        {
            checked_curl_setopt(pCurl, CURLOPT_SSL_VERIFYHOST, (long)0);
        }

        checked_curl_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
        checked_curl_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, config.connect_timeout.count()); // Conn. phase
        checked_curl_setopt(pCurl, CURLOPT_TIMEOUT, config.timeout.count());                // Data trs phase
        checked_curl_setopt(pCurl, CURLOPT_ERRORBUFFER, pErrbuf);
        checked_curl_setopt(pCurl, CURLOPT_WRITEFUNCTION, write_callback);
        checked_curl_setopt(pCurl, CURLOPT_WRITEDATA, &pRes->body);
        checked_curl_setopt(pCurl, CURLOPT_URL, url.c_str());
        checked_curl_setopt(pCurl, CURLOPT_HEADERFUNCTION, header_callback);
        checked_curl_setopt(pCurl, CURLOPT_HEADERDATA, &pRes->headers);

        if (pHeaders)
        {
            checked_curl_setopt(pCurl, CURLOPT_HTTPHEADER, pHeaders);
        }

        if (pRcd && pRcd->body.size() != 0)
        {
            checked_curl_setopt(pCurl, CURLOPT_UPLOAD, 1L);
            checked_curl_setopt(pCurl, CURLOPT_READFUNCTION, read_callback);
            checked_curl_setopt(pCurl, CURLOPT_READDATA, pRcd);
            checked_curl_setopt(pCurl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)pRcd->body.size());
        }

        if (!user.empty() && !password.empty())
        {
            // In release mode we will silently ignore the unlikely event that the escaping fails.
            char* zU = curl_easy_escape(pCurl, user.c_str(), user.length());
            mxb_assert(zU);
            char* zP = curl_easy_escape(pCurl, password.c_str(), password.length());
            mxb_assert(zP);

            string u(zU ? zU : user);
            string p(zP ? zP : password);

            curl_free(zU);
            curl_free(zP);

            checked_curl_setopt(pCurl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            checked_curl_setopt(pCurl, CURLOPT_USERPWD, (u + ":" + p).c_str());
        }
    }

    return pCurl;
}

curl_slist* create_headers(const std::map<std::string, std::string>& headers)
{
    curl_slist* pHeaders = nullptr;

    for (const auto& kv : headers)
    {
        string header = kv.first + ":" + kv.second;
        pHeaders = curl_slist_append(pHeaders, header.c_str());
    }

    return pHeaders;
}

using Errbuf = array<char, CURL_ERROR_SIZE + 1>;

struct Context
{
    Context(Response* pResponse,
            Errbuf* pErrbuf)
        : pResponse(pResponse)
        , pErrbuf(pErrbuf)
    {
    }

    mxb::http::Response* pResponse;
    Errbuf*              pErrbuf;
};

class ReadyImp : public Async::Imp
{
public:
    ReadyImp(Async::status_t status = Async::READY)
        : m_status(status)
    {
    }

    Async::status_t status() const override
    {
        return m_status;
    }

    Async::status_t perform(long timeout_ms) override
    {
        return m_status;
    }

    long wait_no_more_than() const override
    {
        return 0;
    }

    const vector<Response>& responses() const override
    {
        return m_responses;
    }

    const vector<string>& urls() const override
    {
        return m_urls;
    }

private:
    Async::status_t  m_status;
    vector<Response> m_responses;
    vector<string>   m_urls;
};

class HttpImp : public Async::Imp
{
public:
    HttpImp()
        : m_pCurlm(curl_multi_init())
        , m_status(Async::ERROR)
        , m_still_running(0)
        , m_wait_no_more_than(0)
    {
        mxb_assert(m_pCurlm);
        if (!m_pCurlm)
        {
            throw std::bad_alloc();
        }
    }

    ~HttpImp()
    {
        mxb_assert(m_pCurlm);

        for (auto& item : m_curls)
        {
            CURL* pCurl = item.first;
            MXB_AT_DEBUG(CURLMcode rv = ) curl_multi_remove_handle(m_pCurlm, pCurl);
            mxb_assert(rv == CURLM_OK);
            curl_easy_cleanup(pCurl);
        }

        CURLMcode code = curl_multi_cleanup(m_pCurlm);
        if (code != CURLM_OK)
        {
            MXB_ERROR("curl_multi_cleanup() failed: %s", curl_multi_strerror(code));
        }

        curl_slist_free_all(m_pHeaders);
    }

    bool initialize(CurlOp op,
                    const std::vector<std::string>& urls,
                    const std::string& body,
                    const std::string& user, const std::string& password,
                    const Config& config)
    {
        mxb_assert(m_status == Async::ERROR);

        m_urls = urls;
        m_body = body;
        m_pHeaders = create_headers(config.headers);

        m_responses.reserve(urls.size());
        m_errbufs.reserve(urls.size());

        if (m_body.size())
        {
            m_rcds.reserve(urls.size());
        }

        size_t i;
        for (i = 0; i < urls.size(); ++i)
        {
            m_responses.resize(i + 1);
            m_errbufs.resize(i + 1);

            if (m_body.size())
            {
                m_rcds.emplace_back(&m_body);
            }

            CURL* pCurl = get_easy_curl(op, urls[i], user, password, config,
                                        &m_responses[i], m_errbufs[i].data(),
                                        m_pHeaders,
                                        m_body.empty() ? nullptr : &m_rcds[i]);

            if (!pCurl || (curl_multi_add_handle(m_pCurlm, pCurl) != CURLM_OK))
            {
                mxb_assert(!true);
                if (pCurl)
                {
                    curl_easy_cleanup(pCurl);
                }
                m_responses.resize(m_responses.size() - 1);
                break;
            }
            else
            {
                m_curls.insert(std::make_pair(pCurl, Context(&m_responses[i], &m_errbufs[i])));
            }
        }

        if (m_responses.size() == urls.size())
        {
            CURLMcode rv = curl_multi_perform(m_pCurlm, &m_still_running);

            if (rv == CURLM_OK)
            {
                if (m_still_running == 0)
                {
                    m_status = Async::READY;
                    m_wait_no_more_than = 0;

                    collect_response();
                }
                else
                {
                    update_timeout();
                    m_status = Async::PENDING;
                }
            }
            else
            {
                MXB_ERROR("curl_multi_perform() failed: %s", curl_multi_strerror(rv));
                m_status = Async::ERROR;
            }
        }

        return m_status != Async::ERROR;
    }

    Async::status_t status() const override
    {
        return m_status;
    }

    Async::status_t perform(long timeout_ms) override
    {
        switch (m_status)
        {
        case Async::READY:
        case Async::ERROR:
            break;

        case Async::PENDING:
            {
                fd_set fdread;
                fd_set fdwrite;
                fd_set fdexcep;

                FD_ZERO(&fdread);
                FD_ZERO(&fdwrite);
                FD_ZERO(&fdexcep);

                int maxfd;
                CURLMcode rv_curl = curl_multi_fdset(m_pCurlm, &fdread, &fdwrite, &fdexcep, &maxfd);

                if (rv_curl == CURLM_OK)
                {
                    int rv = 0;
                    if (maxfd != -1)
                    {
                        struct timeval timeout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
                        rv = select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
                    }

                    switch (rv)
                    {
                    case -1:
                        mxb_assert(!true);
                        MXB_ERROR("select() failed: %s", mxb_strerror(errno));
                        m_status = Async::ERROR;
                        break;

                    case 0:
                    default:
                        rv_curl = curl_multi_perform(m_pCurlm, &m_still_running);
                        if (rv_curl == CURLM_OK)
                        {
                            if (m_still_running == 0)
                            {
                                m_status = Async::READY;
                            }
                            else
                            {
                                update_timeout();
                            }
                        }
                        else
                        {
                            MXB_ERROR("curl_multi_perform() failed: %s", curl_multi_strerror(rv_curl));
                            m_status = Async::ERROR;
                        }
                    }
                }

                if (m_status == Async::READY)
                {
                    collect_response();
                }
            }
        }

        return m_status;
    }

    long wait_no_more_than() const override
    {
        return m_wait_no_more_than;
    }

    const vector<Response>& responses() const override
    {
        return m_responses;
    }

    const vector<string>& urls() const override
    {
        return m_urls;
    }

private:
    void update_timeout()
    {
        curl_multi_timeout(m_pCurlm, &m_wait_no_more_than);
        if (m_wait_no_more_than < 0)
        {
            // No default value, we'll use 100ms as default.
            m_wait_no_more_than = 100;
        }
    }

    void collect_response()
    {
        mxb_assert(m_status == Async::READY);
        mxb_assert(m_still_running == 0);

        int nRemaining = 0;
        do
        {
            CURLMsg* pMsg = curl_multi_info_read(m_pCurlm, &nRemaining);
            if (pMsg && (pMsg->msg == CURLMSG_DONE))
            {
                CURL* pCurl = pMsg->easy_handle;
                auto it = m_curls.find(pCurl);
                mxb_assert(it != m_curls.end());

                auto& context = it->second;
                Response* pResponse = context.pResponse;
                Errbuf* pErrbuf = context.pErrbuf;

                if (pMsg->data.result == CURLE_OK)
                {
                    long code;
                    curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &code);
                    pResponse->code = code;
                }
                else
                {
                    pResponse->code = translate_curl_code(pMsg->data.result);
                    pResponse->body = pErrbuf->data();
                }

                m_curls.erase(it);
                curl_multi_remove_handle(m_pCurlm, pCurl);
                curl_easy_cleanup(pCurl);
            }
        }
        while (nRemaining != 0);
    }

private:
    CURLM*                                   m_pCurlm {nullptr};
    Async::status_t                          m_status;
    vector<Response>                         m_responses;
    vector<array<char, CURL_ERROR_SIZE + 1>> m_errbufs;
    unordered_map<CURL*, Context>            m_curls;
    int                                      m_still_running;
    long                                     m_wait_no_more_than;
    vector<string>                           m_urls;
    string                                   m_body;
    curl_slist*                              m_pHeaders {nullptr};
    vector<ReadCallbackData>                 m_rcds;
};
}


namespace maxbase
{

namespace http
{

bool init()
{
    bool rv = true;

    if (this_unit.nInits == 0)
    {
        CURLcode code = curl_global_init(CURL_GLOBAL_ALL);

        if (code == CURLE_OK)
        {
            this_unit.nInits = 1;
        }
        else
        {
            MXB_ERROR("Failed to initialize CURL library: %s", curl_easy_strerror(code));
            rv = false;
        }
    }

    return rv;
}

void finish()
{
    mxb_assert(this_unit.nInits > 0);

    if (--this_unit.nInits == 0)
    {
        curl_global_cleanup();
    }
}

// static
const char* Response::to_string(int code)
{
    if (code < 0)
    {
        switch (code)
        {
        case ERROR:
            return "Unspecified HTTP error.";

        case COULDNT_RESOLVE_HOST:
            return "Could not resolve host.";

        case OPERATION_TIMEDOUT:
            return "Operation timed out.";

        default:
            return "Unknown error.";
        }
    }
    else if (code < INFORMATIONAL)
    {
        return "Unknown HTTP response code.";
    }
    else if (code < SUCCESS)
    {
        return "Informational response.";
    }
    else if (code < REDIRECTION)
    {
        return "Successful response.";
    }
    else if (code < CLIENT_ERROR)
    {
        return "Redirection.";
    }
    else if (code < SERVER_ERROR)
    {
        return "Client error.";
    }
    else
    {
        return "Server error";
    }
}

Async::Imp::~Imp()
{
}

Async::Async()
    : m_sImp(std::make_shared<ReadyImp>())
{
}

void Async::reset()
{
    m_sImp = std::make_shared<ReadyImp>();
}

namespace
{

Async create_async(CurlOp op,
                   const std::vector<std::string>& urls,
                   const std::string& body,
                   const std::string& user, const std::string& password,
                   const Config& config)
{
    shared_ptr<Async::Imp> sImp;

    if (urls.empty())
    {
        sImp = std::make_shared<ReadyImp>(Async::READY);
    }
    else
    {
        shared_ptr<HttpImp> sHttp_imp = std::make_shared<HttpImp>();
        if (sHttp_imp->initialize(op, urls, body, user, password, config))
        {
            sImp = sHttp_imp;
        }
        else
        {
            sImp = std::make_shared<ReadyImp>(Async::ERROR);
        }
    }

    return Async(sImp);
}
}

Async get_async(const std::vector<std::string>& urls,
                const std::string& user, const std::string& password,
                const Config& config)
{
    return create_async(CurlOp::GET, urls, string(), user, password, config);
}

Async put_async(const std::vector<std::string>& urls,
                const std::string& body,
                const std::string& user, const std::string& password,
                const Config& config)
{
    return create_async(CurlOp::PUT, urls, body, user, password, config);
}

namespace
{

Response execute(CurlOp op,
                 const std::string& url,
                 const std::string& body,
                 const std::string& user,
                 const std::string& password,
                 const Config& config)
{
    Response res;
    char errbuf[CURL_ERROR_SIZE + 1] = "";
    curl_slist* pHeaders = create_headers(config.headers);
    ReadCallbackData rcd(&body);
    CURL* pCurl = get_easy_curl(op, url, user, password, config, &res, errbuf, pHeaders, &rcd);
    mxb_assert(pCurl);

    CURLcode code = curl_easy_perform(pCurl);

    switch (code)
    {
    case CURLE_OK:
        {
            long resp_code = 0;     // needs to be a long
            curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &resp_code);
            res.code = resp_code;
        }
        break;

    default:
        res.code = translate_curl_code(code);
        res.body = errbuf;
    }

    curl_easy_cleanup(pCurl);
    curl_slist_free_all(pHeaders);

    return res;
}

vector<Response> execute(CurlOp op,
                         const std::vector<std::string>& urls,
                         const std::string& body,
                         const std::string& user, const std::string& password,
                         const Config& config)
{
    Async http = create_async(op, urls, body, user, password, config);

    long timeout_ms = (config.connect_timeout.count() + config.timeout.count()) * 1000;
    long max_wait_ms = timeout_ms / 10;

    long wait_ms = 10;
    while (http.perform(wait_ms) == Async::PENDING)
    {
        wait_ms = http.wait_no_more_than();

        if (wait_ms > max_wait_ms)
        {
            wait_ms = max_wait_ms;
        }
    }

    vector<Response> responses(http.responses());

    if (responses.size() != urls.size())
    {
        responses.resize(urls.size());
    }

    return responses;
}
}

Response get(const std::string& url,
             const std::string& user,
             const std::string& password,
             const Config& config)
{
    return execute(CurlOp::GET, url, std::string(), user, password, config);
}

vector<Response> get(const std::vector<std::string>& urls,
                     const std::string& user, const std::string& password,
                     const Config& config)
{
    return execute(CurlOp::GET, urls, std::string(), user, password, config);
}

Response put(const std::string& url,
             const std::string& body,
             const std::string& user,
             const std::string& password,
             const Config& config)
{
    return execute(CurlOp::PUT, url, body, user, password, config);
}

vector<Response> put(const std::vector<std::string>& urls,
                     const std::string& body,
                     const std::string& user, const std::string& password,
                     const Config& config)
{
    return execute(CurlOp::PUT, urls, body, user, password, config);
}

Response del(const std::string& url,
             const std::string& body,
             const std::string& user,
             const std::string& password,
             const Config& config)
{
    return execute(CurlOp::DELETE, url, body, user, password, config);
}

Response post(const std::string& url,
              const std::string& body,
              const std::string& user,
              const std::string& password,
              const Config& config)
{
    return execute(CurlOp::POST, url, body, user, password, config);
}

const char* to_string(Async::status_t status)
{
    switch (status)
    {
    case Async::READY:
        return "READY";

    case Async::PENDING:
        return "PENDING";

    case Async::ERROR:
        return "ERROR";
    }

    mxb_assert(!true);
    return "Unknown";
}
namespace
{
using MethodString = std::pair<Method, std::string>;
std::array method_pairs{MethodString{Method::GET, "GET"},
                        MethodString{Method::PUT, "PUT"},
                        MethodString{Method::POST, "POST"},
                        MethodString{Method::PATCH, "PATCH"},
                        MethodString{Method::DELETE, "DELETE"},
                        MethodString{Method::HEAD, "HEAD"},
                        MethodString{Method::CONNECT, "CONNECT"},
                        MethodString{Method::OPTIONS, "OPTIONS"},
                        MethodString{Method::TRACE, "TRACE"}};
}

std::string to_string(Method method)
{
    for (const auto& p : method_pairs)
    {
        if (method == p.first)
        {
            return p.second;
        }
    }

    return "Unknown";
}

Method from_string(const std::string& str)
{
    for (const auto& p : method_pairs)
    {
        if (maxbase::upper_case_copy(str) == p.second)
        {
            return p.first;
        }
    }

    return Method::INVALID;
}

// *uncrustify-off*
string code_to_string(int code)
{
    switch (code)
    {
    // Response::Category::INFORMATIONAL
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 102: return "Processing";
    case 103: return "Early Hints";

    // Response::Category::SUCCESS
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";
    case 208: return "Already Reported";
    case 226: return "IM Used";

    // Response::Category::REDIRECTION
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";

    // Response::Category::CLIENT_ERROR
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 421: return "Misdirected Request";
    case 422: return "Unprocessable Content";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 425: return "Too Early";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 451: return "Unavailable For Legal Reasons";

    // Response::Category::SERVER_ERROR
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    case 506: return "Variant Also Negotiates";
    case 507: return "Insufficient Storage";
    case 508: return "Loop Detected";
    case 510: return "Not Extended";
    case 511: return "Network Authentication Required";

    default: return "Unknown";
    }
}
// *uncrustify-on*
}
}
