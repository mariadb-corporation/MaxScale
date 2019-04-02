#include <cstring>
#include <string>
#include <stdio.h>
#include "labels_table.h"

std::string get_mdbci_lables(const char *labels_string)
{
    std::string mdbci_labels("MAXSCALE");

    for (size_t i = 0; i < sizeof(labels_table) / sizeof(labels_table_t); i++)
    {
        printf("%lu\t %s\n", i, labels_table[i].test_label);
        if (strstr(labels_string, labels_table[i].test_label))
        {
            mdbci_labels += "," + std::string(labels_table[i].mdbci_label);
        }
    }
    printf("mdbci labels %s\n", mdbci_labels.c_str());
    return mdbci_labels;
}
