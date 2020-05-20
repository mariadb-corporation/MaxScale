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
#include <maxbase/log.hh>

using namespace std;
namespace xml = cs::xml;

namespace
{

void print_usage_and_exit()
{
    cout << "usage: csxml xml-file ...\n"
         << "create_first ip manager\n"
         << "    Create multi-node config for first node.\n"
         << "\n"
         << "insert_b key value\n"
         << "    Unconditionally insert new key/value pair at beginning.\n"
         << "\n"
         << "insert_e key value\n"
         << "    Unconditionally insert new key/value pair at end.\n"
         << "\n"
         << "remove xpath-expr\n"
         << "    Remove key(s)\n"
         << "\n"
         << "reset\n"
         << "    Convert multi-node config to single-node config.\n"
         << "\n"
         << "scan\n"
         << "    Scan DB roots\n"
         << "\n"
         << "update_if xpath-expr new_value [if_value]\n"
         << "    Update value at path, optionally only if existing value matches specified value\n"
         << "\n"
         << "update_if_not xpath-expr new_value [if_value]\n"
         << "    Update value at path, optionally only if existing value does not match specified value\n"
         << "\n"
         << "upsert xpath-expr new_value\n"
         << "    Update value of matching key(s), or insert new value.\n"
         << endl;
    exit(EXIT_FAILURE);
}

bool create_first(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zIp = argv[0];
    const char* zManager = argv[1];

    xml::convert_to_first_multi_node(xml, zManager, zIp);
    return true;
}

bool insert_b(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zKey = argv[0];
    const char* zValue = argv[1];

    xml::insert(xml, zKey, zValue, xml::XmlLocation::AT_BEGINNING);
    return true;
}

bool insert_e(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zKey = argv[0];
    const char* zValue = argv[1];

    xml::insert(xml, zKey, zValue, xml::XmlLocation::AT_END);
    return true;
}

bool remove(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 1)
    {
        print_usage_and_exit();
    }

    const char* zXpath = argv[0];

    xml::remove(xml, zXpath);
    return true;
}

bool reset(xmlDoc& xml, int argc, char* argv[])
{
    xml::convert_to_single_node(xml);
    return true;
}

bool scan(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zAddress = argv[0];
    int nRoots = atoi(argv[1]);

    vector<int> dbroots;

    for (auto i = 1; i <= nRoots; ++i)
    {
        dbroots.push_back(i);
    }

    json_t* pOutput = json_object();

    switch (xml::update_dbroots(xml, zAddress, dbroots, pOutput))
    {
    case xml::DbRoots::NO_CHANGE:
        cout << "success: No change in dbroots." << endl;
        break;

    case xml::DbRoots::UPDATED:
        cout << "success: dbroots updated." << endl;
        break;

    case xml::DbRoots::ERROR:
        cout << "error: Could not update dbroots." << endl;
        break;
    }

    return true;
}

bool update_if(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zXpath = argv[0];
    const char* zNew_value = argv[1];
    const char* zIf_value = argc == 3 ? argv[2] : nullptr;

    xml::update_if(xml, zXpath, zNew_value, zIf_value);
    return true;
}

bool update_if_not(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zXpath = argv[0];
    const char* zNew_value = argv[1];
    const char* zIf_value = argc == 3 ? argv[2] : nullptr;

    xml::update_if_not(xml, zXpath, zNew_value, zIf_value);
    return true;
}

bool upsert(xmlDoc& xml, int argc, char* argv[])
{
    if (argc < 2)
    {
        print_usage_and_exit();
    }

    const char* zXpath = argv[0];
    const char* zValue = argv[1];

    xml::upsert(xml, zXpath, zValue);
    return true;
}


map<string, bool (*)(xmlDoc&, int, char**)> commands =
{
    { "create_first", &create_first },
    { "insert_b", &insert_b },
    { "insert_e", &insert_e },
    { "remove", &remove },
    { "reset", &reset },
    { "scan", &scan },
    { "update_if", &update_if },
    { "update_if_not", &update_if_not },
    { "upsert", &upsert }
};

}

int main(int argc, char* argv[])
{
    int rv = EXIT_FAILURE;

    mxb::Log log;

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
                if (command(*sDoc.get(), argc - 3, &argv[3]))
                {
                    xmlDocDump(stdout, sDoc.get());
                }
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


