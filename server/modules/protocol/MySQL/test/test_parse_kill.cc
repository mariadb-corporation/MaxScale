#include <maxscale/cdefs.h>

#include "../MySQLClient/mysql_client.cc"

#define NO_THREAD_ID 0

int test_one_query(const char *query, bool should_succeed, uint64_t expected_tid,
                   int expected_kt, std::string expected_user)
{
    char *query_copy = MXS_STRDUP_A(query);
    uint64_t result_tid = 1111111;
    kill_type_t result_kt = KT_QUERY;
    std::string user;

    /* If the parse fails, these should remain unchanged */
    if (!should_succeed)
    {
        result_tid = expected_tid;
        result_kt = (kill_type_t)expected_kt;
    }
    bool success = parse_kill_query(query_copy, &result_tid, &result_kt, &user);
    MXS_FREE(query_copy);

    if (success == should_succeed && result_tid == expected_tid &&
        result_kt == expected_kt && expected_user == user)
    {
        return 0;
    }
    else
    {
        printf("Result wrong on query: '%s'.\n", query);
        if (success != should_succeed)
        {
            printf("Expected success '%d', got '%d'.\n", should_succeed, success);
        }
        if (result_tid != expected_tid)
        {
            printf("Expected thread id '%" PRIu64 "', got '%" PRIu64 "'.\n", expected_tid, result_tid);
        }
        if (result_kt != expected_kt)
        {
            printf("Expected kill type '%u', got '%u'.\n", expected_kt, result_kt);
        }
        if (expected_user != user)
        {
            printf("Expected user '%s', got '%s'.\n", expected_user.c_str(), user.c_str());
        }
        printf("\n");
        return 1;
    }
}
typedef struct test_t
{
    const char *query;
    bool should_succeed;
    uint64_t correct_id;
    int correct_kt;
    const char* correct_user;
} test_t;

int main(int argc, char **argv)
{
    test_t tests[] =
    {
        {" kill ConNectioN 123  ",        true, 123, KT_CONNECTION},
        {"kIlL  coNNectioN 987654321  ;", true, 987654321, KT_CONNECTION},
        {" Ki5L CoNNectioN 987654321  ",  false, 0, KT_CONNECTION},
        {"1",                             false, 0, KT_CONNECTION},
        {"kILL 1",                        true, 1, KT_CONNECTION},
        {"\n\t kill \nQueRy 456",         true, 456, KT_QUERY},
        {"     A         kill 1;     ",   false, 0, KT_CONNECTION},
        {" kill connection 1A",           false, 0, KT_CONNECTION},
        {" kill connection 1 A ",         false, 0, KT_CONNECTION},
        {"kill query 7 ; select * ",      false, 0, KT_CONNECTION},
        {"KIll query 12345678901234567890", false, 0, KT_QUERY}, // 32-bit integer overflow
        {"KIll query   \t    \n    \t   21  \n \t   ", true, 21, KT_QUERY},
        {"KIll   \t    \n    \t   -6  \n \t   ",       false, 0, KT_CONNECTION},
        {"KIll 12345678901234567890123456  \n \t   ",  false, 0, KT_CONNECTION},
        {"kill ;", false, 0, KT_QUERY},
        {" kill ConNectioN 123 HARD",        false, 123, KT_CONNECTION},
        {" kill ConNectioN 123 SOFT",        false, 123, KT_CONNECTION},
        {" kill ConNectioN SOFT 123",        false, 123, KT_CONNECTION},
        {" kill  HARD ConNectioN 123",       true,  123, KT_CONNECTION | KT_HARD},
        {" kill  SOFT ConNectioN 123",       true,  123, KT_CONNECTION | KT_SOFT},
        {" kill  HARD 123",                  true,  123, KT_CONNECTION | KT_HARD},
        {" kill  SOFT 123",                  true,  123, KT_CONNECTION | KT_SOFT},
        {"KIll soft query 21 ",              true, 21, KT_QUERY | KT_SOFT},
        {"KIll query soft 21 ",              false, 21, KT_QUERY},
        {"KIll query user maxuser ",         true, NO_THREAD_ID, KT_QUERY, "maxuser"},
        {"KIll user query  maxuser ",        false, NO_THREAD_ID, KT_QUERY}
    };
    int result = 0;
    int arr_size = sizeof(tests) / sizeof(test_t);
    for (int i = 0; i < arr_size; i++)
    {
        const char *query = tests[i].query;
        bool should_succeed = tests[i].should_succeed;
        uint64_t expected_tid = tests[i].correct_id;
        int expected_kt = tests[i].correct_kt;
        std::string expected_user = tests[i].correct_user ? tests[i].correct_user : "";
        result += test_one_query(query, should_succeed, expected_tid,
                                 expected_kt, expected_user);
    }
    return result;
}
