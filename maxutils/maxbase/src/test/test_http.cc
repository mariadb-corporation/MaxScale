/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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
#include <iostream>
#include <maxbase/log.hh>

using namespace std;

namespace
{

int test_http()
{
    int rv = EXIT_FAILURE;

    auto res = mxb::http::get("http://www.example.com/");
    cout << "http://www.example.com/ responded with: " << res.code << endl;
    if (res.code == 200)
    {
        if (res.headers.count("Date"))
        {
            cout << "The date is: " << res.headers["Date"] << endl;
            rv = EXIT_SUCCESS;
        }
    }
    else
    {
        cout << "error: Exit code not 200 but: " << res.code << endl;
    }

    return rv;
}

}

int main()
{
    int rv = EXIT_SUCCESS;
    mxb::Log log;

    rv = test_http();

    return rv;
}
