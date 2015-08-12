#ifndef TEST_BINLOG_FNC_H
#define TEST_BINLOG_FNC_H

int check_sha1(TestConnections* Test);
int start_transaction(TestConnections* Test);
int test_binlog(TestConnections* Test, MYSQL* binlog);

#endif // TEST_BINLOG_FNC_H
