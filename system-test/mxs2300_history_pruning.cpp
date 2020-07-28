/**
 * MXS-2300: Session command history pruning
 */

#include "testconnections.h"
#include <sstream>

std::vector<int> ids;

void block_by_id(TestConnections& test, int id)
{
    for (size_t i = 0; i < ids.size(); i++)
    {
        if (ids[i] == id)
        {
            test.repl->block_node(i);
        }
    }
}

void unblock_by_id(TestConnections& test, int id)
{
    for (size_t i = 0; i < ids.size(); i++)
    {
        if (ids[i] == id)
        {
            test.repl->unblock_node(i);
        }
    }
}

int main(int argc, char** argv)
{
    TestConnections test(argc, argv);

    test.repl->connect();
    ids = test.repl->get_all_server_ids();
    test.repl->disconnect();

    int master_id = test.get_master_server_id();
    Connection conn = test.maxscales->rwsplit();
    test.expect(conn.connect(), "Connection failed: %s", conn.error());

    test.tprintf("Get the ID of the server we first start with");
    int first_id = std::stoi(conn.field("SELECT @@server_id"));

    test.tprintf("The history size is set to 10 commands, execute five and check that they are retained");
    for (int i = 0; i < 5; i++)
    {
        std::stringstream query;
        query << "SET @a" << i << " = " << i;
        conn.query(query.str());
    }

    block_by_id(test, first_id);
    test.maxscales->wait_for_monitor();

    int second_id = std::stoi(conn.field("SELECT @@server_id"));

    test.tprintf("Make sure that a reconnection actually took place");
    test.expect(first_id != second_id && second_id > 0, "Invalid server ID: %d", second_id);
    test.expect(master_id != second_id, "SELECT should not go to the master");

    test.tprintf("Check that the values were correctly set");
    for (int i = 0; i < 5; i++)
    {
        std::string value = std::to_string(i);
        std::string query = "SELECT @a" + value;
        test.expect(conn.check(query, value), "Invalid value for user variable @a%s", value.c_str());
    }

    unblock_by_id(test, first_id);

    test.tprintf("Execute 15 commands and check that we lose the first five values");
    for (int i = 0; i < 15; i++)
    {
        std::stringstream query;
        query << "SET @b" << i << " =" << i;
        conn.query(query.str());
    }

    block_by_id(test, second_id);
    test.maxscales->wait_for_monitor();

    int third_id = std::stoi(conn.field("SELECT @@server_id"));

    test.expect(third_id != second_id && third_id > 0, "Invalid server ID: %d", third_id);
    test.expect(master_id != third_id, "SELECT should not go to the master");

    for (int i = 0; i < 5; i++)
    {
        std::string variable = "@b" + std::to_string(i);
        std::string query = "SELECT IFNULL(" + variable + ", '" + variable + " is null')";
        test.expect(conn.check(query, variable + " is null"), "%s should not be set", variable.c_str());
    }

    test.tprintf("Check that the remaining values were correctly set");
    for (int i = 5; i < 15; i++)
    {
        std::string value = std::to_string(i);
        std::string query = "SELECT @b" + value;
        std::string f = conn.field(query);
        test.expect(conn.check(query, value), "Invalid value for user variable @b%s: %s", value.c_str(), f.c_str());
    }

    unblock_by_id(test, second_id);

    return test.global_result;
}
