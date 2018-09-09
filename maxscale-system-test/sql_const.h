#ifndef SQL_CONST_H
#define SQL_CONST_H

const char* create_repl_user
    = "grant replication slave on *.* to repl@'%%' identified by 'repl'; "
      "FLUSH PRIVILEGES";
const char* setup_slave
    = "change master to MASTER_HOST='%s', "
      "MASTER_USER='repl', "
      "MASTER_PASSWORD='repl', "
      "MASTER_LOG_FILE='%s', "
      "MASTER_LOG_POS=%s, "
      "MASTER_PORT=%d; "
      "start slave;";

const char* setup_slave_no_pos
    = "change master to MASTER_HOST='%s', "
      "MASTER_USER='repl', "
      "MASTER_PASSWORD='repl', "
      "MASTER_LOG_FILE='mar-bin.000001', "
      "MASTER_LOG_POS=4, "
      "MASTER_PORT=%d";


#endif // SQL_CONST_H
