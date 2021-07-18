#include <maxtest/testconnections.hh>
#include <string>

using std::string;
using mxt::MaxScale;

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
    MaxScale* maxscale {nullptr};
};

MonitorInfo monitors[] = {
    {MonitorID::ONE_A,   "MariaDB-Monitor1A"},
    {MonitorID::ONE_B,   "MariaDB-Monitor1B"},
    {MonitorID::TWO_A,   "MariaDB-Monitor2A"},
    {MonitorID::TWO_B,   "MariaDB-Monitor2B"},
    {MonitorID::UNKNOWN, "none",            },
};

const int failover_mon_ticks = 3;
const int mxs_switch_ticks = 3;
}

bool               monitor_is_primary(TestConnections& test, const MonitorInfo& mon_info);
const MonitorInfo* get_primary_monitor(TestConnections& test);

void test_failover(TestConnections& test, MaxScale& maxscale);
bool release_monitor_locks(TestConnections& test, const MonitorInfo& mon_info);
void test_main(TestConnections& test);

int main(int argc, char* argv[])
{
    TestConnections test;
    return test.run_test(argc, argv, test_main);
}

void test_main(TestConnections& test)
{
    test.expect(test.n_maxscales() >= 2, "At least 2 MaxScales are needed for this test. Exiting");
    if (!test.ok())
    {
        return;
    }

    auto& mxs1 = *test.maxscale;
    auto& mxs2 = *test.maxscale2;
    monitors[0].maxscale = &mxs1;
    monitors[1].maxscale = &mxs1;
    monitors[2].maxscale = &mxs2;
    monitors[3].maxscale = &mxs2;
    mxs1.wait_for_monitor(mxs_switch_ticks);
    mxs2.wait_for_monitor(mxs_switch_ticks);

    mxs1.check_print_servers_status(mxt::ServersInfo::default_repl_states());
    mxs2.check_print_servers_status(mxt::ServersInfo::default_repl_states());

    // Should have just one primary monitor.
    const auto* primary_mon1 = get_primary_monitor(test);
    if (test.ok())
    {
        // Test a normal failover.
        test_failover(test, *primary_mon1->maxscale);
    }

    // If ok so far, stop the MaxScale with the current primary monitor.
    if (test.ok())
    {
        auto* previous_primary_maxscale = primary_mon1->maxscale;
        test.tprintf("Stopping %s.", previous_primary_maxscale->node_name().c_str());
        previous_primary_maxscale->stop();
        MaxScale* expect_primary_maxscale = (previous_primary_maxscale == &mxs1) ? &mxs2 : &mxs1;
        // When swapping from one MaxScale to another, only waiting for monitor does not seem to be
        // 100% reliable. 1s sleep seems to ensure the switch has happened. A possible reason is that there
        // is some lag between a connection releasing a lock and that lock becoming available for other
        // connections to take.
        sleep(1);
        expect_primary_maxscale->wait_for_monitor(mxs_switch_ticks);
        const auto* primary_mon2 = get_primary_monitor(test);
        if (test.ok())
        {
            auto* current_primary_maxscale = primary_mon2->maxscale;
            test.expect(primary_mon2 != primary_mon1, "Primary monitor did not change.");
            test.expect(current_primary_maxscale == expect_primary_maxscale,
                        "Unexpected primary '%s'.", current_primary_maxscale->node_name().c_str());

            // Again, check that failover works. Wait a few more intervals since failover is not
            // immediately enabled on primary MaxScale switch.
            current_primary_maxscale->wait_for_monitor(failover_mon_ticks);
            test_failover(test, *current_primary_maxscale);
        }
        test.tprintf("Starting %s.", previous_primary_maxscale->node_name().c_str());
        previous_primary_maxscale->start();
        expect_primary_maxscale->wait_for_monitor(mxs_switch_ticks);
    }

    // If ok so far, do a rolling sweep through all four monitors by having each monitor release it's
    // locks in turn.
    if (test.ok())
    {
        const char revisited[] = "Revisited the same monitor";
        test.tprintf("Testing rolling monitor swapping.");
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
                    // The 'wait_for_monitor'-function causes the target monitor to tick faster than usual.
                    // This can cause issues when two separate MaxScales are involved, not leaving enough
                    // time for the next MaxScale to tick. Simply wait on both MaxScales.
                    sleep(1);
                    mxs1.wait_for_monitor(mxs_switch_ticks);
                    mxs2.wait_for_monitor(mxs_switch_ticks);
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
        test.tprintf("Test successful!");
    }
}

const MonitorInfo* get_primary_monitor(TestConnections& test)
{
    // Test each monitor in turn until find the one with lock majority. Also check that only one
    // monitor is primary.
    const MonitorInfo* rval = nullptr;
    int primaries = 0;
    for (int i = 0; monitors[i].maxscale; i++)
    {
        auto& mon_info = monitors[i];
        if (monitor_is_primary(test, mon_info))
        {
            primaries++;
            rval = &mon_info;
        }
    }
    test.expect(primaries == 1, "Found %i primary monitors when 1 was expected.", primaries);
    return rval;
}

bool monitor_is_primary(TestConnections& test, const MonitorInfo& mon_info)
{
    string cmd = "api get monitors/" + mon_info.name + " data.attributes.monitor_diagnostics.primary";
    auto res = mon_info.maxscale->maxctrl(cmd);
    auto& mxs_name = mon_info.maxscale->node_name();
    // If the MaxCtrl-command failed, assume it's because the target MaxScale machine is down.
    bool rval = false;
    if (res.rc == 0)
    {
        string& output = res.output;
        if (output == "true")
        {
            test.tprintf("%s from %s is the primary monitor.", mon_info.name.c_str(), mxs_name.c_str());
            rval = true;
        }
        else
        {
            test.expect(output == "false", "Unexpected result '%s' from %s",
                        output.c_str(), mxs_name.c_str());
        }
    }
    else
    {
        test.tprintf("MaxCtrl command failed, %s  is likely down.", mxs_name.c_str());
    }
    return rval;
}

bool release_monitor_locks(TestConnections& test, const MonitorInfo& mon_info)
{
    string cmd = "call command mariadbmon release-locks " + mon_info.name;
    auto res = mon_info.maxscale->maxctrl(cmd);
    bool success = res.rc == 0 && (res.output == "OK" || res.output == "\"OK\"");
    test.expect(success, "MaxCtrl command failed.");
    return success;
}

void test_failover(TestConnections& test, MaxScale& maxscale)
{
    // Test a normal failover.
    mxt::ServerInfo first_master = maxscale.get_servers().get_master();
    test.expect(first_master.server_id > 0, "No master at start of failover");
    if (test.ok())
    {
        test.tprintf("Stopping %s and waiting for failover.", first_master.name.c_str());
        int master_node = first_master.server_id - 1;
        test.repl->stop_node(master_node);
        maxscale.wait_for_monitor(failover_mon_ticks);
        mxt::ServerInfo second_master = maxscale.get_servers().get_master();
        test.expect(second_master.server_id > 0, "No master after failover");
        if (test.ok())
        {
            test.tprintf("%s is now master.", second_master.name.c_str());
            test.expect(first_master.server_id != second_master.server_id,
                        "Master did not change, failover did not happen.");
        }
        test.tprintf("Starting %s.", first_master.name.c_str());
        test.repl->start_node(master_node);
        maxscale.wait_for_monitor(failover_mon_ticks);      // wait for rejoin, assume it works
    }
}
