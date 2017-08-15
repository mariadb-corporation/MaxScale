/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include <new>
#include <tr1/unordered_map>
#include <string>

#include <maxscale/users.h>
#include <maxscale/authenticator.h>
#include <maxscale/spinlock.hh>
#include <maxscale/log_manager.h>
#include <maxscale/jansson.hh>

namespace
{

static const char STR_BASIC[] = "basic";
static const char STR_ADMIN[] = "admin";

static const char* account_type_to_str(account_type type)
{
    switch (type)
    {
    case ACCOUNT_BASIC:
        return STR_BASIC;

    case ACCOUNT_ADMIN:
        return STR_ADMIN;

    default:
        MXS_ERROR("Unknown enum account_type value: %d", (int)type);
        ss_dassert(!true);
        return "unknown";
    }
}
static account_type json_to_account_type(json_t* json)
{
    std::string str = json_string_value(json);

    if (str == STR_BASIC)
    {
        return ACCOUNT_BASIC;
    }
    else if (str == STR_ADMIN)
    {
        return ACCOUNT_ADMIN;
    }

    MXS_ERROR("Unknown account type string: %s", str.c_str());
    ss_dassert(!true);
    return ACCOUNT_UNKNOWN;
}
struct UserInfo
{
    UserInfo():
        permissions(ACCOUNT_BASIC)
    {
    }

    UserInfo(std::string pw, account_type perm):
        password(pw),
        permissions(perm)
    {
    }

    std::string  password;
    account_type permissions;
};


class Users
{
    Users(const Users&);
    Users& operator=(const Users&);

public:
    typedef std::tr1::unordered_map<std::string, UserInfo> UserMap;

    Users()
    {
    }

    ~Users()
    {
    }

    bool add(std::string user, std::string password, account_type perm)
    {
        mxs::SpinLockGuard guard(m_lock);
        return m_data.insert(std::make_pair(user, UserInfo(password, perm))).second;
    }

    bool remove(std::string user)
    {
        mxs::SpinLockGuard guard(m_lock);
        bool rval = false;
        UserMap::iterator it = m_data.find(user);

        if (it != m_data.end())
        {
            m_data.erase(it);
            rval = true;
        }

        return rval;
    }

    bool get(std::string user, UserInfo* output = NULL) const
    {
        mxs::SpinLockGuard guard(m_lock);
        UserMap::const_iterator it = m_data.find(user);
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

    bool check_permissions(std::string user, account_type perm) const
    {
        mxs::SpinLockGuard guard(m_lock);
        UserMap::const_iterator it = m_data.find(user);
        bool rval = false;

        if (it != m_data.end() && it->second.permissions == perm)
        {
            rval = true;
        }

        return rval;
    }

    bool set_permissions(std::string user, account_type perm)
    {
        mxs::SpinLockGuard guard(m_lock);
        UserMap::iterator it = m_data.find(user);
        bool rval = false;

        if (it != m_data.end())
        {
            rval = true;
            it->second.permissions = perm;
        }

        return rval;
    }

    json_t* diagnostic_json() const
    {
        mxs::SpinLockGuard guard(m_lock);
        json_t* rval = json_array();

        for (UserMap::const_iterator it = m_data.begin(); it != m_data.end(); it++)
        {
            json_array_append_new(rval, json_string(it->first.c_str()));
        }

        return rval;
    }

    void diagnostic(DCB* dcb) const
    {
        mxs::SpinLockGuard guard(m_lock);
        if (m_data.size())
        {
            const char *sep = "";

            for (UserMap::const_iterator it = m_data.begin(); it != m_data.end(); it++)
            {
                dcb_printf(dcb, "%s%s", sep, it->first.c_str());
                sep = ", ";
            }
            dcb_printf(dcb, "\n");
        }
    }

    bool empty() const
    {
        mxs::SpinLockGuard guard(m_lock);
        return m_data.size() > 0;
    }

    json_t* to_json() const
    {
        json_t* arr = json_array();
        mxs::SpinLockGuard guard(m_lock);

        for (UserMap::const_iterator it = m_data.begin(); it != m_data.end(); it++)
        {
            json_t* obj = json_object();
            json_object_set_new(obj, CN_NAME, json_string(it->first.c_str()));
            json_object_set_new(obj, CN_TYPE, json_string(account_type_to_str(it->second.permissions)));
            json_object_set_new(obj, CN_PASSWORD, json_string(it->second.password.c_str()));
            json_array_append_new(arr, obj);
        }

        return arr;
    }

    static Users* from_json(json_t* json)
    {
        Users* u = reinterpret_cast<Users*>(users_alloc());
        u->load_json(json);
        return u;
    }

private:
    void load_json(json_t* json)
    {
        // This function is always called in a single-threaded context
        size_t i;
        json_t* value;

        json_array_foreach(json, i, value)
        {
            json_t* name = json_object_get(value, CN_NAME);
            json_t* type = json_object_get(value, CN_TYPE);
            json_t* password = json_object_get(value, CN_PASSWORD);

            if (name && json_is_string(name) &&
                type && json_is_string(type) &&
                password && json_is_string(password) &&
                json_to_account_type(type) != ACCOUNT_UNKNOWN)
            {
                add(json_string_value(name), json_string_value(password),
                    json_to_account_type(type));
            }
            else
            {
                MXS_ERROR("Corrupt JSON value in users file: %s", mxs::json_dump(value).c_str());
            }
        }
    }

    mxs::SpinLock m_lock;
    UserMap       m_data;

};

}

USERS *users_alloc()
{
    Users* rval = new (std::nothrow) Users();
    MXS_OOM_IFNULL(rval);
    return reinterpret_cast<USERS*>(rval);
}

void users_free(USERS *users)
{
    Users* u = reinterpret_cast<Users*>(users);
    delete u;
}

bool users_add(USERS *users, const char *user, const char *password, enum account_type type)
{
    Users* u = reinterpret_cast<Users*>(users);
    return u->add(user, password, type);
}

bool users_delete(USERS *users, const char *user)
{
    Users* u = reinterpret_cast<Users*>(users);
    return u->remove(user);
}

json_t* users_to_json(USERS *users)
{
    Users* u = reinterpret_cast<Users*>(users);
    return u->to_json();
}

USERS* users_from_json(json_t* json)
{
    return reinterpret_cast<USERS*>(Users::from_json(json));
}

bool users_find(USERS* users, const char* user)
{
    Users* u = reinterpret_cast<Users*>(users);
    return u->get(user);
}

bool users_auth(USERS* users, const char* user, const char* password)
{
    Users* u = reinterpret_cast<Users*>(users);
    bool rval = false;
    UserInfo info;

    if (u->get(user, &info))
    {
        rval = strcmp(password, info.password.c_str()) == 0;
    }

    return rval;
}

bool users_is_admin(USERS* users, const char* user)
{
    Users* u = reinterpret_cast<Users*>(users);
    return u->check_permissions(user, ACCOUNT_ADMIN);
}

bool users_promote(USERS* users, const char* user)
{
    Users* u = reinterpret_cast<Users*>(users);
    return u->set_permissions(user, ACCOUNT_ADMIN);
}

bool users_demote(USERS* users, const char* user)
{
    Users* u = reinterpret_cast<Users*>(users);
    return u->set_permissions(user, ACCOUNT_BASIC);
}

void users_diagnostic(DCB* dcb, USERS* users)
{
    Users* u = reinterpret_cast<Users*>(users);
    u->diagnostic(dcb);
}

json_t* users_diagnostic_json(USERS* users)
{
    Users* u = reinterpret_cast<Users*>(users);
    return u->diagnostic_json();
}

void users_default_diagnostic(DCB* dcb, SERV_LISTENER* port)
{
    if (port->users)
    {
        Users* u = reinterpret_cast<Users*>(port->users);

        if (u->empty())
        {
            dcb_printf(dcb, "Users table is empty\n");
        }
        else
        {
            dcb_printf(dcb, "User names: ");
            users_diagnostic(dcb, port->users);
        }
    }
}

json_t* users_default_diagnostic_json(const SERV_LISTENER *port)
{
    return port->users ? users_diagnostic_json(port->users) : json_array();
}

int users_default_loadusers(SERV_LISTENER *port)
{
    users_free(port->users);
    port->users = users_alloc();
    return MXS_AUTH_LOADUSERS_OK;
}
