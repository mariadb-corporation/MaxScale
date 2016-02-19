package maxscale.java;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.ResultSetMetaData;
import java.sql.SQLException;
import java.sql.Statement;
import org.mariadb.jdbc.MariaDbDataSource;

/**
 * Simple MaxScale connection class
 *
 * Allows execution of queries to one of the MaxScale services configured for
 * testing.
 */
public class MaxScaleConnection {

    private static String ip = null;
    private static String user = null;
    private static String password = null;
    private static boolean smoke_test = false;
    private MariaDbDataSource datasource_rw = null;
    private MariaDbDataSource datasource_rc_master = null;
    private MariaDbDataSource datasource_rc_slave = null;
    private Connection conn_rw = null, conn_master = null, conn_slave = null;
    public static final int READWRITESPLIT_PORT = 4006;
    public static final int READCONNROUTE_MASTER_PORT = 4008;
    public static final int READCONNROUTE_SLAVE_PORT = 4009;

    public Connection getConn_rw() {
        return conn_rw;
    }

    public Connection getConn_master() {
        return conn_master;
    }

    public Connection getConn_slave() {
        return conn_slave;
    }

    public MaxScaleConnection() throws SQLException, Exception {
        String s = System.getenv("smoke");
        smoke_test = (s != null && s.compareTo("yes") == 0);

        if ((ip = System.getenv("maxscale_IP")) == null || ip.length() == 0) {
            throw new Exception("Missing environment variable 'maxscale_IP'.");
        }

        if ((user = System.getenv("maxscale_user")) == null || user.length() == 0) {
            throw new Exception("Missing environment variable 'maxscale_user'.");
        }

        if ((password = System.getenv("maxscale_password")) == null || password.length() == 0) {
            throw new Exception("Missing environment variable 'maxscale_password'.");
        }

        System.out.println("IP: " + ip + " User: " + user + " Password: " + password);

        datasource_rw = new MariaDbDataSource(ip, READWRITESPLIT_PORT, "");
        datasource_rc_master = new MariaDbDataSource(ip, READCONNROUTE_MASTER_PORT, "");
        datasource_rc_slave = new MariaDbDataSource(ip, READCONNROUTE_SLAVE_PORT, "");
        conn_rw = datasource_rw.getConnection(user, password);
        conn_master = datasource_rc_master.getConnection(user, password);
        conn_slave = datasource_rc_slave.getConnection(user, password);
    }

    public boolean isSmokeTest() {
        return smoke_test;
    }

    public void query(Connection connection, String query) throws SQLException {
        System.out.println(query);
        Statement stmt = connection.createStatement();
        stmt.execute(query);
    }
}
