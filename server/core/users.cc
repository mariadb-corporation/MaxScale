/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/ccdefs.hh>

#include <algorithm>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <unordered_map>

#include <maxbase/jansson.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/users.hh>
#include <maxscale/http.hh>
#include <maxscale/utils.hh>

namespace
{
constexpr char CN_CREATED[] = "created";
constexpr char CN_LAST_UPDATE[] = "last_update";
constexpr char CN_LAST_LOGIN[] = "last_login";

const char STR_BASIC[] = "basic";
const char STR_ADMIN[] = "admin";

// Generates SHA2-512 hashes
constexpr const char* ADMIN_SALT = "$6$MXS";

// Generates MD5 hashes, only used for authentication of old users
constexpr const char* OLD_ADMIN_SALT = "$1$MXS";

using Guard = std::lock_guard<std::mutex>;

json_t* date_or_null(time_t date)
{
    return date ? json_string(http_to_date(date).c_str()) : json_null();
}
}

namespace maxscale
{

Users::Users(const Users& rhs)
    : m_data(rhs.copy_contents())
{
}

Users& Users::operator=(const Users& rhs)
{
    // Get a copy of the rhs.data to avoid locking both mutexes simultaneously.
    auto rhs_data = rhs.copy_contents();
    Guard guard(m_lock);
    m_data = std::move(rhs_data);
    return *this;
}

Users::Users(Users&& rhs) noexcept
    : m_data(std::move(rhs.m_data))     // rhs should be a temporary, and no other thread can access it. No
                                        // lock.
{
}

Users& Users::operator=(Users&& rhs) noexcept
{
    Guard guard(m_lock);
    m_data = std::move(rhs.m_data);     // same as above
    return *this;
}

bool Users::add(const std::string& user, const std::string& password, user_account_type perm)
{
    time_t now = time(nullptr);
    return add_hashed(user, hash(password), perm, now, 0);
}

bool Users::remove(const std::string& user)
{
    Guard guard(m_lock);
    bool rval = false;

    auto it = m_data.find(user);
    if (it != m_data.end())
    {
        m_data.erase(it);
        rval = true;
    }

    return rval;
}

bool Users::get(const std::string& user, UserInfo* output) const
{
    Guard guard(m_lock);
    auto it = m_data.find(user);
    bool rval = false;

    if (it != m_data.end())
    {
        rval = true;
        if (output)
        {
            *output = it->second;
        }
    }

    return rval;
}

std::vector<UserInfo> Users::get_all() const
{
    std::vector<UserInfo> rval;
    Guard guard(m_lock);
    rval.reserve(m_data.size());

    for (const auto& [k, v] : m_data)
    {
        rval.push_back(v);
    }

    return rval;
}

user_account_type Users::authenticate(const std::string& user, const std::string& password)
{
    user_account_type rval = USER_ACCOUNT_UNKNOWN;
    Guard guard(m_lock);

    if (auto it = m_data.find(user); it != m_data.end())
    {
        const auto& stored = it->second.password;
        // The second character tell us which hashing function to use
        auto crypted = stored[1] == ADMIN_SALT[1] ? hash(password) : old_hash(password);

        if (stored == crypted)
        {
            it->second.last_login = time(nullptr);
            rval = it->second.permissions;
        }
    }

    return rval;
}

int Users::admin_count() const
{
    return std::count_if(m_data.begin(), m_data.end(), is_admin);
}

bool
Users::check_permissions(const std::string& user, const std::string& password, user_account_type perm) const
{
    Guard guard(m_lock);
    auto it = m_data.find(user);
    bool rval = false;

    if (it != m_data.end() && it->second.permissions == perm)
    {
        rval = true;
    }

    return rval;
}

bool Users::set_permissions(const std::string& user, user_account_type perm)
{
    Guard guard(m_lock);
    auto it = m_data.find(user);
    bool rval = false;

    if (it != m_data.end())
    {
        rval = true;
        it->second.permissions = perm;
    }

    return rval;
}

json_t* Users::diagnostics() const
{
    Guard guard(m_lock);
    json_t* rval = json_array();

    for (const auto& [key, val] : m_data)
    {
        json_array_append_new(rval, val.to_json(UserInfo::PUBLIC));
    }

    return rval;
}

bool Users::empty() const
{
    Guard guard(m_lock);
    return m_data.empty();
}

Users::UserMap Users::copy_contents() const
{
    Guard guard(m_lock);
    return m_data;
}

json_t* Users::to_json() const
{
    json_t* arr = json_array();
    Guard guard(m_lock);

    for (const auto& [key, val] : m_data)
    {
        json_array_append_new(arr, val.to_json(UserInfo::PRIVATE));
    }

    return arr;
}

bool Users::is_last_user(const std::string& user) const
{
    Guard guard(m_lock);
    return m_data.size() == 1 && m_data.find(user) != m_data.end();
}

bool Users::add_hashed(const std::string& user, const std::string& password, user_account_type perm,
                       time_t created, time_t updated)
{
    Guard guard(m_lock);
    return m_data.emplace(user, UserInfo(user, password, perm, created, updated)).second;
}

bool Users::is_admin(const std::unordered_map<std::string, UserInfo>::value_type& value)
{
    return value.second.permissions == USER_ACCOUNT_ADMIN;
}

bool Users::load_json(json_t* json)
{
    bool ok = true;
    // This function is always called in a single-threaded context
    size_t i;
    json_t* value;

    json_array_foreach(json, i, value)
    {
        json_t* name = json_object_get(value, CN_NAME);
        json_t* type = json_object_get(value, CN_ACCOUNT);
        json_t* password = json_object_get(value, CN_PASSWORD);
        json_t* created = json_object_get(value, CN_CREATED);
        json_t* updated = json_object_get(value, CN_LAST_UPDATE);

        if (name && json_is_string(name)
            && type && json_is_string(type)
            && password && json_is_string(password)
            && json_to_account_type(type) != USER_ACCOUNT_UNKNOWN)
        {
            time_t created_at = json_is_string(created) ? http_from_date(json_string_value(created)) : 0;
            time_t updated_at = json_is_string(updated) ? http_from_date(json_string_value(updated)) : 0;

            add_hashed(json_string_value(name),
                       json_string_value(password),
                       json_to_account_type(type),
                       created_at,
                       updated_at);
        }
        else
        {
            MXB_ERROR("Corrupt JSON value in users file: %s", mxb::json_dump(value).c_str());
            ok = false;
        }
    }

    return ok;
}

std::string Users::hash(const std::string& password)
{
    const int CACHE_MAX_SIZE = 1000;
    static std::unordered_map<std::string, std::string> hash_cache;
    auto it = hash_cache.find(password);

    if (it != hash_cache.end())
    {
        return it->second;
    }
    else
    {
        if (hash_cache.size() > CACHE_MAX_SIZE)
        {
            auto bucket = rand() % hash_cache.bucket_count();
            mxb_assert(bucket < hash_cache.bucket_count());
            hash_cache.erase(hash_cache.cbegin(bucket)->first);
        }

        auto new_hash = mxs::crypt(password, ADMIN_SALT);
        hash_cache.insert(std::make_pair(password, new_hash));
        return new_hash;
    }
}

std::string Users::old_hash(const std::string& password)
{
    return mxs::crypt(password, OLD_ADMIN_SALT);
}

bool Users::change_password(const char* user, const char* password)
{
    bool rval = false;
    Guard guard(m_lock);

    if (auto it = m_data.find(user); it != m_data.end())
    {
        rval = true;
        it->second.password = hash(password);
        it->second.last_update = time(nullptr);
    }

    return rval;
}
}

using mxs::Users;

bool users_is_admin(Users* users, const char* user, const char* password)
{
    return users->check_permissions(user, password ? password : "", mxs::USER_ACCOUNT_ADMIN);
}

const char* account_type_to_str(mxs::user_account_type type)
{
    switch (type)
    {
    case mxs::USER_ACCOUNT_BASIC:
        return STR_BASIC;

    case mxs::USER_ACCOUNT_ADMIN:
        return STR_ADMIN;

    default:
        return "unknown";
    }
}

mxs::user_account_type json_to_account_type(json_t* json)
{
    std::string str = json_string_value(json);

    if (str == STR_BASIC)
    {
        return mxs::USER_ACCOUNT_BASIC;
    }
    else if (str == STR_ADMIN)
    {
        return mxs::USER_ACCOUNT_ADMIN;
    }

    return mxs::USER_ACCOUNT_UNKNOWN;
}

json_t* mxs::UserInfo::to_json(Contents contents) const
{
    json_t* obj = json_object();

    json_object_set_new(obj, CN_NAME, json_string(name.c_str()));
    json_object_set_new(obj, CN_ACCOUNT, json_string(account_type_to_str(permissions)));

    if (contents == PRIVATE)
    {
        // For internal use only: include the password and don't add the timestamps. This way the users
        // are compare equal when compared as JSON.
        json_object_set_new(obj, CN_PASSWORD, json_string(password.c_str()));
    }
    else
    {
        json_object_set_new(obj, CN_CREATED, date_or_null(created));
        json_object_set_new(obj, CN_LAST_UPDATE, date_or_null(last_update));
        json_object_set_new(obj, CN_LAST_LOGIN, date_or_null(last_login));
    }

    return obj;
}
