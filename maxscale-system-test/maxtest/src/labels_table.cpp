#include <cstring>
#include <string>
#include <stdio.h>
#include "labels_table.h"
#include "testconnections.h"

std::string get_mdbci_lables(const char *labels_string)
{
    std::string mdbci_labels("MAXSCALE");
    for (size_t i = 0; i < sizeof(labels_table) / sizeof(labels_table_t); i++)
    {
        std::string test_label = std::string(";") + labels_table[i].test_label;
        if (strstr(labels_string, test_label.c_str()))
        {
            mdbci_labels += "," + labels_table[i].mdbci_label;
        }
    }

    if (TestConnections::verbose)
    {
        printf("mdbci labels %s\n", mdbci_labels.c_str());
    }
    return mdbci_labels;
}
