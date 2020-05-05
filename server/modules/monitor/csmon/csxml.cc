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
#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

using namespace std;

namespace
{

void print_usage_and_exit(const char* zProg)
{
    cout << "usage: " << zProg << " xml-file xpath-expr new_value [if_value]" << endl;
}

int replace_if(const string& xml, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    int rv = EXIT_FAILURE;
    unique_ptr<xmlDoc> sDoc(xmlReadMemory(xml.c_str(), xml.length(), "columnstore.xml", NULL, 0));

    if (sDoc)
    {
        cs::replace_if(*sDoc.get(), zXpath, zNew_value, zIf_value);
        xmlDocDump(stdout, sDoc.get());
    }
    else
    {
        cerr << "error: Could not parse document." << endl;
    }

    return rv;
}

}

int main(int argc, char* argv[])
{
    int rv = EXIT_FAILURE;

    if (argc < 4)
    {
        print_usage_and_exit(argv[0]);
        exit(rv);
    }

    const char* zFile = argv[1];
    const char* zXpath = argv[2];
    const char* zNew_value = argv[3];
    const char* zIf_value = argc == 5 ? argv[4] : nullptr;

    ifstream in(zFile);

    if (in)
    {
        auto begin = std::istreambuf_iterator<char>(in);
        auto end = std::istreambuf_iterator<char>();

        string s(begin, end);

        rv = replace_if(s, zXpath, zNew_value, zIf_value);
    }
    else
    {
        cerr << "error: Could not open '" << zFile << "'." << endl;
    }

    return rv;
}


