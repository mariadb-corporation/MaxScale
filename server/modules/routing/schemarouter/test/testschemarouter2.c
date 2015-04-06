#include <my_config.h>
#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysqld_error.h>

int main(int argc, char** argv)
{

    /**
     * This test sets a session variable, creates tables in each of the shards
     * and inserts into them a single value while querying the session variable.
     * This will show if the value in the session variable in the shard is set and
     * if it is the same in all the shards.
     *
     * The test fails if any of the session variables is not set or differ from the original value.
     */
    
    const unsigned int ports[4] = {
        3000,
        3001,
        3002,
        3003
    };

    const char* srv_id[4] = {
        "3000",
        "3001",
        "3002",
        "3003"
    };

    const char* databases[4] = {
        "db0",
        "db1",
        "db2",
        "db3"
    };

	MYSQL* server;
    MYSQL_RES *result,*shdres;
    MYSQL_ROW row;
	char *host = NULL,*username = NULL, *password = NULL;
    char query[2048];
	unsigned int port,errnum,optval;
    unsigned long *lengths;
	int rval, i, j;

    if(argc < 5)
    {
        fprintf(stderr,"Usage: %s <host> <port> <username> <password>\n",argv[0]);
        return 1;
    }
    
	host = strdup(argv[1]);
	port = atoi(argv[2]);
	username = strdup(argv[3]);
    password = strdup(argv[4]);
	rval = 0;

    for(i = 0;i<4;i++)
    {

        if((server = mysql_init(NULL)) == NULL){
            fprintf(stderr,"Error : Initialization of MySQL client failed.\n");
            rval = 1;
            goto report;
        }
        optval = 1;
        mysql_options(server,MYSQL_OPT_CONNECT_TIMEOUT,&optval);

        if(mysql_real_connect(server,host,username,password,NULL,ports[i],NULL,0) == NULL){
            fprintf(stderr, "Failed to connect to server on port %d: %s\n",
                    ports[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }

        sprintf(query,"STOP SLAVE");
        if(mysql_real_query(server,query,strlen(query)))
        {
            fprintf(stderr, "Failed to stop slave in %d: %s.\n",
                    ports[i],
                    mysql_error(server));
        }


        for(j = 0;j<4;j++)
        {
            sprintf(query,"DROP DATABASE IF EXISTS %s",databases[j]);
            if(mysql_real_query(server,query,strlen(query)))
            {
                fprintf(stderr, "Failed to drop database in %d: %s.\n",
                        ports[i],
                        mysql_error(server));
            }
            
        }
        mysql_close(server);
    }

    for(i=0;i<4;i++)
    {
        if((server = mysql_init(NULL)) == NULL){
            fprintf(stderr,"Error : Initialization of MySQL client failed.\n");
            rval = 1;
            goto report;
        }
        
        mysql_options(server,MYSQL_OPT_CONNECT_TIMEOUT,&optval);
        
        if(mysql_real_connect(server,host,username,password,NULL,ports[i],NULL,0) == NULL){
            fprintf(stderr, "Failed to connect to server on port %d: %s\n",
                    ports[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }
        
        sprintf(query,"CREATE DATABASE %s",databases[i]);
        if(mysql_real_query(server,query,strlen(query)))
        {
            fprintf(stderr, "Failed to create database '%s' in %d: %s.\n",
                    databases[i],
                    ports[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }

        sprintf(query,"DROP TABLE IF EXISTS %s.t1",databases[i]);
        if(mysql_real_query(server,query,strlen(query)))
        {
            fprintf(stderr, "Failed to drop table '%s.t1' in %d: %s.\n",
                    databases[i],
                    ports[i],
                    mysql_error(server));
        }
        

        sprintf(query,"CREATE TABLE %s.t1 (id int)",databases[i]);
        if(mysql_real_query(server,query,strlen(query)))
        {
            fprintf(stderr, "Failed to create table '%s.t1' in %d: %s.\n",
                    databases[i],
                    ports[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }
        

        if(mysql_select_db(server,databases[i]))
        {
            fprintf(stderr, "Failed to use database %s in %d: %s.\n",
                    databases[i],
                    ports[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }

        sprintf(query,"INSERT INTO t1 values (%s)",srv_id[i]);
        if(mysql_real_query(server,query,strlen(query)))
        {
            fprintf(stderr, "Failed to insert values in %d: %s.\n",
                    ports[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }

        mysql_close(server);
    }

/** Test 1 - With default database  */

    printf("Testing with default database.\n");

    for(i = 0;i<4;i++)
    {

        printf("Testing database %s through MaxScale.\n",databases[i]);
        if((server = mysql_init(NULL)) == NULL){
            fprintf(stderr,"Error : Initialization of MySQL client failed.\n");
            rval = 1;
            goto report;
        }

        if(mysql_real_connect(server,host,username,password,databases[i],port,NULL,0) == NULL){
            fprintf(stderr, "Failed to connect to port %d using database %s: %s\n",
                    port,
                    databases[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }

        if(mysql_real_query(server,"SELECT id FROM t1",strlen("SELECT id FROM t1")))
        {
            fprintf(stderr, "Failed to execute query in %d: %s.\n",
                    ports[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }

        result = mysql_store_result(server);

        while((row = mysql_fetch_row(result)))
        {
            if(strcmp(row[0],srv_id[i]))
            {
                fprintf(stderr, "Test failed in %d: Was expecting %s but got %s instead.\n",
                        ports[i],srv_id[i],row[0]);
                rval = 1;
            
            }
        }

        mysql_free_result(result);
        
        mysql_close(server);

    }

    printf("Testing without default database and USE ... query.\n");

    for(i = 0;i<4;i++)
    {

        printf("Testing server on port %d through MaxScale.\n",ports[i]);
        if((server = mysql_init(NULL)) == NULL){
            fprintf(stderr,"Error : Initialization of MySQL client failed.\n");
            rval = 1;
            goto report;
        }

        if(mysql_real_connect(server,host,username,password,NULL,port,NULL,0) == NULL){
            fprintf(stderr, "Failed to connect to port %d using database %s: %s\n",
                    port,
                    databases[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }

        sprintf(query,"USE %s",databases[i]);
        if(mysql_select_db(server,databases[i]))
        {
            fprintf(stderr, "Failed to use database %s in %d: %s.\n",
                    databases[i],
                    ports[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }
    
        if(mysql_real_query(server,"SELECT id FROM t1",strlen("SELECT id FROM t1")))
        {
            fprintf(stderr, "Failed to execute query in %d: %s.\n",
                    ports[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }

        result = mysql_store_result(server);

        while((row = mysql_fetch_row(result)))
        {
            if(strcmp(row[0],srv_id[i]))
            {
                fprintf(stderr, "Test failed in %d: Was expecting %s but got %s instead.\n",
                        ports[i],srv_id[i],row[0]);
                rval = 1;
            
            }
        }

        mysql_free_result(result);
        
        mysql_close(server);
    }

/** Cleanup and START SLAVE */

    for(i = 0;i<4;i++)
    {
        
        if((server = mysql_init(NULL)) == NULL){
            fprintf(stderr,"Error : Initialization of MySQL client failed.\n");
            rval = 1;
            goto report;
        }

        if(mysql_real_connect(server,host,username,password,NULL,port,NULL,0) == NULL){
            fprintf(stderr, "Failed to connect to port %d using database %s: %s\n",
                    port,
                    databases[i],
                    mysql_error(server));
            rval = 1;
            goto report;
        }

        if(i > 0 && mysql_real_query(server,"START SLAVE",strlen("START SLAVE")))
        {
            fprintf(stderr, "Failed to start slave in %d: %s.\n",
                    ports[i],
                    mysql_error(server));
        }
        mysql_close(server);
    }

    report:

    if(rval){	
        printf("\nTest failed: Errors during test run.\n");
    }
    free(host);
    free(username);
    free(password);
    return rval;
}
