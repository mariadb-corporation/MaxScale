#ifndef SQL_CONST_H
#define SQL_CONST_H

/*const char * create_repl_user =
        "grant replication slave on *.* to repl@'%' identified by 'repl';\
        FLUSH PRIVILEGES";*/
const char * setup_slave =
        "change master to MASTER_HOST='%s';\
        change master to MASTER_USER='repl';\
        change master to MASTER_PASSWORD='repl';\
        change master to MASTER_LOG_FILE='%s';\
        change master to MASTER_LOG_POS=%s;\
        change master to MASTER_PORT=%d";

#endif // SQL_CONST_H
