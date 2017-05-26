package maxscale.java.test1;

import maxscale.java.MaxScaleConfiguration;
import maxscale.java.MaxScaleConnection;

public class SimpleConnectorJTest {

    public static final int RWSPLIT_PORT = 4006;
    public static final int READCONN_MASTER = 4008;
    public static final int READCONN_SLAVE = 4009;
    public static final String DATABASE_NAME = "mytestdb";
    public static final String TABLE_NAME = "t1";
    public static final int ITERATIONS_NORMAL = 1500;
    public static final int ITERATIONS_SMOKE = 150;
    public static int test_rows = ITERATIONS_NORMAL;

    public static void main(String[] args) {
        boolean error = false;

        try {
            MaxScaleConfiguration config = new MaxScaleConfiguration("simplejavatest");
            MaxScaleConnection maxscale = new MaxScaleConnection();
            try {

                if (maxscale.isSmokeTest()) {
                    test_rows = ITERATIONS_SMOKE;
                }

                System.out.println("Creating databases and tables..");
                maxscale.query(maxscale.getConnMaster(), "DROP DATABASE IF EXISTS " + DATABASE_NAME);
                maxscale.query(maxscale.getConnMaster(), "CREATE DATABASE " + DATABASE_NAME);
                maxscale.query(maxscale.getConnMaster(), "CREATE TABLE " + DATABASE_NAME
                               + "." + TABLE_NAME + "(id int primary key auto_increment, data varchar(128))");

                System.out.println("Inserting " + test_rows + " values");
                for (int i = 0; i < test_rows; i++) {
                    maxscale.query(maxscale.getConnMaster(),
                                   "INSERT INTO " + DATABASE_NAME + "." + TABLE_NAME
                                   + "(data) VALUES (" + String.valueOf(System.currentTimeMillis()) + ")");
                }

                System.out.println("Querying " + test_rows / 10 + "rows " + test_rows + " times");
                for (int i = 0; i < test_rows; i++) {
                    maxscale.query(maxscale.getConnMaster(),
                                   "SELECT * FROM " + DATABASE_NAME + "." + TABLE_NAME
                                   + " LIMIT " + test_rows / 10);
                }

                maxscale.query(maxscale.getConnMaster(), "DROP DATABASE IF EXISTS " + DATABASE_NAME);

            } catch (Exception ex) {
                error = true;
                System.out.println("Error: " + ex.getMessage());
                ex.printStackTrace();
            }

            config.close();

        } catch (Exception ex) {
            error = true;
            System.out.println("Error: " + ex.getMessage());
            ex.printStackTrace();
        }

        if (error) {
            System.exit(1);
        }
    }
}
