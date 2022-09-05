#include <maxtest/testconnections.hh>

/**
 * Get master server id (master decided by MaxScale)
 *
 * @param maxscale_ind Which MaxScale to query
 * @param test Tester object
 * @return Master server id
 */
int get_master_server_id(TestConnections& test, int maxscale_ind = 0)
{
    MYSQL* conn = test.maxscale->open_rwsplit_connection();
    int id = -1;
    char str[1024];

    if (find_field(conn, "SELECT @@server_id, @@last_insert_id;", "@@server_id", str) == 0)
    {
        id = atoi(str);
    }

    mysql_close(conn);
    return id;
}
