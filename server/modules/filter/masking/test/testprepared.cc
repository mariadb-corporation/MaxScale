#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <mysql.h>

using namespace std;

namespace
{

void print_usage_and_exit(ostream& out, const char* zName)
{
    out << "usage: " << zName << "[-h host] [-P port] [-u user] [-p password] -s statement" << endl;
    exit(EXIT_FAILURE);
}

int test_prepared(MYSQL* pMysql, const char* zStatement)
{
    int rc = EXIT_FAILURE;
    MYSQL_STMT* pStmt = mysql_stmt_init(pMysql);

    if (pStmt)
    {
        if (mysql_stmt_prepare(pStmt, zStatement, strlen(zStatement)) == 0)
        {
            if (mysql_stmt_execute(pStmt) == 0)
            {
                int nColumns = mysql_stmt_field_count(pStmt);
                cout << "Columns: " << nColumns << endl;

                typedef char Buffer[256];

                Buffer buffers[nColumns];
                MYSQL_BIND bind[nColumns];
                unsigned long lengths[nColumns];
                my_bool nulls[nColumns];

                for (int i = 0; i < nColumns; ++i)
                {
                    memset(&bind[i], 0, sizeof(MYSQL_BIND));
                    bind[i].buffer_type = MYSQL_TYPE_STRING;
                    bind[i].buffer = buffers[i];
                    bind[i].buffer_length = 256;
                    bind[i].length = &lengths[i];
                    bind[i].is_null = &nulls[i];
                }

                if (mysql_stmt_bind_result(pStmt, bind) == 0)
                {
                    while (mysql_stmt_fetch(pStmt) == 0)
                    {
                        for (int j = 0; j < nColumns; ++j)
                        {
                            if (nulls[j])
                            {
                                cout << "NULL";
                            }
                            else
                            {
                                cout.write(buffers[j], lengths[j]);
                            }

                            if (j < nColumns - 1)
                            {
                                cout << ", ";
                            }
                        }

                        cout << endl;
                    }
                }
                else
                {
                    cerr << "error (mysql_stmt_bind_result): " << mysql_stmt_error(pStmt) << endl;
                }
            }
            else
            {
                cerr << "error (mysql_stmt_execute): " << mysql_stmt_error(pStmt) << endl;
            }
        }
        else
        {
            cerr << "error (mysql_stmt_prepare): " << mysql_stmt_error(pStmt) << endl;
        }

        mysql_stmt_close(pStmt);
    }
    else
    {
        cerr << "error (mysql_stmt_init): " << mysql_error(pMysql) << endl;
    }

    return rc;
}

}

int main(int argc, char* argv[])
{
    int rc = EXIT_FAILURE;

    const char* zHost = "127.0.0.1";
    int port = 3306;
    const char* zUser = getenv("USER");
    const char* zPassword = NULL;
    const char* zStatement = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "h:P:u:p:s:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            zHost = optarg;
            break;

        case 'p':
            zPassword = optarg;
            break;

        case 'u':
            zUser = optarg;
            break;

        case 'P':
            port = atoi(optarg);
            break;

        case 's':
            zStatement = optarg;
            break;

        default:
            print_usage_and_exit(cerr, argv[0]);
        }
    }

    if (!zStatement)
    {
        print_usage_and_exit(cerr, argv[0]);
    }

    MYSQL* pMysql = mysql_init(NULL);

    if (pMysql)
    {
        if (mysql_real_connect(pMysql, zHost, zUser, zPassword, NULL, port, NULL, 0))
        {
            rc = test_prepared(pMysql, zStatement);
        }
        else
        {
            cerr << "error: " << mysql_error(pMysql) << endl;
        }

        mysql_close(pMysql);
        pMysql = NULL;
    }
    else
    {
        cerr << "error: " << mysql_error(NULL) << endl;
    }

    return rc;
}
