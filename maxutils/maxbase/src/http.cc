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
#include <curl/curl.h>
#include <maxbase/assert.h>

using std::map;
using std::string;

namespace
{

// TODO: Remove once trim is in maxbase.
// trim from beginning (in place)
inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

template<class T>
inline void checked_curl_setopt(CURL* pCurl, CURLoption option, T value)
{
    MXB_AT_DEBUG(CURLcode rv =) curl_easy_setopt(pCurl, option, value);
    mxb_assert(rv == CURLE_OK);
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
            trim(key);
            trim(value);
            pHeaders->insert(std::make_pair(key, value));
        }
    }

    return len;
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
    CURL* pCurl = curl_easy_init();

    checked_curl_setopt(pCurl, CURLOPT_NOSIGNAL, 1);
    checked_curl_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, config.connect_timeout); // For connection phase
    checked_curl_setopt(pCurl, CURLOPT_TIMEOUT, config.timeout);                // For data transfer phase
    checked_curl_setopt(pCurl, CURLOPT_ERRORBUFFER, errbuf);
    checked_curl_setopt(pCurl, CURLOPT_WRITEFUNCTION, write_callback);
    checked_curl_setopt(pCurl, CURLOPT_WRITEDATA, &res.body);
    checked_curl_setopt(pCurl, CURLOPT_URL, url.c_str());
    checked_curl_setopt(pCurl, CURLOPT_HEADERFUNCTION, header_callback);
    checked_curl_setopt(pCurl, CURLOPT_HEADERDATA, &res.headers);

    if (!user.empty() && !password.empty())
    {
        checked_curl_setopt(pCurl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        checked_curl_setopt(pCurl, CURLOPT_USERPWD, (user + ":" + password).c_str());
    }

    long code = 0;      // needs to be a long

    if (curl_easy_perform(pCurl) == CURLE_OK)
    {
        curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &code);
        res.code = code;
    }
    else
    {
        res.code = -1;
        res.body = errbuf;
    }

    curl_easy_cleanup(pCurl);

    return std::move(res);
}

}

}
