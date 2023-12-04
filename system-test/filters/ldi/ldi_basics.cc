/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxtest/testconnections.hh>
#include <maxtest/docker.hh>

const size_t TARGET_BYTES = 1024 * 1024 * 50;   // We want a 50MiB file
const size_t TARGET_ROWS = TARGET_BYTES / 6;    // Length of 'hello\n' is 6
const std::string ROWS = std::to_string(TARGET_ROWS);

// The S3 variables are required by normal MariaDB data imports. The others are required by Xpand.
const char* SET_VARIABLES = "SET @maxscale.ldi.s3_key='my-access-key', "
                            "@maxscale.ldi.s3_secret='my-secret-key', "
                            "@maxscale.ldi.import_user='maxskysql', "
                            "@maxscale.ldi.import_password='skysql'";

// Puts InnoDB into a special mode that makes it faster
const char* GO_FASTER = "SET autocommit=0, unique_checks=0, foreign_key_checks=0";

void ldi_from_s3(TestConnections& test, mxt::MariaDBServer* backend)
{
    auto conn = backend->open_connection();
    auto table = conn->create_table("test.t1", "data CHAR(10)");

    auto c = test.maxscale->rwsplit();
    MXT_EXPECT_F(c.connect(), "Failed to connect: %s", c.error());
    MXT_EXPECT_F(c.query(SET_VARIABLES), "SET failed: %s", c.error());
    MXT_EXPECT(c.query(GO_FASTER));
    MXT_EXPECT_F(c.query("LOAD DATA INFILE 's3://my-bucket/test.csv' INTO TABLE t1"),
                 "LOAD DATA failed: %s", c.error());
    MXT_EXPECT(c.query("COMMIT"));
    MXT_EXPECT(c.query("BEGIN"));
    auto rows = c.field("SELECT COUNT(*) FROM t1");
    MXT_EXPECT(c.query("COMMIT"));
    MXT_EXPECT_F(rows == ROWS, "Expected %s rows, got %s", ROWS.c_str(), rows.c_str());
}

void normal_ldli(TestConnections& test, mxt::MariaDBServer* backend)
{
    char filepath[256] = "/tmp/data.csv.XXXXXX";
    int fd = mkstemp(filepath);

    for (size_t i = 0; i < TARGET_ROWS; i++)
    {
        write(fd, "hello\n", 6);
    }

    close(fd);

    try
    {
        auto conn = backend->open_connection();
        auto table = conn->create_table("test.t1", "data CHAR(10)");

        auto c = test.maxscale->rwsplit();
        MXT_EXPECT_F(c.connect(), "Failed to connect: %s", c.error());
        MXT_EXPECT_F(c.query(SET_VARIABLES), "SET failed: %s", c.error());
        MXT_EXPECT(c.query(GO_FASTER));
        MXT_EXPECT(c.query(mxb::cat("LOAD DATA LOCAL INFILE '", filepath, "' INTO TABLE t1")));
        MXT_EXPECT(c.query("COMMIT"));
        MXT_EXPECT(c.query("BEGIN"));
        auto rows = c.field("SELECT COUNT(*) FROM t1");
        MXT_EXPECT(c.query("COMMIT"));
        MXT_EXPECT_F(rows == ROWS, "Expected %s rows, got %s", ROWS.c_str(), rows.c_str());
    }
    catch (const std::exception& e)
    {
        test.add_failure("Caught exception: %s", e.what());
    }

    remove(filepath);
}

void test_main(TestConnections& test)
{
    mxt::Docker container(test, "quay.io/minio/minio", "minio", {9000, 9001}, {},
                          "server /data --console-address \":9001\"", "");

    for (std::string cmd : std::vector<std::string> {
        "gunzip /opt/bin/mc.gz",
        "install /opt/bin/mc /bin/",
        "mc alias set local http://localhost:9000 minioadmin minioadmin",
        "mc admin user add local test-user test-user",
        "mc admin policy attach local --user test-user readwrite",
        "mc admin user svcacct add local test-user --access-key my-access-key --secret-key my-secret-key",
        "mc mb local/my-bucket",
        "/bin/sh -c \"yes hello|head -n " + ROWS + "|mc pipe local/my-bucket/test.csv\""})
    {
        container.execute(cmd);
    }

    auto backend = test.repl->backend(0);

    test.log_printf("Testing LDI from S3");
    ldi_from_s3(test, backend);

    test.log_printf("Testing normal LDLI");
    normal_ldli(test, backend);
}

int main(int argc, char** argv)
{
    return TestConnections().run_test(argc, argv, test_main);
}
