#include "testconnections.h"

/**
 *  Reads COM_SELECT and COM_INSERT variables from all nodes and stores into 'selects' and 'inserts'
 */
int get_global_status_allnodes(long int* selects, long int* inserts, Mariadb_nodes* nodes, int silent)
{
    int i;
    MYSQL_RES* res;
    MYSQL_ROW row;

    for (i = 0; i < nodes->N; i++)
    {
        if (nodes->nodes[i] != NULL)
        {

            if (mysql_query(nodes->nodes[i], "show global status like 'COM_SELECT';") != 0)
            {
                printf("Error: can't execute SQL-query\n");
                printf("%s\n", mysql_error(nodes->nodes[i]));
                return 1;
            }

            res = mysql_store_result(nodes->nodes[i]);
            if (res == NULL)
            {
                printf("Error: can't get the result description\n");
                return 1;
            }

            if (mysql_num_rows(res) > 0)
            {
                while ((row = mysql_fetch_row(res)) != NULL)
                {
                    if (silent == 0)
                    {
                        printf("Node %d COM_SELECT=%s\n", i, row[1]);
                    }
                    sscanf(row[1], "%ld", &selects[i]);
                }
            }

            mysql_free_result(res);
            while (mysql_next_result(nodes->nodes[i]) == 0)
            {
                res = mysql_store_result(nodes->nodes[i]);
                mysql_free_result(res);
            }

            if (mysql_query(nodes->nodes[i], "show global status like 'COM_INSERT';") != 0)
            {
                printf("Error: can't execute SQL-query\n");
            }

            res = mysql_store_result(nodes->nodes[i]);
            if (res == NULL)
            {
                printf("Error: can't get the result description\n");
            }

            if (mysql_num_rows(res) > 0)
            {
                while ((row = mysql_fetch_row(res)) != NULL)
                {
                    if (silent == 0)
                    {
                        printf("Node %d COM_INSERT=%s\n", i, row[1]);
                    }
                    sscanf(row[1], "%ld", &inserts[i]);
                }
            }

            mysql_free_result(res);
            while (mysql_next_result(nodes->nodes[i]) == 0)
            {
                res = mysql_store_result(nodes->nodes[i]);
                mysql_free_result(res);
            }
        }
        else
        {
            selects[i] = 0;
            inserts[i] = 0;
        }
    }
    return 0;
}

/**
 *  Prints difference in COM_SELECT and COM_INSERT
 */
int print_delta(long int* new_selects,
                long int* new_inserts,
                long int* selects,
                long int* inserts,
                int nodes_num)
{
    int i;
    for (i = 0; i < nodes_num; i++)
    {
        printf("COM_SELECT increase on node %d is %ld\n", i, new_selects[i] - selects[i]);
        printf("COM_INSERT increase on node %d is %ld\n", i, new_inserts[i] - inserts[i]);
    }
    return 0;
}
