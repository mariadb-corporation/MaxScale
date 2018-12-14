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

#include <maxbase/http.hh>
#include <algorithm>
#include <array>
#include <unordered_map>
#include <curl/curl.h>
#include <maxbase/assert.h>
#include <maxbase/string.hh>
#include <iostream>
using namespace std;

using std::array;
using std::map;
using std::string;
using std::unordered_map;
using std::vector;

namespace
{

using namespace mxb;
using namespace mxb::http;

template<class T>
inline int checked_curl_setopt(CURL* pCurl, CURLoption option, T value)
{
    CURLcode rv = curl_easy_setopt(pCurl, option, value);
    mxb_assert(rv == CURLE_OK);

    return rv == CURLE_OK ? 0 : 1;
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

CURL* get_easy_curl(const std::string& url,
                    const std::string& user, const std::string& password,
                    const Config& config,
                    Result *pRes,
                    char* pErrbuf)
{
    CURL* pCurl = curl_easy_init();
    mxb_assert(pCurl);

    if (pCurl)
    {
        checked_curl_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
        checked_curl_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, config.connect_timeout_s); // For connection phase
        checked_curl_setopt(pCurl, CURLOPT_TIMEOUT, config.timeout_s);                // For data transfer phase
        checked_curl_setopt(pCurl, CURLOPT_ERRORBUFFER, pErrbuf);
        checked_curl_setopt(pCurl, CURLOPT_WRITEFUNCTION, write_callback);
        checked_curl_setopt(pCurl, CURLOPT_WRITEDATA, &pRes->body);
        checked_curl_setopt(pCurl, CURLOPT_URL, url.c_str());
        checked_curl_setopt(pCurl, CURLOPT_HEADERFUNCTION, header_callback);
        checked_curl_setopt(pCurl, CURLOPT_HEADERDATA, &pRes->headers);

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

}


namespace maxbase
{

namespace http
{

Result get(const std::string& url, const Config& config)
{
    return std::move(get(url, "", "", config));
}

Result get(const std::string& url, const std::string& user, const std::string& password, const Config& config)
{
    Result res;
    char errbuf[CURL_ERROR_SIZE + 1] = "";
    CURL* pCurl = get_easy_curl(url, user, password, config, &res, errbuf);
    mxb_assert(pCurl);


    if (curl_easy_perform(pCurl) == CURLE_OK)
    {
        long code = 0; // needs to be a long
        curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &code);
        res.code = code;
    }
    else
    {
        res.code = -1;
        res.body = errbuf;
    }

    curl_easy_cleanup(pCurl);

    return res;
}

vector<Result> get(const std::vector<std::string>& urls, const Config& config)
{
    return get(urls, "", "", config);
}

namespace
{

using Errbuf = array<char, CURL_ERROR_SIZE + 1>;

struct Context
{
    Context(Result* pResult,
            Errbuf* pErrbuf)
        : pResult(pResult)
        , pErrbuf(pErrbuf)
    {
    }

    mxb::http::Result* pResult;
    Errbuf *           pErrbuf;
};

void execute(CURLM* pCurlm,
             const Config& config,
             unordered_map<CURL*, Context>& curls)
{
    int timeout_ms = (config.connect_timeout_s + config.timeout_s) * 1000;
    int still_running {0};
    int numfs {0};
    int repeats {0};

    CURLMcode rv = curl_multi_perform(pCurlm, &still_running);

    if (rv == CURLM_OK)
    {
        while ((rv == CURLM_OK) && (still_running != 0))
        {
            fd_set fdread;
            fd_set fdwrite;
            fd_set fdexcep;

            FD_ZERO(&fdread);
            FD_ZERO(&fdwrite);
            FD_ZERO(&fdexcep);

            long default_timeout = 100; // 100ms

            long curl_timeout = -1;
            curl_multi_timeout(pCurlm, &curl_timeout);

            if ((curl_timeout >= 0) && (curl_timeout < default_timeout))
            {
                default_timeout = curl_timeout;
            }

            struct timeval timeout = { 0, default_timeout };

            int maxfd;
            rv = curl_multi_fdset(pCurlm, &fdread, &fdwrite, &fdexcep, &maxfd);

            if (rv == CURLM_OK)
            {
                int rc;

                if (maxfd == -1)
                {
                    struct timeval wait = { 0, 100 * 1000 }; /* 100ms */
                    rc = select(0, NULL, NULL, NULL, &wait);
                }
                else
                {
                    rc = select(maxfd + 1,  &fdread, &fdwrite, &fdexcep, &timeout);
                }

                switch (rc)
                {
                case -1:
                    mxb_assert(!true);
                    MXB_ERROR("select() failed: %s", mxb_strerror(errno));
                    rv = CURLM_INTERNAL_ERROR;
                    break;

                case 0:
                default:
                    rv = curl_multi_perform(pCurlm, &still_running);
                    if (rv != CURLM_OK)
                    {
                        MXB_ERROR("curl_multi_perform() failed, error: %d, %s", (int)rv, curl_multi_strerror(rv));
                    }
                }
            }
            else
            {
                MXB_ERROR("curl_multi_fdset() failed, error: %d, %s", (int)rv, curl_multi_strerror(rv));
            }
        }
    }
    else
    {
        MXB_ERROR("curl_multi_perform() failed, error: %d, %s", (int)rv, curl_multi_strerror(rv));
    }

    if (rv == CURLM_OK)
    {
        int nRemaining = 0;
        do
        {
            CURLMsg* pMsg = curl_multi_info_read(pCurlm, &nRemaining);
            if (pMsg && (pMsg->msg == CURLMSG_DONE))
            {
                CURL* pCurl = pMsg->easy_handle;
                auto it = curls.find(pCurl);
                mxb_assert(it != curls.end());

                auto& context = it->second;
                Result* pResult = context.pResult;
                Errbuf* pErrbuf = context.pErrbuf;

                long code;
                curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &code);
                pResult->code = code;

                curls.erase(it);
                curl_multi_remove_handle(pCurlm, pCurl);
                curl_easy_cleanup(pCurl);
            }
        }
        while (nRemaining != 0);
    }
}

vector<Result> get_curlm(CURLM* pCurlm,
                         const std::vector<std::string>& urls,
                         const std::string& user, const std::string& password,
                         const Config& config)
{
    vector<Result> results;
    vector<array<char, CURL_ERROR_SIZE + 1>> errbufs;
    unordered_map<CURL*, Context> curls;

    results.reserve(urls.size());
    errbufs.reserve(urls.size());

    size_t i;
    for (i = 0; i < urls.size(); ++i)
    {
        results.resize(i + 1);
        errbufs.resize(i + 1);

        CURL* pCurl = get_easy_curl(urls[i], user, password, config, &results[i], errbufs[i].data());

        if (!pCurl || (curl_multi_add_handle(pCurlm, pCurl) != CURLM_OK))
        {
            mxb_assert(!true);
            if (pCurl)
            {
                curl_easy_cleanup(pCurl);
            }
            break;
        }
        else
        {
            curls.insert(std::make_pair(pCurl, Context(&results[i], &errbufs[i])));
        }
    }

    if (i == urls.size())
    {
        // Success
        execute(pCurlm, config, curls);
    }
    else
    {
        --i;
        mxb_assert(i == curls.size());

        for (auto& item : curls)
        {
            CURL* pCurl = item.first;
            MXB_AT_DEBUG(CURLMcode rv =) curl_multi_remove_handle(pCurlm, pCurl);
            mxb_assert(rv == CURLM_OK);
            curl_easy_cleanup(pCurl);
        }
    }

    return results;
}

}

vector<Result> get(const std::vector<std::string>& urls,
                   const std::string& user, const std::string& password,
                   const Config& config)
{
    vector<Result> results;
    results.reserve(urls.size());

    CURLM* pCurlm = curl_multi_init();
    mxb_assert(pCurlm);

    if (pCurlm)
    {
        results = get_curlm(pCurlm, urls, user, password, config);

        if (curl_multi_cleanup(pCurlm) != CURLM_OK)
        {
            mxb_assert(!true);
        }
    }

    return results;
}

}

}
