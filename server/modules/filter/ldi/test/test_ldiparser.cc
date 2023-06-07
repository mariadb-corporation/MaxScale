/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "../ldiparser.hh"
#include <iostream>

int check(std::string_view sql, bool expected,
          std::string bucket = "", std::string file = "",
          std::string db = "", std::string table = "")
{
    auto parsed = parse_ldi(sql);
    auto* res = std::get_if<LoadDataResult>(&parsed);
    auto* err = std::get_if<ParseError>(&parsed);
    bool success = res != nullptr;
    int errors = 0;

    if (success != expected)
    {
        std::cout << "Expected " << std::boolalpha << expected << ", got " << success << "." << std::endl;
        ++errors;

        if (expected && !success && err)
        {
            std::cout << "Parser error:\n" << err->message << std::endl;
        }
    }
    else if (expected)
    {
        if (res->s3.bucket != bucket)
        {
            std::cout << "Bucket mismatch: " << res->s3.bucket << " != " << bucket << std::endl;
            ++errors;
        }

        if (res->s3.filename != file)
        {
            std::cout << "File mismatch: " << res->s3.filename << " != " << file << std::endl;
            ++errors;
        }

        if (res->db != db)
        {
            std::cout << "DB mismatch: " << res->db << " != " << db << std::endl;
            ++errors;
        }

        if (res->table != table)
        {
            std::cout << "Table mismatch: " << res->table << " != " << table << std::endl;
            ++errors;
        }
    }

    return errors;
}

int main(int argc, char** argv)
{
    int rc = 0;

    rc += check("SELECT 1", false);
    rc += check("LOAD DATA LOCAL INFILE 's3://bucket/file.csv' INTO TABLE t1", false);
    rc += check("LOAD INTO TABLE t1", false);
    rc += check("LOAD INTO TABLE test.t1", false);
    rc += check("LOAD DATA INFILE '/tmp/data.csv' INTO TABLE t1 ", false);
    rc += check("LOAD DATA INFILE 'http://tmp/data.csv' INTO TABLE t1 ", false);
    rc += check("LOAD DATA INFILE 'ftp://tmp/data.csv' INTO TABLE t1 ", false);

    rc += check("LOAD DATA INFILE 'S3://bucket/file' INTO TABLE t1",
                true, "bucket", "file", "", "t1");

    rc += check("load data infile 's3://bucket/file' into table t1",
                true, "bucket", "file", "", "t1");

    rc += check("LOAD DATA INFILE 'gs://bucket/file' INTO TABLE t1",
                true, "bucket", "file", "", "t1");

    rc += check("LOAD DATA INFILE 's3://bucket/file' INTO TABLE test.t1",
                true, "bucket", "file", "test", "t1");

    rc += check("LOAD DATA INFILE 's3://bucket/file/with/path.csv' INTO TABLE test.t1",
                true, "bucket", "file/with/path.csv", "test", "t1");

    return rc;
}
