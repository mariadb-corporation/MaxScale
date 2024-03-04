/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
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
    // Just for info.
    cout << "/etc/lsb-release: " << mxb::get_release_string(mxb::ReleaseSource::LSB_RELEASE) << endl;
    cout << "/etc/os-release : " << mxb::get_release_string(mxb::ReleaseSource::OS_RELEASE) << endl;

    string release = mxb::get_release_string();

    cout << "Any: " << release << endl;

    return release.empty() ? 1 : 0;
}
