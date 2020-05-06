/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-04-23
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "columnstore.hh"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <getopt.h>
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

using namespace std;

namespace
{

void print_usage_and_exit()
{
    cout << "usage: csxml xml-file ...\n"
         << "insert key value\n"
         << "    Unconditionally insert new key/value pair.\n"
         << "\n"
         << "update xpath-expr new_value [if_value]\n"
         << "    Update value at path, optionally only if existing value matches specified value\n"
         << "\n"
         << "upsert xpath-expr new_value\n"
         << "    Update value of matching key(s), or insert new value.\n"
         << endl;
    exit(EXIT_FAILURE);
}

void insert(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zKey = argv[0];
    const char* zValue = argv[1];

    cs::insert(xml, zKey, zValue);
}

void replace(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zXpath = argv[0];
    const char* zNew_value = argv[1];
    const char* zIf_value = argc == 3 ? argv[2] : nullptr;

    cs::update_if(xml, zXpath, zNew_value, zIf_value);
}

void upsert(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zXpath = argv[0];
    const char* zValue = argv[1];

    cs::upsert(xml, zXpath, zValue);
}


map<string, void (*)(xmlDoc&, int, char**)> commands =
{
    { "insert", &insert },
    { "update", &replace },
    { "upsert", &upsert }
};

}

int main(int argc, char* argv[])
{
    int rv = EXIT_FAILURE;

    if (argc < 3)
    {
        print_usage_and_exit();
    }

    const char* zCmd = argv[1];
    const char* zFile = argv[2];

    auto it = commands.find(zCmd);

    if (it != commands.end())
    {
        auto command = it->second;
        ifstream in(zFile);

        if (in)
        {
            auto begin = std::istreambuf_iterator<char>(in);
            auto end = std::istreambuf_iterator<char>();

            string xml(begin, end);

            unique_ptr<xmlDoc> sDoc(xmlReadMemory(xml.c_str(), xml.length(), "columnstore.xml", NULL, 0));

            if (sDoc)
            {
                command(*sDoc.get(), argc - 3, &argv[3]);
                xmlDocDump(stdout, sDoc.get());
                rv = EXIT_SUCCESS;
            }
            else
            {
                cerr << "error: Could not parse document." << endl;
            }
        }
        else
        {
            cerr << "error: Could not open '" << zFile << "'." << endl;
        }
    }
    else
    {
        cerr << "error: Unknown command '" << zCmd << "'" << endl;
    }

    return rv;
}


