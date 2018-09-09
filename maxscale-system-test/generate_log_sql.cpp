#include <iostream>
#include "testconnections.h"
#include "sql_t1.h"

using namespace std;


int main(int argc, char* argv[])
{
    char sql[1000000];
    create_insert_string(sql, 16, 0);

    printf("%s\n", sql);

    create_insert_string(sql, 256, 1);

    printf("%s\n", sql);

    create_insert_string(sql, 4096, 2);

    printf("%s\n", sql);

    create_insert_string(sql, 65536, 3);

    printf("%s\n", sql);

    exit(0);
}
