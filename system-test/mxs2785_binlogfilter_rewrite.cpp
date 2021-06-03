#include <maxtest/testconnections.hh>
#include <sstream>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    auto slave = test.repl->get_connection(1);
    slave.connect();
    slave.query("STOP SLAVE");
    std::ostringstream ss;
    ss << "CHANGE MASTER TO MASTER_HOST='" << test.maxscale->ip()
       << "', MASTER_PORT=4008, MASTER_USE_GTID=slave_pos";
    slave.query(ss.str());

    auto master = test.repl->get_connection(0);
    master.connect();

    // Since the servers are configured to use ROW based replication, we only
    // use DDL statement to test. This makes sure they result in query events.
    master.query("CREATE DATABASE test_db1");
    master.query("CREATE TABLE test_db1.t1(id int)");
    master.query("USE test_db1");
    master.query("CREATE TABLE t2(id int)");

    master.query("CREATE DATABASE test_db2");
    master.query("CREATE TABLE test_db2.t1(id int)");
    master.query("USE test_db2");
    master.query("CREATE TABLE t2(id int)");

    master.query("CREATE DATABASE some_db");
    master.query("CREATE TABLE some_db.t1(id int)");
    master.query("USE some_db");
    master.query("CREATE TABLE t2(id int)");

    slave.query("START SLAVE");
    slave.query("SELECT MASTER_GTID_WAIT('" + master.field("SELECT @@last_gtid") + "', 120)");

    // The filter does s/test_[a-z0-9_]*/$1_rewritten/g
    test.expect(slave.query("SELECT * FROM test_db1_rewritten.t1 LIMIT 1"),
                "Query to test_db1_rewritten.t1 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM test_db1_rewritten.t2 LIMIT 1"),
                "Query to test_db1_rewritten.t2 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM test_db2_rewritten.t1 LIMIT 1"),
                "Query to test_db2_rewritten.t1 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM test_db2_rewritten.t2 LIMIT 1"),
                "Query to test_db2_rewritten.t2 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM some_db.t1 LIMIT 1"),
                "Query to some_db.t1 should work: %s", slave.error());
    test.expect(slave.query("SELECT * FROM some_db.t2 LIMIT 1"),
                "Query to some_db.t2 should work: %s", slave.error());

    master.query("DROP DATABASE test_db1");
    master.query("DROP DATABASE test_db2");
    master.query("DROP DATABASE some_db");

    slave.query("SELECT MASTER_GTID_WAIT('" + master.field("SELECT @@last_gtid") + "', 120)");

    return test.global_result;
}
