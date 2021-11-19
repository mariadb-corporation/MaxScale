#include <maxtest/testconnections.hh>
#include <fstream>

int main(int argc, char* argv[])
{
    TestConnections test(argc, argv);

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connection failed: %s", conn.error());

    test.expect(conn.query("CREATE OR REPLACE TABLE t1(id INT)"), "Failed to create table: %s", conn.error());

    std::ofstream ofile("data.csv");

    for (int i = 0; i < 1000; i++)
    {
        ofile << i << '\n';
    }

    ofile.close();

    test.expect(conn.query("BEGIN"), "BEGIN failed: %s", conn.error());
    test.expect(conn.query("LOAD DATA LOCAL INFILE 'data.csv' INTO TABLE t1"),
                "LOAD DATA failed: %s", conn.error());
    test.expect(conn.query("COMMIT"), "COMMIT failed: %s", conn.error());
    test.expect(conn.query("DROP TABLE t1"), "DROP failed: %s", conn.error());

    remove("data.csv");

    return test.global_result;
}
