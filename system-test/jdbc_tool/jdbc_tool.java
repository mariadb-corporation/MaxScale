/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import java.sql.*;

public class jdbc_tool {

    /*
     * Use the connector built-in to the app to connect using the url given as argument. The url has form
     * jdbc:(mysql|mariadb):[replication:|failover:|sequential:|aurora:]//<hostDescription>[,<hostDescription>...]/[database][?<key1>=<value1>[&<key2>=<value2>]]
     *
     * If the connection succeeds, the app runs the query given in the optional second argument. If no
     * query is given, the app runs the query "SELECT rand();".
     *
     * Returns 0 if both connection and query succeed, 1 on failure. Any query results are printed on
     * standard output, row by row.
     */
    public static void main( String[] args ) throws SQLException {
        int rval = 1;
        String url;
        String query;

        int argc = args.length;
        if (argc == 1 || argc == 2) {
            url = args[0];
            query = "SELECT rand();";
            if (argc == 2) {
                query = args[1];
            }

            try (Connection conn = DriverManager.getConnection(url)) {
                try (Statement stmt = conn.createStatement()) {
                    try (ResultSet rs = stmt.executeQuery(query)) {
                        ResultSetMetaData metaData = rs.getMetaData();
                        int nCols = metaData.getColumnCount();
                        String total = "";
                        String total_sep = "";

                        while(rs.next()) {
                            String row = "";
                            String sep = "";

                            for (int i = 1; i <= nCols; i++) {
                                row += sep + rs.getString(i);
                                sep = ", ";
                            }

                            total += total_sep + row;
                            total_sep = "\n";
                        }
                        System.out.println(total);
                        rval = 0;
                    }
                }
            } catch (Exception ex) {
                System.out.println(ex.toString());
            }
        } else {
          System.out.println("Usage: java -jar jdbc_tool_mariadb_2.5.0.jar <url> [query]");
        }

        System.exit(rval);
    }
}
