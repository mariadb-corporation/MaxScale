package maxscale.java;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.sql.Statement;

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
    private Connection conn_rw = null, conn_master = null, conn_slave = null;
    public static final int READWRITESPLIT_PORT = 4006;
    public static final int READCONNROUTE_MASTER_PORT = 4008;
    public static final int READCONNROUTE_SLAVE_PORT = 4009;

    public String getIp() {
        return ip;
    }

    public String getUser() {
        return user;
    }

    public String getPassword() {
        return password;
    }

    public Connection getConnRw() {
        return conn_rw;
    }

    public Connection getConnMaster() {
        return conn_master;
    }

    public Connection getConnSlave() {
        return conn_slave;
    }

    public void setConnRw(Connection conn) {
        conn_rw = conn;
    }

    public void setConnMaster(Connection conn) {
        conn_master = conn;
    }

    public void setConnSlave(Connection conn) {
        conn_slave = conn;
    }

    public MaxScaleConnection(String options) throws SQLException, Exception {
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

        Class.forName("org.mariadb.jdbc.Driver");
        conn_rw = DriverManager.getConnection(
                "jdbc:mariadb://" + ip + ":" + READWRITESPLIT_PORT + "/test?" + options, user, password);

        conn_master = DriverManager.getConnection(
                "jdbc:mariadb://" + ip + ":" + READCONNROUTE_MASTER_PORT + "/test?" + options, user, password);

        conn_slave = DriverManager.getConnection(
                "jdbc:mariadb://" + ip + ":" + READCONNROUTE_SLAVE_PORT + "/test?" + options, user, password);
    }

    public MaxScaleConnection() throws SQLException, Exception {
        this("");
    }

    public boolean isSmokeTest() {
        return smoke_test;
    }

    public void query(Connection connection, String query) throws SQLException {
        Statement stmt = connection.createStatement();
        stmt.execute(query);
    }
}
