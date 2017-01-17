#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <mysql.h>

#include "../../utils/skygw_utils.h"
#include "../query_classifier.h"

static char datadir[1024] = "";
static char mysqldir[1024] = "";

static char* server_options[] =
{
    "MariaDB Corporation MaxScale",
    "--datadir=",
    "--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

static char* server_groups[] =
{
    "embedded",
    "server",
    "server",
    NULL
};

static void slcursor_add_case(
    slist_cursor_t* c,
    void*           data)
{
    slcursor_add_data(c, data);
}


typedef struct query_test_st query_test_t;

struct query_test_st
{
    skygw_chk_t        qt_chk_top;
    const char*        qt_query_str;
    skygw_query_type_t qt_query_type;
    skygw_query_type_t qt_result_type;
    bool               qt_should_fail;
    bool               qt_exec_also_in_server;
    skygw_chk_t        qt_chk_tail;
};




static query_test_t* query_test_init(
    const char*        query_str,
    skygw_query_type_t query_type,
    bool               case_should_fail,
    bool               exec_also_in_server)
{
    query_test_t* qtest;

    qtest = (query_test_t *)calloc(1, sizeof(query_test_t));
    ss_dassert(qtest != NULL);
    qtest->qt_chk_top = CHK_NUM_QUERY_TEST;
    qtest->qt_chk_tail = CHK_NUM_QUERY_TEST;
    qtest->qt_query_str = query_str;
    qtest->qt_query_type = query_type;
    qtest->qt_should_fail = case_should_fail;
    qtest->qt_exec_also_in_server = exec_also_in_server;
    return qtest;
}

const char* query_test_get_querystr(
    query_test_t* qt)
{
    CHK_QUERY_TEST(qt);
    return qt->qt_query_str;
}

static skygw_query_type_t query_test_get_query_type(
    query_test_t* qt)
{
    CHK_QUERY_TEST(qt);
    return qt->qt_query_type;
}

static skygw_query_type_t query_test_get_result_type(
    query_test_t* qt)
{
    CHK_QUERY_TEST(qt);
    return qt->qt_result_type;
}


static bool query_test_types_match(
    query_test_t* qt)
{
    CHK_QUERY_TEST(qt);
    return (qt->qt_query_type == qt->qt_result_type);
}

static bool query_test_exec_also_in_server(
    query_test_t* qt)
{
    CHK_QUERY_TEST(qt);
    return (qt->qt_exec_also_in_server);
}

static query_test_t* slcursor_get_case(
    slist_cursor_t* c)
{
    return (query_test_t*)slcursor_get_data(c);
}

int main(int argc, char** argv)
{
    slist_cursor_t*    c;
    const char*        q;
    query_test_t*      qtest;
    bool               succp;
    bool               failp = true;
    unsigned int       f = 0;
    int                nsucc = 0;
    int                nfail = 0;
    int                rc = 0;
    MYSQL*             mysql;
    char*              workingdir;
    char               ddoption[1024];

    ss_dfprintf(stderr, ">> testmain\n");
    c = slist_init();

    /** Test some functions */
    q = "SELECT MY_UDF('Hello')";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));

    /** This could be QUERY_TYPE_LOCAL_READ */
    q = "SELECT repeat('a', 1024)";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_READ, false, true));

    /** This could be QUERY_TYPE_LOCAL_READ */
    q = "SELECT soundex('Hello')";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_READ, false, true));

    q = "SELECT ssoundexx('Hello')";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));

    /** This could be QUERY_TYPE_LOCAL_READ */
    q = "SELECT now()";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_READ, false, true));

    /** This could be QUERY_TYPE_LOCAL_READ */
    q = "SELECT rand()";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_READ, false, true));

    q = "SELECT rand(234), MY_UDF('Hello'), soundex('Hello')";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));


    /** Read-only SELECTs */
    q = "SELECT user from mysql.user";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_READ, false, true));

    q = "select tt1.id, tt2.id from t1 tt1, t2 tt2 where tt1.name is "
        "not null and tt2.name is not null";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_READ, false, false));

    /** SELECT ..INTO clauses > session updates */
    q = "SELECT user from mysql.user INTO DUMPFILE '/tmp/dump1'";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, false));

    q = "SELECT user INTO DUMPFILE '/tmp/dump2 ' from mysql.user";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, false));

    q = "SELECT user from mysql.user INTO OUTFILE '/tmp/out1'";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, false));

    /** Database and table name must be separated by a dot */
    q = "SELECT user INTO OUTFILE '/tmp/out2 ' from mysql-user";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, true, false));

    /** Database and table name must be separated by a dot */
    q = "SELECT user INTO OUTFILE '/tmp/out2 ' from mysql_foo_user";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, false));

    q = "SELECT user FROM mysql.user limit 1 INTO @local_variable";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, false));

    q = "SELECT user INTO @local_variable FROM mysql.user limit 1";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, false));

    q = "SELECT non_existent_attr INTO @d FROM non_existent_table";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, false));

    q = "select * from table1 "
        "where table1.field IN "
        "(select * from table1a union select * from table1b) union "
        "select * from table2 where table2.field = "
        "(select (select f1 from table2a where table2a.f2 = table2b.f3) "
        "from table2b where table2b.f1 = table2.f2) union "
        "select * from table3";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_READ, false, true));

    /** RENAME TABLEs */
    q = "RENAME TABLE T1 to T2";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, false));


    /** INSERTs */
    q = "INSERT INTO T1 (SELECT * FROM T2)";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));

    q = "INSERT INTO T1 VALUES(2, 'foo', 'toomanyattributes')";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));

    q = "INSERT INTO T2 VALUES(1, 'sthrgey')";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, false));

    q = "INSERT INTO T2 VALUES(8, 'ergstrhe')";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, false));

    q = "INSERT INTO T2 VALUES(9, NULL)";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, false));


    /** Ok, delimeter is client-side parameter which shouldn't be handled
     * on server side.
     */
    q = "delimiter //";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, true, true));

    /** SETs, USEs > Session updates */
    q = "SET @a=1";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, true));

    q = "USE TEST";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, false));


    /** Object creation statements */
    q = "create procedure si (out param1 int) \nbegin select count(*) "
        "into param1 from t1; \nend";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));

    q = "CREATE TABLE T1 (id integer primary key, name varchar(10))";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));

    q = "DROP TABLE T1";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, false));

    q = "ALTER TABLE T1 ADD COLUMN WHYME INTEGER NOT NULL";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, false));

    q = "TRUNCATE TABLE T1";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, false));

    q = "DROP SERVER IF EXISTS VICTIMSRV";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, true));

    q = "CREATE USER FOO IDENTIFIED BY 'BAR'";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));

    q = "OPTIMIZE NO_WRITE_TO_BINLOG TABLE T1";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));

    q = "SELECT NOW();CREATE TABLE T1 (ID INTEGER);"
        "SET sql_log_bin=0;CREATE TABLE T2 (ID INTEGER)";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_WRITE, false, true));


    /** Setting database makes this SESSION_WRITE */
    q = "USE TEST;CREATE TABLE T1 (ID INTEGER);"
        "SET sql_log_bin=0;CREATE TABLE T2 (ID INTEGER)";
    slcursor_add_case(
        c,
        query_test_init(q, QUERY_TYPE_SESSION_WRITE, false, true));

    /**
     * Init libmysqld.
     */
    workingdir = getenv("PWD");

    if (workingdir == NULL)
    {
        fprintf(stderr,
                "Failed to resolve the working directory, $PWD is not "
                "set.\n");
        ss_dassert(workingdir != NULL);
    }
    else if (access(workingdir, R_OK) != 0)
    {
        char errbuf[STRERROR_BUFLEN];
        fprintf(stderr,
                "Failed to access the working directory due %d, %s\n",
                errno,
                strerror_r(errno, errbuf, sizeof(errbuf)));
        ss_dassert(false);
    }
    else
    {
        char** so = server_options;
        snprintf(datadir, 1023, "%s/data", workingdir);
        mkdir(datadir, 0777);
        snprintf(ddoption, 1023, "--datadir=%s", datadir);

        while (strncmp(*so++, "--datadir=", 10) != 0) ;

        if (*so == NULL)
        {
            fprintf(stderr, "Failed to find datadir option.\n");
            ss_dassert(*so != NULL);
        }
        *so = ddoption;

        snprintf(mysqldir, 1023, "%s/mysql", workingdir);
        setenv("MYSQL_HOME", mysqldir, 1);
    }
    failp = mysql_library_init(num_elements, server_options, server_groups);

    if (failp)
    {
        MYSQL* mysql = mysql_init(NULL);
        ss_dassert(mysql != NULL);
        fprintf(stderr,
                "mysql_init failed, %d : %s\n",
                mysql_errno(mysql),
                mysql_error(mysql));
        ss_dassert(!failp);
    }

    fprintf(stderr,
            "\nExecuting selected cases in "
            "skygw_query_classifier_get_type :\n\n");
    /**
     * Set cursor to the beginning, scan through the list and execute
     * test cases.
     */
    succp = slcursor_move_to_begin(c);

    while (succp)
    {
        qtest = slcursor_get_case(c);
        qtest->qt_result_type =
            skygw_query_classifier_get_type(qtest->qt_query_str, f,
                                            &mysql);
        succp = slcursor_step_ahead(c);
    }
    /**
     * Scan through test results and compare them against expected
     * results.
     */
    succp = slcursor_move_to_begin(c);
    fprintf(stderr, "\nScanning through the results :\n\n");

    while (succp)
    {
        qtest = slcursor_get_case(c);

        if (!query_test_types_match(qtest))
        {
            nfail += 1;
            ss_dfprintf(stderr,
                        "* Failed: \"%s\" -> %s (Expected %s)\n",
                        query_test_get_querystr(qtest),
                        STRQTYPE(query_test_get_result_type(qtest)),
                        STRQTYPE(query_test_get_query_type(qtest)));
        }
        else
        {
            nsucc += 1;
            ss_dfprintf(stderr,
                        "Succeed\t: \"%s\" -> %s\n",
                        query_test_get_querystr(qtest),
                        STRQTYPE(query_test_get_query_type(qtest)));
        }
        succp = slcursor_step_ahead(c);
    }
    fprintf(stderr,
            "------------------------------------------\n"
            "Tests in total %d, SUCCEED %d, FAILED %d\n",
            nsucc + nfail,
            nsucc,
            nfail);

    /**
     * Scan test results and re-execute those which are marked to be
     * executed also in the server. This serves mostly debugging purposes.
     */
    succp = slcursor_move_to_begin(c);
    mysql = mysql_init(NULL);

    if (mysql == NULL)
    {
        fprintf(stderr, "mysql_init failed\n");
        ss_dassert(mysql != NULL);
    }

    mysql_options(mysql,
                  MYSQL_READ_DEFAULT_GROUP,
                  "libmysqld_client");
    mysql_options(mysql, MYSQL_OPT_USE_EMBEDDED_CONNECTION, NULL);
    mysql_options(mysql, MYSQL_OPT_USE_EMBEDDED_CONNECTION, NULL);

    mysql = mysql_real_connect(mysql,
                               NULL,
                               "skygw",
                               "skygw",
                               NULL,
                               0,
                               NULL,
                               CLIENT_MULTI_STATEMENTS);

    if (mysql == NULL)
    {
        fprintf(stderr, "mysql_real_connect failed\n");
        ss_dassert(mysql != NULL);
    }

    fprintf(stderr,
            "\nRe-execution of selected cases in Embedded server :\n\n");

    while (succp)
    {
        qtest = slcursor_get_case(c);

        if (query_test_exec_also_in_server(qtest))
        {
            MYSQL_RES*  results;
            MYSQL_ROW   record;
            const char* query_str;

            query_str = query_test_get_querystr(qtest);
            failp = mysql_query(mysql, query_str);

            if (failp)
            {
                ss_dfprintf(stderr,
                            "* Failed: \"%s\" -> %d : %s\n",
                            query_str,
                            mysql_errno(mysql),
                            mysql_error(mysql));
            }
            else
            {
                ss_dfprintf(stderr,
                            "Succeed\t: \"%s\"\n",
                            query_str);
                results = mysql_store_result(mysql);

                if (results != NULL)
                {

                    while ((record = mysql_fetch_row(results)))
                    {
                        while (record != NULL && *record != NULL)
                        {
                            ss_dfprintf(stderr, "%s ", *record);
                            record++;
                        }
                        ss_dfprintf(stderr, "\n");
                    }
                    mysql_free_result(results);
                }
            }
        }
        succp = slcursor_step_ahead(c);

    }
    slist_done(c);
    fprintf(stderr, "------------------------------------------\n");

return_with_handle:
    mysql_close(mysql);
    mysql_thread_end();
    mysql_library_end();

return_without_server:
    ss_dfprintf(stderr, "\n<< testmain\n");
    fflush(stderr);
    return rc;
}
