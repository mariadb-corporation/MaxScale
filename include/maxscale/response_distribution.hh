/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxbase/stopwatch.hh>

#include <map>

using namespace std::chrono_literals;

namespace maxscale
{

/**
 * Distribution of queries into buckets of response time, similar to
 * the Query Response Time Plugin in mariadb.
 * https://mariadb.com/kb/en/query-response-time-plugin/
 *
 * From Query Response Time Plugin documentation:
 * The user can define time intervals that divide the range 0 to positive infinity into smaller
 * intervals and then collect the number of commands whose execution times fall into each of
 * those intervals.
 * Each interval is described as:
 * (range_base ^ n; range_base ^ (n+1)]
 *
 * SELECT * FROM INFORMATION_SCHEMA.QUERY_RESPONSE_TIME;
 * +----------------+-------+----------------+
 * | TIME           | COUNT | TOTAL          |
 * +----------------+-------+----------------+
 * |       0.000001 |     0 |       0.000000 |
 * |       0.000010 |    17 |       0.000094 |
 * |       0.000100 |  4301         0.236555 |
 * |       0.001000 |  1499 |       0.824450 |
 * |       0.010000 | 14851 |      81.680502 |
 * |       0.100000 |  8066 |     443.635693 |
 * |       1.000000 |     0 |       0.000000 |
 * |      10.000000 |     0 |       0.000000 |
 * |     100.000000 |     1 |      55.937094 |
 * |    1000.000000 |     0 |       0.000000 |
 * |   10000.000000 |     0 |       0.000000 |
 * |  100000.000000 |     0 |       0.000000 |
 * | 1000000.000000 |     0 |       0.000000 |
 * | TOO LONG       |     0 | TOO LONG       |
 * +----------------+-------+----------------+
 *
 * This class tallies the response times added to it maintaining
 * a vector of the results.
 *
 * The limits are rounded to microseconds (a bit differently than the plugin).
 * The first limit is >= 1us, depends on the given range_base.
 * The last limit < 10'000'000 (1M for range_base=10, 11.6 days). In the server
 * the last limit is followed by a "TOO LONG" entry. There is no too-long entry
 * in class ResponseDistribution (not needed, can't convert to consistent json).
 */
class ResponseDistribution
{
public:
    /**
     * @brief ResponseDistribution
     * @param range_base - minimum 2
     */
    ResponseDistribution(int range_base = 10);

    struct Element
    {
        // These are all "atomic" sizes (64 bits).
        mxb::Duration limit;    // upper limit for a bucket
        int64_t       count;
        mxb::Duration total;
    };

    int range_base() const;

    void add(mxb::Duration dur);

    const std::vector<Element>& get() const;

    // Get an initial copy for summing up using operator+=
    ResponseDistribution with_stats_reset() const;

    ResponseDistribution& operator+(const ResponseDistribution& rhs);
    ResponseDistribution& operator+=(const ResponseDistribution& rhs);

private:
    int m_range_base;
    // initialized in the constructor after which
    // the underlying array (size) remains unchanged
    std::vector<Element> m_elements;
};

inline void ResponseDistribution::add(mxb::Duration dur)
{
    for (auto& element : m_elements)
    {
        if (dur <= element.limit)
        {
            ++element.count;
            element.total += dur;
            break;
        }
    }
}
}
