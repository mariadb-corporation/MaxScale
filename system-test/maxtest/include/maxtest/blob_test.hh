#pragma once

#include <maxtest/testconnections.hh>

/**
 * @brief test_longblob INSERT big amount of data into long_blob_table
 * @param test TestConnection object
 * @param conn MYSQL connection handler
 * @param blob_name blob type (LONGBLOB, MEDIUMBLOB or BLOB)
 * @param chunk_size size of one data chunk
 * @param chunks number of chunks to INSERT
 * @param rows number of rows to INSERT (executes INSERT statement 'rows' times)
 * @return true on success
 */
bool test_longblob(TestConnections& test, MYSQL* conn, const char* blob_name,
                   unsigned long chunk_size, int chunks, int rows);

/**
 * @brief check_longblob_data Does SELECT against table created by test_longblob() and checks that data are
 * correct
 * @param Test TestConnection object
 * @param conn MYSQL connection handler
 * @param chunk_size size of one data chunk (in sizeof(long usingned))
 * @param chunks number of chunks in the table
 * @param rows number of rows in the table
 * @return 0 in case of success
 */
int check_longblob_data(TestConnections& Test, MYSQL* conn, size_t chunk_size, size_t chunks, int rows);
