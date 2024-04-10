/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxbase/json.hh>

struct Counters
{
    Counters(int h, int m, int s, int u)
        : hits(h)
        , misses(m)
        , stale(s)
        , updates(u)
    {
    }

    bool operator==(const Counters& rhs)
    {
        return hits == rhs.hits && misses == rhs.misses && stale == rhs.stale && updates == rhs.updates;
    }

    int hits = -1;
    int misses = -1;
    int stale = -1;
    int updates = -1;
};

Counters get_counters(TestConnections& test)
{
    mxb::Json js;
    auto res = test.maxctrl("api get services/Sharding-Router "
                            "data.attributes.router_diagnostics");
    MXT_EXPECT(js.load_string(res.output));

    return Counters(js.get_int("shard_map_hits"),
                    js.get_int("shard_map_misses"),
                    js.get_int("shard_map_stale"),
                    js.get_int("shard_map_updates"));
}

bool compare_counters(TestConnections& test, const Counters& expected)
{
    auto result = get_counters(test);

    test.expect(result.hits == expected.hits,
                "Expected %d hits, got %d", expected.hits, result.hits);
    test.expect(result.misses == expected.misses,
                "Expected %d misses, got %d", expected.misses, result.misses);
    test.expect(result.stale == expected.stale,
                "Expected %d stale, got %d", expected.stale, result.stale);
    test.expect(result.updates == expected.updates,
                "Expected %d updates, got %d", expected.updates, result.updates);

    return test.ok();
}

void one_session(TestConnections& test)
{
    auto c = test.maxscale->rwsplit();
    MXT_EXPECT(c.connect());
    MXT_EXPECT(c.query("SELECT 1"));
    c.disconnect();
}

void test_main(TestConnections& test)
{
    Counters expected(0, 0, 0, 0);
    // All counters should start at zero
    MXT_EXPECT(compare_counters(test, expected));

    // We should get an update
    expected.misses++;
    expected.updates++;
    one_session(test);
    MXT_EXPECT(compare_counters(test, expected));

    // A second connection should hit the cache
    expected.hits++;
    one_session(test);
    MXT_EXPECT(compare_counters(test, expected));

    test.check_maxctrl("alter service Sharding-Router refresh_interval=2s");

    // Wait long enough to make all entries stale
    sleep(3);

    expected.stale++;
    expected.updates++;
    one_session(test);
    MXT_EXPECT(compare_counters(test, expected));

    // Second connection should not be stale
    expected.hits++;
    one_session(test);
    MXT_EXPECT(compare_counters(test, expected));

    test.check_maxctrl("alter service Sharding-Router refresh_interval=2000s");

    // Should be in the cache
    expected.hits++;
    one_session(test);
    MXT_EXPECT(compare_counters(test, expected));

    test.check_maxctrl("call command schemarouter clear Sharding-Router");

    // Should cause a miss
    expected.misses++;
    expected.updates++;
    one_session(test);
    MXT_EXPECT(compare_counters(test, expected));

    test.check_maxctrl("call command schemarouter invalidate Sharding-Router");

    // Should hit a stale shard
    expected.stale++;
    expected.updates++;
    one_session(test);
    MXT_EXPECT(compare_counters(test, expected));
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
