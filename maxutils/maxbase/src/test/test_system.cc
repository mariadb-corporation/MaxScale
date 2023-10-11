/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/system.hh>
#include <iostream>

using namespace std;

int main(int argc, char* argv[])
{
    string release = mxb::get_release_string();

    cout << release << endl;

    return !release.empty();
}
