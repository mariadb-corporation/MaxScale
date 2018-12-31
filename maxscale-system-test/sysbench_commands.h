#ifndef SYSBENCH_COMMANDS_H
#define SYSBENCH_COMMANDS_H

/*const char * SYSBENCH_PREPARE =
         "sysbench oltp_read_write \
         --mysql-db=test --mysql-user=skysql --mysql-password=skysql \
         --mysql-port=4006 --mysql-host=%s  prepare";



const char * SYSBENCH_COMMAND =
         "sysbench oltp_read_write \
         --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
         --mysql-db=test \
         --num-threads=32 \
         --oltp-dist-type=uniform --oltp-skip-trx=off --init-rng=on --oltp-test-mode=complex \
         --max-requests=0 --time=600 run";*/

const char * SYSBENCH_PREPARE =
                "sysbench oltp_read_write \
                 --mysql-db=test --mysql-user=skysql --mysql-password=skysql \
                 --mysql-port=4006 --mysql-host=%s prepare";

const char * SYSBENCH_COMMAND =
                "sysbench oltp_read_write \
                 --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
                 --mysql-db=test \
                 --threads=32 \
                 --max-requests=0 --report-interval=5 --time=100 run";

const char * SYSBENCH_PREPARE_RO =
                 "sysbench oltp_read_only \
                --mysql-db=test --mysql-user=skysql --mysql-password=skysql \
                --mysql-port=4006 --mysql-host=%s prepare";

const char * SYSBENCH_COMMAND_RO =
                "sysbench oltp_read_only \
                --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
                --mysql-db=test \
                --threads=32 \
                --max-requests=0 --report-interval=5 --time=100 run";


const char * SYSBENCH_PREPARE1 =
                "sysbench oltp_read_write \
                 --mysql-db=test --mysql-user=skysql --mysql-password=skysql \
                 --mysql-port=4006 --mysql-host=%s prepare";

const char * SYSBENCH_COMMAND1 =
                 "sysbench oltp_read_write \
                  --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
                  --mysql-db=test \
                  --threads=32  \
                  --max-requests=0 --report-interval=5 --time=100 run";


const char * SYSBENCH_COMMAND_LONG =
               "sysbench oltp_read_write \
                --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
                --mysql-db=test \
                --threads=32 \
                --max-requests=0 --report-interval=5 --time=2592000 run";


const char * SYSBENCH_PREPARE_SHORT =
                        "sysbench oltp_read_write \
                         --mysql-db=test --mysql-user=skysql --mysql-password=skysql \
                         --mysql-port=4006 --mysql-host=%s prepare";

const char * SYSBENCH_COMMAND_SHORT =
                       "sysbench oltp_read_write \
                        --mysql-host=%s --mysql-port=%d --mysql-user=skysql --mysql-password=skysql \
                        --mysql-db=test \
                        --threads=32 \
                        --max-requests=0 --report-interval=5 --time=300 run";

#endif // SYSBENCH_COMMANDS_H
