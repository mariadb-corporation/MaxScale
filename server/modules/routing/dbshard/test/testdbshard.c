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
    

	MYSQL* server;
    MYSQL_RES *result,*shdres;
    MYSQL_ROW row;
	char *host = NULL,*username = NULL, *password = NULL;
    char query[2048];
	unsigned int port,errnum;
    unsigned long *lengths;
	int rval;

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

    printf("Connecting to %s:%d as %s/%s\n",host,port,username,password);
    
	if((server = mysql_init(NULL)) == NULL){
        fprintf(stderr,"Error : Initialization of MySQL client failed.\n");
	    rval = 1;
        goto report;
	}

	if(mysql_real_connect(server,host,username,password,NULL,port,NULL,0) == NULL){
		fprintf(stderr, "Failed to connect to database: %s\n",
				mysql_error(server));
        rval = 1;
		goto report;
	}

    if(mysql_real_query(server,
                        "SET @test=123",
                        strlen("SET @test=123")))
    {
        fprintf(stderr, "Failed to set session variable: %s.\n",
                mysql_error(server));
        rval = 1;
        goto report;
    }
    
    if((result = mysql_list_dbs(server,NULL)) == NULL){
        fprintf(stderr, "Failed to query databases: %s\n",
                mysql_error(server));
        rval = 1;
        goto report;
    }

    if(mysql_field_count(server) != 1)
    {
        fprintf(stderr, "SHOW DATABASES returned an unexpected result.\n");
        rval = 1;
        goto report;
    }

    while((row = mysql_fetch_row(result)))
    {
        char* dbname = strdup(row[0]);
        printf("Testing database %-32s",dbname);
        sprintf(query,"DROP TABLE IF EXISTS %s.t1",dbname);

        if(mysql_real_query(server,(const char*)query,strlen(query)))
        {
            errnum = mysql_errno(server);
            
            if(errnum != ER_DBACCESS_DENIED_ERROR &&
               errnum != ER_ACCESS_DENIED_ERROR)
            {
                fprintf(stderr, "DROP TABLE failed in %s: %d: %s.\n",dbname,mysql_errno(server),mysql_error(server));
            }
            printf("NO PERMISSION\n");
            continue;
        }

        
        sprintf(query,"CREATE TABLE %s.t1 (id INT)",dbname);

        if(mysql_real_query(server,(const char*)query,strlen(query)))
        {
            errnum = mysql_errno(server);
            if( errnum == ER_TABLEACCESS_DENIED_ERROR)
            {
                sprintf(query,"DROP TABLE IF EXISTS %s.t1",dbname);
                mysql_real_query(server,(const char*)query,strlen(query));
                printf("NO PERMISSION\n");
                continue;
            }
                
            fprintf(stderr, "CREATE TABLE failed in %s: %d: %s.\n",
                    dbname,mysql_errno(server),mysql_error(server));
            rval = 1;
            goto report;
        }
        
        sprintf(query,"INSERT INTO %s.t1 VALUES (1);",dbname);
        
        if(mysql_real_query(server,(const char*)query,strlen(query)))
        {
            fprintf(stderr, "Query to server failed: %d: %s.\n",
                    mysql_errno(server),mysql_error(server));
            rval = 1;
            goto report;
        }

        sprintf(query,"SELECT ID FROM %s.t1 UNION SELECT @test",dbname);

        if(mysql_real_query(server,(const char*)query,strlen(query)))
        {
            fprintf(stderr, "Query to server failed: %d: %s.\n",
                    mysql_errno(server),mysql_error(server));
            rval = 1;
            goto report;
        }
                
        if((shdres = mysql_store_result(server)) == NULL)
        {
            fprintf(stderr, "Failed to fetch result set: %d: %s\n",
                    mysql_errno(server),mysql_error(server));
            rval = 1;
            goto report;
        }

        if(mysql_field_count(server) != 1)
        {
            fprintf(stderr, "Returned field count value did not match the expected value.\n");
            rval = 1;
            goto report;

        }

        /**Fetch the two rows, the inserted value and the session variable*/        
        if(mysql_fetch_row(shdres) == NULL ||
           (row = mysql_fetch_row(shdres)) == NULL )
        {
            fprintf(stderr, "Number of returned rows did not match the expected value.\n");
            rval = 1;
            goto report;
        }
        
        if((lengths = mysql_fetch_lengths(shdres)) == NULL)
        {
            fprintf(stderr, "Failed to retrieve row lengths: %d: %s.\n",
                    mysql_errno(server),mysql_error(server));
            rval = 1;
            goto report;
        }
        if(lengths[0] != 3 || strcmp(row[0],"123"))
        {
            
            rval = 1;
            printf(" FAILED\n");
            printf( "Reason: Session variable was %s instead of \"123\".\n",row[0]);
        }
        else
        {
            printf("OK\n");
        }
        

        sprintf(query,"DROP TABLE %s.t1;",dbname);
        
        if(mysql_real_query(server,(const char*)query,strlen(query)))
        {
            fprintf(stderr, "Query to server failed: %s.\n",mysql_error(server));
            rval = 1;
            goto report;
        }
        
        free(dbname);
        mysql_free_result(shdres);        
    }
    mysql_free_result(result);
    
	mysql_close(server);			

	report:

	if(rval){	
		printf("\nTest failed: Errors during test run.\n");
	}
	free(host);
    free(username);
    free(password);
	return rval;
}
