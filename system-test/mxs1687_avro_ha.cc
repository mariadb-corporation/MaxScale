#include <maxtest/testconnections.hh>
#include <maxbase/string.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto get_gtid = [&](std::string name) {
            auto rv = test.maxscales->ssh_output(
                "cat /var/lib/maxscale/" + name + "/current_gtid.txt 2>/dev/null");
            return mxb::trimmed_copy(rv.output);
        };

    auto conn = test.repl->get_connection(0);
    conn.connect();

    conn.query("CREATE TABLE test.t1(id INT)");
    conn.query("INSERT INTO test.t1 VALUES (1)");

    // Stop B-Monitor, A-Monitor will take ownership of the cluster
    test.maxctrl("stop monitor B-Monitor");
    sleep(5);
    test.maxctrl("start monitor B-Monitor");

    conn.query("INSERT INTO test.t1 VALUES (1)");
    sleep(5);

    // A-avro should be at the same position as the master. B-avro should not be replicating.
    auto master = conn.field("SELECT @@gtid_current_pos");
    auto a = get_gtid("A-avro");
    auto b = get_gtid("B-avro");
    test.expect(a == master, "Expected A-avro to be at '%s', not at '%s'", master.c_str(), a.c_str());
    test.expect(b != master, "Expected B-avro to not be at '%s'", master.c_str());

    // Stash the current GTID position to make sure that the stopped instance doesn't start replicating.
    auto old_master = master;

    test.maxctrl("stop monitor A-Monitor");
    sleep(5);
    test.maxctrl("start monitor A-Monitor");

    conn.query("INSERT INTO test.t1 VALUES (2)");
    sleep(5);

    // B-avro should be at the same position as the master. A-avro should not be replicating.
    master = conn.field("SELECT @@gtid_current_pos");
    a = get_gtid("A-avro");
    b = get_gtid("B-avro");
    test.expect(b == master, "Expected B-avro to be at '%s', not at '%s'", master.c_str(), b.c_str());
    test.expect(a != master, "Expected A-avro to not be at '%s'", master.c_str());
    test.expect(a == old_master, "Expected A-avro to be at '%s', not at '%s'", old_master.c_str(), a.c_str());

    auto older_master = old_master;
    old_master = master;

    // Stop B-Monitor, A-Monitor will take ownership of the cluster
    test.maxctrl("stop monitor B-Monitor");
    test.maxctrl("stop monitor A-Monitor");
    sleep(5);

    conn.query("INSERT INTO test.t1 VALUES (3)");
    sleep(5);

    // Neither should advance when both monitors are stopped.
    master = conn.field("SELECT @@gtid_current_pos");
    a = get_gtid("A-avro");
    b = get_gtid("B-avro");
    test.expect(a != master, "Expected B-avro to not be at '%s'", master.c_str());
    test.expect(b != master, "Expected A-avro to not be at '%s'", master.c_str());
    test.expect(a == older_master,
                "Expected A-avro to be at '%s', not at '%s'", older_master.c_str(), a.c_str());
    test.expect(b == old_master,
                "Expected B-avro to be at '%s', not at '%s'", old_master.c_str(), a.c_str());



    conn.query("DROP TABLE test.t1");

    return test.global_result;
}
