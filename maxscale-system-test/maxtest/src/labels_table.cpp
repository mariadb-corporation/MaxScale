#include <cstdio>
#include "testconnections.h"

namespace
{
struct labels_table_t
{
    std::string test_label;
    std::string mdbci_label;
};

const labels_table_t labels_table[] =
{
    {"REPL_BACKEND", "REPL_BACKEND"},
    {"BIG_REPL_BACKEND", "BIG_REPL_BACKEND"},
    {"GALERA_BACKEND", "GALERA_BACKEND"},
    {"TWO_MAXSCALES", "SECOND_MAXSCALE"},
    {"COLUMNSTORE_BACKEND", "COLUMNSTORE_BACKEND"},
    {"CLUSTRIX_BACKEND", "CLUSTRIX_BACKEND"},
};
}
/**
 * Generate MDBCI labels required by test. Every test has a number of labels defined in CMakeLists.txt.
 * Some of these labels define which nodes (virtual machines) are needed for this particular test.
 * This function generates the equivalent labels for the 'mdbci up'-command.
 */
void TestConnections::set_mdbci_labels()
{
    std::string mdbci_labels_str("MAXSCALE");
    for (size_t i = 0; i < sizeof(labels_table) / sizeof(labels_table_t); i++)
    {
        std::string test_label = ";" + labels_table[i].test_label;
        if (m_labels.find(test_label) != std::string::npos)
        {
            mdbci_labels_str += "," + labels_table[i].mdbci_label;
        }
    }

    if (TestConnections::verbose)
    {
        printf("mdbci labels %s\n", mdbci_labels_str.c_str());
    }
    m_mdbci_labels = mdbci_labels_str;
}

/**
 * Check if label is part of current test labels.
 *
 * @param labels String with all labels of the test
 * @param label Labels to find
 * @return true if label present
 */
bool TestConnections::has_label(std::string labels, std::string label)
{
    std::string labels_ext = ";" + labels + ";";
    std::string label_ext = std::string(";") + label + std::string(";");
    return (labels_ext.find(label_ext, 0) != std::string::npos);
}
