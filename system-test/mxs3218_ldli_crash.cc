/**
 * MXS-3218: Crash with LOAD DATA LOCAL INFILE
 *
 * The protocol parsed the data during the LOAD DATA LOCAL INFILE and confused it
 * with a `USE <database>` query.
 */

#include <maxtest/testconnections.hh>


// The payload must have a leading space so that it is interpreted as the command byte
const char data[] = " USE test";

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    char filename[1024] = "/tmp/mxs3218.XXXXXX";
    int fd = mkstemp(filename);
    write(fd, data, sizeof(data) - 1);

    auto conn = test.maxscale->rwsplit();
    test.expect(conn.connect(), "Connect failed: %s", conn.error());

    test.expect(conn.query("CREATE OR REPLACE TABLE test.t1(id INT)"),
                "CREATE failed: %s", conn.error());
    test.expect(conn.query("LOAD DATA LOCAL INFILE '" + std::string(filename) + "' INTO TABLE test.t1"),
                "LOAD DATA LOCAL INFILE failed: %s", conn.error());
    test.expect(conn.query("DROP TABLE test.t1"),
                "DROP failed: %s", conn.error());

    close(fd);
    remove(filename);

    return test.global_result;
}
