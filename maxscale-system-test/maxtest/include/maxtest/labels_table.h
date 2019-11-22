#pragma once

#include <string>

struct labels_table_t
{
    std::string test_label;
    std::string mdbci_label;

};

const labels_table_t labels_table [] __attribute__((unused)) =
{
    {"REPL_BACKEND", "REPL_BACKEND"},
    {"BIG_REPL_BACKEND", "BIG_REPL_BACKEND"},
    {"GALERA_BACKEND", "GALERA_BACKEND"},
    {"TWO_MAXSCALES", "SECOND_MAXSCALE"},
    {"COLUMNSTORE_BACKEND", "COLUMNSTORE_BACKEND"},
};

/**
 * @brief get_mdbci_lables Finds all MDBCI labels which are needed by test
 * Every test has a number of labels defined in the CMakeLists.txt,
 * some of these lables defines which nodes (virtual machines) are needed
 * for this particular test. Function finds such labels and forms labels string
 * in the 'mdbci up' command format
 * @param labels_string All lables from CMakeLists.txt
 * @return Labels string in the 'mdbci up' --labels parameter format
 */
std::string get_mdbci_lables(const char * labels_string);
