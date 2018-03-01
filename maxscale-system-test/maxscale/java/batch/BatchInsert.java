package maxscale.java.batch;

import maxscale.java.MaxScaleConfiguration;
import maxscale.java.MaxScaleConnection;
import java.sql.Connection;
import java.sql.Statement;

public class BatchInsert {

    public static void main(String[] args) {
        boolean error = false;
        try {
            MaxScaleConfiguration config = new MaxScaleConfiguration("batchinsert");
            MaxScaleConnection maxscale = new MaxScaleConnection("useBatchMultiSendNumber=500");

            try {
                Connection connection = maxscale.getConnRw();
                Statement stmt = connection.createStatement();

                stmt.execute("DROP TABLE IF EXISTS tt");
                stmt.execute("CREATE TABLE tt (d int)");

                for (int i = 0; i < 150; i++) {
                    stmt.addBatch("INSERT INTO tt(d) VALUES (1)");

                    if (i % 3 == 0) {
                        stmt.addBatch("SET @test2='aaa'");
                    }
                }

                stmt.executeBatch();
                System.out.println("finished");

            } catch (Exception e) {
                System.out.println("Error: " + e.getMessage());
                error = true;
            }
            config.close();

        } catch (Exception e) {
            System.out.println("Error: " + e.getMessage());
        }

        if (error) {
            System.exit(1);
        }
    }
}
