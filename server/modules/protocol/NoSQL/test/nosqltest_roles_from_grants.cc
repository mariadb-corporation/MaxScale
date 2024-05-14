/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlusermanager.hh"
#include <iostream>
#include <vector>
#include <maxbase/log.hh>
#include <maxbase/string.hh>

using namespace nosql;
using namespace std;

namespace nosql
{
namespace role
{
std::ostream& operator << (ostream& out, const Role& role)
{
    out << "{'" << role.db << "', " << nosql::role::to_string(role.id) << " }";
    return out;
}
}
}

namespace
{

struct TestCase
{
    bool               should_succeed;
    string             user;
    vector<string>     grants;
    vector<role::Role> roles;
};

TestCase test_cases[] =
{
    {
        true,
        "bob",
        {
            "GRANT ALL PRIVILEGES ON `db`.* TO `bob`@`%` IDENTIFIED BY PASSWORD 'bob' WITH GRANT OPTION",
        },
        {
            { "db", role::DB_ADMIN },
            { "db", role::READ_WRITE },
            { "db", role::USER_ADMIN },
            { "db", role::DB_OWNER }
        }
    },
    {
        true,
        "admin.bob",
        {
            "GRANT ALL PRIVILEGES ON *.* TO `admin.bob`@`%` IDENTIFIED BY PASSWORD 'bob' WITH GRANT OPTION",
        },
        {
            { "admin", role::DB_ADMIN_ANY_DATABASE },
            { "admin", role::READ_WRITE_ANY_DATABASE },
            { "admin", role::USER_ADMIN_ANY_DATABASE },
            { "admin", role::ROOT }
        }
    },
    {
        true,
        "bob",
        {
            "GRANT SELECT ON `db`.* TO `bob`@`%` IDENTIFIED BY PASSWORD 'bob'",
        },
        {
            { "db", role::READ },
        }
    },
    {
        true,
        "bob",
        {
            "GRANT SELECT ON `dbA`.* TO `bob`@`%` IDENTIFIED BY PASSWORD 'bob'",
            "GRANT CREATE, DELETE, INDEX, INSERT, SELECT, UPDATE ON `dbB`.* TO `bob`@`%` IDENTIFIED BY PASSWORD 'bob'",
        },
        {
            { "dbA", role::READ },
            { "dbB", role::READ_WRITE },
        }
    },
    {
        true,
        "bob",
        {
            "GRANT USAGE ON *.* TO `dbA.xyz`@`%` IDENTIFIED BY PASSWORD '*975B2CD4FF9AE554FE8AD33168FBFC326D2021DD'",
            "GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, INDEX ON `dbB`.* TO `dbA.xyz`@`%`",
            "GRANT SELECT ON `dbA`.* TO `dbA.xyz`@`%`"
        },
        {
            { "dbB", role::READ_WRITE },
            { "dbA", role::READ },
        }
    },
    {
        false, // You can't grant anything on *.* to a regular user.
        "bob",
        {
            "GRANT ALL PRIVILEGES ON *.* TO 'bob'@'%'"
        },
        {
        }
    },
    {
        false, // You can't grant anything on db.* to an admin user.
        "admin.bob",
        {
            "GRANT ALL PRIVILEGES ON `dbA`.* TO 'admin.bob'@'%'"
        },
        {
        }
    }
};

const int nTest_cases = sizeof(test_cases) / sizeof(test_cases[0]);

bool is_admin(string user)
{
    bool rv = false;

    auto i = user.find(".");

    if (i != string::npos)
    {
        auto head = user.substr(0, i);

        rv = head == "admin";
    }

    return rv;
}

int test(const TestCase& tc)
{
    int rv = 0;

    bool is_admin = ::is_admin(tc.user);

    vector<role::Role> roles;
    for (auto grant : tc.grants)
    {
        set<string> priv_types;
        string on;
        bool with_grant_option;

        if (role::get_grant_characteristics(grant, &priv_types, &on, &with_grant_option))
        {
            vector<role::Role> some_roles;
            if (role::from_grant(is_admin, priv_types, on, with_grant_option, &some_roles))
            {
                roles.insert(roles.end(),
                             std::move_iterator(some_roles.begin()), std::move_iterator(some_roles.end()));
            }
            else
            {
                if (tc.should_succeed)
                {
                    cerr << "Could not get roles of: " << grant << endl;
                }

                ++rv;
            }
        }
        else if (tc.should_succeed)
        {
            cerr << "Could not get grant characteristics: " << grant << endl;
            ++rv;
        }
    }

    if (rv == 0)
    {
        bool equal = (roles == tc.roles);

        if (equal && tc.should_succeed)
        {
            cout << "SUCCESS" << endl;
        }
        else
        {
            cout << mxb::join(tc.grants, ", ", "'") << endl;
            cout << "Expected: " << mxb::join(tc.roles, ", ") << endl;
            cout << "Got     : " << mxb::join(roles, ", ") << endl;

            cout << "EXPECTED FAILURE" << endl;
            ++rv;
        }
    }
    else if (tc.should_succeed)
    {
        cout << mxb::join(tc.grants, ", ", "'") << endl;
        cout << "Expected: " << mxb::join(tc.roles, ", ") << endl;
        cout << "Got     : " << mxb::join(roles, ", ") << endl;

        cout << "EXPECTED SUCCESS" << endl;
        ++rv;
    }
    else
    {
        rv = 0;
    }

    return rv;
}

}



int main()
{
    int rv = 0;

    mxb::Log log;

    for (int i = 0; i < nTest_cases; ++i)
    {
        const TestCase& tc = test_cases[i];

        rv += test(tc);
    }

    return rv;
}
