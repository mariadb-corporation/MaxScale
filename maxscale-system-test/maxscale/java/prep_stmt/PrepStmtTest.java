package maxscale.java.prep_stmt;

import maxscale.java.MaxScaleConfiguration;
import maxscale.java.MaxScaleConnection;

import java.sql.Connection;
import java.sql.DriverManager;
import java.sql.SQLException;
import java.sql.Statement;
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.util.ArrayList;

public class PrepStmtTest {

    public static final int RWSPLIT_PORT = 4006;
    public static final String DATABASE_NAME = "test";
    public static final String TABLE_NAME = "t1";

    public static final int THREADS = 5;
    public static final int ITERATIONS_NORMAL = 100;
    public static final int ITERATIONS_SMOKE = 50;
    public static int test_iter = ITERATIONS_NORMAL;

    public static final String INSERT_SQL = "INSERT INTO " + DATABASE_NAME + "." + TABLE_NAME + " VALUES (NULL, ?)";
    public static final String SELECT_SQL = "SELECT * FROM " + DATABASE_NAME + "." + TABLE_NAME + " WHERE  id = ?";

    public static String conn_str = null;
    public static String user = null;
    public static String password = null;

    public static void main(String[] args) {
        boolean error = false;

        try {
            MaxScaleConfiguration config = new MaxScaleConfiguration("java_prep_stmt");
            MaxScaleConnection maxscale = new MaxScaleConnection();

            try {
                test_iter = ITERATIONS_SMOKE;

                // Prepare test database
                System.out.println("Creating databases and tables..");
                maxscale.query(maxscale.getConnMaster(), "DROP DATABASE IF EXISTS " + DATABASE_NAME);
                maxscale.query(maxscale.getConnMaster(), "CREATE DATABASE " + DATABASE_NAME);
                maxscale.query(maxscale.getConnMaster(), "CREATE TABLE " + DATABASE_NAME
                               + "." + TABLE_NAME + "(id int primary key auto_increment, data varchar(128))");


                conn_str = "jdbc:mariadb://" + maxscale.getIp() + ":" +
                    MaxScaleConnection.READWRITESPLIT_PORT + "/test?";
                user = maxscale.getUser();
                password = maxscale.getPassword();

                ArrayList<Thread> threads = new ArrayList<>();

                for (int i = 0; i < THREADS; i++) {
                    threads.add(new Thread(){
                            public void run() {

                                try  {
                                    Connection conn = DriverManager.getConnection(conn_str, user, password);
                                    conn.setAutoCommit(false);

                                    for (int i = 0; i < test_iter; i++) {
                                        conn.isValid(1);

                                        PreparedStatement ps_insert = conn.prepareStatement(PrepStmtTest.INSERT_SQL);

                                        ps_insert.setString(1, String.valueOf(i));
                                        ps_insert.executeUpdate();
                                        conn.commit();

                                        conn.isValid(1);

                                        PreparedStatement ps_select = conn.prepareStatement(PrepStmtTest.SELECT_SQL);
                                        ps_select.setInt(1, i);
                                        ResultSet rset = ps_select.executeQuery();

                                        conn.isValid(1);

                                        while (rset.next()) {
                                            int r = rset.getInt(1);
                                            String s = rset.getString(2);
                                            if (i % 10 == 0) {
                                                System.out.println("Result: " + String.valueOf(r) + " " + s);
                                            }
                                        }
                                    }
                                    conn.close();
                                } catch (Exception ex) {
                                    System.out.println("Error: " + ex.getMessage());
                                    ex.printStackTrace();
                                    System.exit(1);
                                }
                            }
                        });
                }

                System.out.println("Starting " + String.valueOf(threads.size()) + " threads");

                for (Thread a: threads) {
                    a.start();
                }

                for (Thread a: threads) {
                    a.join();
                }

            } catch (Exception ex) {
                error = true;
                System.out.println("Error: " + ex.getMessage());
                ex.printStackTrace();
            }

            config.close();
        }
        catch (Exception ex) {
            error = true;
            System.out.println("Error: " + ex.getMessage());
            ex.printStackTrace();
        }

        if (error) {
            System.exit(1);
        }
    }
}
