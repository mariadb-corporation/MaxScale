#ifndef _SHARDING_COMMON_HG
#define _SHARDING_COMMON_HG

#include <my_config.h>
#include <poll.h>
#include <buffer.h>
#include <modutil.h>
#include <mysql_client_server_protocol.h>
#include <hashtable.h>
#include <log_manager.h>
#include <query_classifier.h>

bool extract_database(GWBUF* buf, char* str);
void create_error_reply(char* fail_str,DCB* dcb);
bool change_current_db(char* dest, HASHTABLE* dbhash, GWBUF* buf);

#endif
