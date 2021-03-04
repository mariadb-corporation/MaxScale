
#include <string>
#include <maxtest/testconnections.hh>
#include "failover_common.cpp"

using std::string;

namespace
{
// The test runs two MaxScales with two monitors each.
enum class MonitorID
{
    UNKNOWN,
    ONE_A,
    ONE_B,
    TWO_A,
    TWO_B,
};

struct MonitorInfo
{
    MonitorID id {MonitorID::UNKNOWN};
    string    name;
    int       maxscale_ind {-1};
};

MonitorInfo monitors[] = {
    {MonitorID::ONE_A,   "MariaDB-Monitor1A", 0 },
    {MonitorID::ONE_B,   "MariaDB-Monitor1B", 0 },
    {MonitorID::TWO_A,   "MariaDB-Monitor2A", 1 },
    {MonitorID::TWO_B,   "MariaDB-Monitor2B", 1 },
    {MonitorID::UNKNOWN, "none",              -1},
};

const int failover_mon_ticks = 3;
const int maxscale_switch_mon_ticks = 2;
}

bool               monitor_is_primary(TestConnections& test, const MonitorInfo& mon_info);
const MonitorInfo* get_primary_monitor(TestConnections& test);

void test_failover(TestConnections& test, int maxscale_ind);
bool release_monitor_locks(TestConnections& test, const MonitorInfo& mon_info);

int main(int argc, char* argv[])
{
    TestConnections::multiple_maxscales(true);
    MariaDBCluster::require_gtid(true);
    TestConnections test(argc, argv);

    const int N_maxscales = test.maxscales->N;
    test.expect(N_maxscales >= 2, "%s", "At least 2 MaxScales are needed for this test. Exiting");
    if (!test.ok())
    {
        return test.global_result;
    }

    test.maxscales->wait_for_monitor(2, 0);
    test.maxscales->wait_for_monitor(2, 1);

    // Should have just one primary monitor.
    const auto* primary_mon1 = get_primary_monitor(test);
    if (test.ok())
    {
        // Test a normal failover.
        test_failover(test, primary_mon1->maxscale_ind);
    }

    // If ok so far, stop the MaxScale with the current primary monitor.
    if (test.ok())
    {
        int previous_primary_maxscale = primary_mon1->maxscale_ind;
        cout << "Stopping MaxScale " << previous_primary_maxscale << ".\n";
        test.maxscales->stop_maxscale(previous_primary_maxscale);
        int expect_primary_maxscale = (previous_primary_maxscale == 0) ? 1 : 0;
        // The switch to another primary MaxScale should be quick.
        test.maxscales->wait_for_monitor(maxscale_switch_mon_ticks, expect_primary_maxscale);
        const auto* primary_mon2 = get_primary_monitor(test);
        if (test.ok())
        {
            int current_primary_maxscale = primary_mon2->maxscale_ind;
            test.expect(primary_mon2 != primary_mon1, "Primary monitor did not change.");
            test.expect(current_primary_maxscale == expect_primary_maxscale,
                        "Unexpected primary MaxScale %i.", current_primary_maxscale);

            // Again, check that failover works. Wait a few more intervals since failover is not
            // immediately enabled on primary MaxScale switch.
            test.maxscales->wait_for_monitor(failover_mon_ticks, current_primary_maxscale);
            test_failover(test, current_primary_maxscale);
        }
        cout << "Starting MaxScale" << previous_primary_maxscale << ".\n";
        test.maxscales->start_maxscale(previous_primary_maxscale);
        test.maxscales->wait_for_monitor(maxscale_switch_mon_ticks, expect_primary_maxscale);
    }

    // If ok so far, do a rolling sweep through all four monitors by having each monitor release it's
    // locks in turn.
    if (test.ok())
    {
        const char revisited[] = "Revisited the same monitor";
        cout << "Testing rolling monitor swapping.\n";
        std::set<MonitorID> visited_monitors;
        while (visited_monitors.size() < 3 && test.ok())
        {
            const auto* primary_mon = get_primary_monitor(test);
            if (test.ok())
            {
                auto mon_id = primary_mon->id;
                test.expect(visited_monitors.count(mon_id) == 0, revisited);
                bool released = release_monitor_locks(test, *primary_mon);
                test.expect(released, "Releasing monitor locks failed");
                if (released)
                {
                    visited_monitors.insert(mon_id);
                    test.maxscales->wait_for_monitor(maxscale_switch_mon_ticks, primary_mon->maxscale_ind);
                }
            }
        }
        // Should have one monitor left.
        const auto* primary_mon = get_primary_monitor(test);
        if (test.ok())
        {
            test.expect(visited_monitors.count(primary_mon->id) == 0, revisited);
        }
    }

    if (test.ok())
    {
        cout << "Test successful\n";
    }
    return test.global_result;
}

const MonitorInfo* get_primary_monitor(TestConnections& test)
{
    // Test each monitor in turn until find the one with lock majority. Also check that only one
    // monitor is primary.
    const MonitorInfo* rval = nullptr;
    int primaries = 0;
    for (int i = 0; monitors[i].maxscale_ind >= 0; i++)
    {
        auto& mon_info = monitors[i];
        if (monitor_is_primary(test, mon_info))
        {
            primaries++;
            rval = &mon_info;
        }
    }
    test.expect(primaries == 1, "Unexpected number of primary monitors: %i", primaries);
    return rval;
}

bool monitor_is_primary(TestConnections& test, const MonitorInfo& mon_info)
{
    string cmd = "api get monitors/" + mon_info.name
        + " data.attributes.monitor_diagnostics.primary";
    int maxscale_ind = mon_info.maxscale_ind;
    auto res = test.maxctrl(cmd, maxscale_ind);
    // If the MaxCtrl-command failed, assume it's because the target MaxScale machine is down.
    bool rval = false;
    if (res.rc == 0)
    {
        string& output = res.output;
        if (output == "true")
        {
            cout << mon_info.name << " from MaxScale " << maxscale_ind << " is the primary monitor.\n";
            rval = true;
        }
        else
        {
            test.expect(output == "false", "Unexpected result '%s' from MaxScale %i",
                        output.c_str(), maxscale_ind);
        }
    }
    else
    {
        cout << "MaxCtrl command failed, MaxScale " << maxscale_ind << " is likely down.\n";
    }
    return rval;
}

bool release_monitor_locks(TestConnections& test, const MonitorInfo& mon_info)
{
    string cmd = "call command mariadbmon release-locks " + mon_info.name;
    auto res = test.maxctrl(cmd, mon_info.maxscale_ind);
    bool success = res.rc == 0 && res.output == "OK";
    test.expect(success, "MaxCtrl command failed.");
    return success;
}

void test_failover(TestConnections& test, int maxscale_ind)
{
    // Test a normal failover.
    int first_master_id = get_master_server_id(test, maxscale_ind);
    test.expect(first_master_id > 0, "No master at start of failover");
    if (test.ok())
    {
        cout << "Stopping server" << first_master_id << " and waiting for failover.\n";
        int master_node = first_master_id - 1;
        test.repl->stop_node(master_node);
        test.maxscales->wait_for_monitor(failover_mon_ticks, maxscale_ind);
        int second_master_id = get_master_server_id(test, maxscale_ind);
        test.expect(second_master_id > 0, "No master after failover");
        if (test.ok())
        {
            cout << "Server" << second_master_id << " is now master.\n";
            test.expect(first_master_id != second_master_id,
                        "Master did not change, failover did not happen.");
        }
        cout << "Starting server" << first_master_id << ".\n";
        test.repl->start_node(master_node);
        test.maxscales->wait_for_monitor(2, maxscale_ind);      // wait for rejoin, assume it works
    }
}
