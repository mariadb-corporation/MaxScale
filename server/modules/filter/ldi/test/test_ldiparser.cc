/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "../ldiparser.hh"
#include <iostream>

int check(std::string_view sql, bool expected,
          std::string bucket = "", std::string file = "",
          std::string db = "", std::string table = "",
          bool local = false)
{
    auto parsed = parse_ldi(sql);
    auto* res = std::get_if<LoadDataInfile>(&parsed);
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
        auto url_parsed = parse_s3_url(res->filename);
        auto* s3 = std::get_if<S3URL>(&url_parsed);
        err = std::get_if<ParseError>(&url_parsed);

        if (s3)
        {
            if (s3->bucket != bucket)
            {
                std::cout << "Bucket mismatch: " << s3->bucket << " != " << bucket << std::endl;
                ++errors;
            }

            if (s3->filename != file)
            {
                std::cout << "File mismatch: " << s3->filename << " != " << file << std::endl;
                ++errors;
            }
        }
        else if (!bucket.empty())
        {
            std::cout << "URL parsing failed: " << err->message << std::endl;
            ++errors;
        }
        else if (res->filename != file)
        {
            std::cout << "Filename mismatch: " << res->filename << " != " << file << std::endl;
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

        if (res->local != local)
        {
            std::cout << "LOCAL mismatch: " << std::boolalpha << res->local << " != " << local << std::endl;
            ++errors;
        }
    }

    return errors;
}

int main(int argc, char** argv)
{
    int rc = 0;

    rc += check("SELECT 1", false);
    rc += check("LOAD INTO TABLE t1", false);
    rc += check("LOAD INTO TABLE test.t1", false);


    for (std::string local : {"", " LOCAL "})
    {
        bool is_local = !local.empty();

        rc += check("LOAD DATA " + local + " INFILE '/tmp/data.csv' INTO TABLE t1 ",
                    true, "", "/tmp/data.csv", "", "t1", is_local);

        rc += check("LOAD DATA " + local + " INFILE 'http://tmp/data.csv' INTO TABLE t1 ",
                    true, "", "http://tmp/data.csv", "", "t1", is_local);

        rc += check("LOAD DATA " + local + " INFILE 'ftp://tmp/data.csv' INTO TABLE t1 ",
                    true, "", "ftp://tmp/data.csv", "", "t1", is_local);

        // TODO: These should perhaps be parsed as valid S3 URLs but be reported as bad filenames at some
        // other layer.
        rc += check("LOAD DATA " + local + " INFILE 's3://tmp/data!csv' INTO TABLE t1 ",
                    true, "", "s3://tmp/data!csv", "", "t1", is_local);

        rc += check("LOAD DATA " + local + " INFILE 's3://tmp/data$csv' INTO TABLE t1 ",
                    true, "", "s3://tmp/data$csv", "", "t1", is_local);

        rc += check("LOAD DATA " + local + " INFILE 'S3://bucket/file' INTO TABLE t1",
                    true, "bucket", "file", "", "t1", is_local);

        rc += check("load data " + local + " infile 's3://bucket/file' into table t1",
                    true, "bucket", "file", "", "t1", is_local);

        rc += check("LOAD DATA " + local + " INFILE 'gs://bucket/file' INTO TABLE t1",
                    true, "bucket", "file", "", "t1", is_local);

        rc += check("LOAD DATA " + local + " INFILE 's3://bucket/file' INTO TABLE test.t1",
                    true, "bucket", "file", "test", "t1", is_local);

        rc += check("LOAD DATA " + local + " INFILE 's3://bucket/file/with/path.csv' INTO TABLE test.t1",
                    true, "bucket", "file/with/path.csv", "test", "t1", is_local);

        rc += check("LOAD DATA " + local + " INFILE 's3://bucket-with-dash/file.csv' INTO TABLE test.t1",
                    true, "bucket-with-dash", "file.csv", "test", "t1", is_local);

        rc += check("LOAD DATA " + local + " INFILE 's3://bucket/file-with-dash.csv' INTO TABLE test.t1",
                    true, "bucket", "file-with-dash.csv", "test", "t1", is_local);
    }

    return rc;
}
