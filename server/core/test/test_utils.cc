/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/assert.h>
#include <maxscale/utils.h>
#include <maxscale/utils.hh>
#include <string.h>
#include <iostream>

using std::cout;
using std::endl;

namespace
{

template<typename T>
int test_checksums()
{
    uint8_t data[] =
    {
        'h', 'e', 'l', 'l', 'o', ' ', 'w', 'o', 'r', 'l', 'd', '!'
    };

    GWBUF* d1 = gwbuf_alloc_and_load(sizeof(data), data);
    GWBUF* d2 = gwbuf_alloc_and_load(sizeof(data), data);

    T sum1, sum2;
    sum1.update(d1);
    sum1.finalize();
    sum2.finalize(d1);
    mxb_assert(sum1 == sum2);
    sum1.reset();
    sum2.reset();

    // Check that the hex strings match
    mxb_assert(sum1.hex() == sum2.hex());

    std::string saved = sum1.hex();

    // The checksum must not be empty
    mxb_assert(!saved.empty());

    // Repeat the same test, should produce the same checksums
    sum1.update(d1);
    sum1.finalize();
    sum2.finalize(d1);
    mxb_assert(sum1 == sum2);
    mxb_assert(sum1.hex() == saved);
    mxb_assert(sum2.hex() == saved);
    sum1.reset();
    sum2.reset();

    // Check that different buffers but same content produce the same checksum
    sum1.finalize(d2);
    sum2.finalize(d1);
    mxb_assert(sum1 == sum2);
    mxb_assert(sum1.hex() == saved);
    mxb_assert(sum2.hex() == saved);

    gwbuf_free(d1);
    gwbuf_free(d2);

    return 0;
}

}

int main(int argc, char* argv[])
{
    int rv = 0;

    rv += test_checksums<mxs::SHA1Checksum>();
    rv += test_checksums<mxs::CRC32Checksum>();

    return rv;
}
