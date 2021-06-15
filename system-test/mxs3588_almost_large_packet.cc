#include <maxtest/testconnections.hh>

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);
    std::string query = "SELECT '";

    // One byte command byte and one byte for the single quote
    query.append(0xfffffb - 1 - query.size() - 1, 'a');
    query += "'";

    auto c = test.maxscale->rwsplit();
    c.connect();
    test.expect(c.query(query), "First query should work: %s", c.error());
    test.expect(c.query(query), "Second query should work: %s", c.error());
    test.expect(c.query(query), "Third query should work: %s", c.error());
    test.expect(c.query("SELECT 1"), "Small query should work: %s", c.error());

    return test.global_result;
}
