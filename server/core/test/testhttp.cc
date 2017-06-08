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

#include <iostream>

#include <maxscale/debug.h>
#include <maxscale/http.hh>

using std::string;
using std::cout;
using std::endl;

int main(int argc, char** argv)
{
    time_t now = time(NULL);
    string date = http_to_date(now);
    time_t converted_now = http_from_date(date);
    string converted_date = http_to_date(converted_now);

    cout << "Current linux time: " << now << endl;
    cout << "HTTP-date from current time: " << date << endl;
    cout << "Converted Linux time: " << converted_now << endl;
    cout << "Converted HTTP-date: " << converted_date << endl;

    ss_dassert(now == converted_now);
    ss_dassert(date == converted_date);

    return 0;
}
