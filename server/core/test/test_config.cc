/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#ifndef SS_DEBUG
#define SS_DEBUG
#endif

#include <iostream>
#include "../internal/config.hh"

using namespace std;
auto def_source_type = ConfigSection::SourceType::MAIN;

#define TEST(a) do {if (!(a)) {printf("Error: `" #a "` was not true\n"); return 1;}} while (false)

namespace
{

struct DISK_SPACE_THRESHOLD_RESULT
{
    const char* zPath;
    int32_t     size;
};

struct DISK_SPACE_THRESHOLD_TEST
{
    const char*                 zValue;
    bool                        valid;
    DISK_SPACE_THRESHOLD_RESULT results[5];
};

int dst_report(const DISK_SPACE_THRESHOLD_TEST& test,
               bool parsed,
               DiskSpaceLimits& result)
{
    int nErrors = 0;

    cout << test.zValue << endl;

    if (test.valid)
    {
        if (parsed)
        {
            const DISK_SPACE_THRESHOLD_RESULT* pResult = test.results;

            while (pResult->zPath)
            {
                auto i = result.find(pResult->zPath);

                if (i != result.end())
                {
                    result.erase(i);
                }
                else
                {
                    cout << "error: Expected " << pResult->zPath << " to be found, but it wasn't." << endl;
                    ++nErrors;
                }

                ++pResult;
            }

            if (result.size() != 0)
            {
                for (auto i = result.begin(); i != result.end(); ++i)
                {
                    cout << "error: " << i->first << " was found, although not expected." << endl;
                    ++nErrors;
                    ++i;
                }
            }
        }
        else
        {
            cout << "error: Expected value to be parsed, but it wasn't." << endl;
        }
    }
    else
    {
        if (parsed)
        {
            cout << "error: Expected value not to be parsed, but it was." << endl;
            ++nErrors;
        }
    }

    if (nErrors == 0)
    {
        cout << "OK, ";
        if (test.valid)
        {
            cout << "was valid and was parsed as such.";
        }
        else
        {
            cout << "was not valid, and was not parsed either.";
        }
        cout << endl;
    }

    return nErrors;
}
}

int test_disk_space_threshold()
{
    int nErrors = 0;

    static const DISK_SPACE_THRESHOLD_TEST tests[] =
    {
        {
            "/data:80", true,
            {
                {"/data",  80}
            }
        },
        {
            "/data1", false
        },
        {
            ":50", false
        },
        {
            "/data1:", false
        },
        {
            "/data1:abc", false
        },
        {
            "/data1:120", false
        },
        {
            "/data1:-50", false
        },
        {
            "/data1,/data2:50", false
        },
        {
            "/data1:50,/data2", false
        },
        {
            " /data1 : 40, /data2 :50, /data3: 70 ", true,
            {
                {"/data1", 40},
                {"/data2", 50},
                {"/data3", 70},
            }
        }
    };

    const int nTests = sizeof(tests) / sizeof(tests[0]);

    for (int i = 0; i < nTests; ++i)
    {
        const DISK_SPACE_THRESHOLD_TEST& test = tests[i];

        DiskSpaceLimits dst;

        bool parsed = config_parse_disk_space_threshold(&dst, test.zValue);

        nErrors += dst_report(test, parsed, dst);
    }

    return nErrors;
}

int main(int argc, char** argv)
{
    int result = 0;

    if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_FS))
    {
        result += test_disk_space_threshold();
        mxs_log_finish();
    }
    else
    {
        cerr << "Could not initialize log manager." << endl;
        ++result;
    }

    return result;
}
