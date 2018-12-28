#ifndef SYSBENCH_COMMANDS_H
#define SYSBENCH_COMMANDS_H

/*const char * SYSBENCH_PREPARE =
         "sysbench oltp_read_write \
         --oltp-table-size=1000000 --mysql-db=test --mysql-user=skysql --mysql-password=skysql \
         --mysql-port=4006 --mysql-host=%s  prepare";



const char * SYSBENCH_COMMAND =
         "sysbench oltp_read_write \
         --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
         --mysql-db=test --mysql-table-engine=innodb  \
         --num-threads=32 --oltp-table-size=1000000  --oltp-read-only=off \
         --oltp-dist-type=uniform --oltp-skip-trx=off --init-rng=on --oltp-test-mode=complex \
         --max-requests=0  --max-time=600 run";*/

const char * SYSBENCH_PREPARE =
                "sysbench oltp_read_write \
                 --oltp-table-size=1000000 --mysql-db=test --mysql-user=skysql --mysql-password=skysql \
                 --mysql-port=4006 --mysql-host=%s --oltp-tables-count=4 prepare";

const char * SYSBENCH_COMMAND =
                "sysbench oltp_read_write \
                 --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
                 --mysql-db=test --mysql-table-engine=innodb --mysql-ignore-duplicates=on \
                 --num-threads=32 --oltp-table-size=1000000 --oltp-tables-count=2 --oltp-read-only=%s \
                 --oltp-dist-type=uniform --oltp-skip-trx=off --init-rng=on --oltp-test-mode=complex \
                 --max-requests=0 --report-interval=5 --max-time=100 run";


const char * SYSBENCH_PREPARE1 =
                "sysbench oltp_read_write \
                 --oltp-table-size=1000 --mysql-db=test --mysql-user=skysql --mysql-password=skysql \
                 --mysql-port=4006 --mysql-host=%s --oltp-tables-count=1 prepare";

const char * SYSBENCH_COMMAND1 =
                 "sysbench oltp_read_write \
                  --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
                  --mysql-db=test --mysql-table-engine=innodb --mysql-ignore-duplicates=on \
                  --num-threads=32 --oltp-table-size=1000 --oltp-tables-count=1 --oltp-read-only=%s \
                  --oltp-dist-type=uniform --oltp-skip-trx=off --init-rng=on --oltp-test-mode=complex \
                  --max-requests=0 --report-interval=5 --max-time=100 run";


const char * SYSBENCH_COMMAND_LONG =
               "sysbench oltp_read_write \
                --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
                --mysql-db=test --mysql-table-engine=innodb --mysql-ignore-duplicates=on \
                --num-threads=32 --oltp-table-size=1000000 --oltp-tables-count=2 --oltp-read-only=%s \
                --oltp-dist-type=uniform --oltp-skip-trx=off --init-rng=on --oltp-test-mode=complex \
                --max-requests=0 --report-interval=5 --max-time=2592000 run";


const char * SYSBENCH_PREPARE_SHORT =
                        "sysbench oltp_read_write \
                         --oltp-table-size=10000 --mysql-db=test --mysql-user=skysql --mysql-password=skysql \
                         --mysql-port=4006 --mysql-host=%s --oltp-tables-count=4 prepare";

const char * SYSBENCH_COMMAND_SHORT =
                       "sysbench oltp_read_write \
                        --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
                        --mysql-db=test --mysql-table-engine=innodb --mysql-ignore-duplicates=on \
                        --num-threads=32 --oltp-table-size=10000 --oltp-tables-count=2 --oltp-read-only=%s \
                        --oltp-dist-type=uniform --oltp-skip-trx=off --init-rng=on --oltp-test-mode=complex \
                        --max-requests=0 --report-interval=5 --max-time=300 run";



#endif // SYSBENCH_COMMANDS_H
