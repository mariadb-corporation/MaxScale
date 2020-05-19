/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <iostream>
#include "columnstore.hh"

using namespace std;

namespace
{

const char ZSINGLE_NODE[] =
    "<Columnstore Version=\"V1.0.0\">"
    "  <DBRoot1>"
    "    <PreallocSpace>OFF</PreallocSpace>"
    "  </DBRoot1>"
    "  <ExeMgr1>"
    "    <IPAddr>127.0.0.1</IPAddr>"
    "    <Port>8601</Port>"
    "    <Module>pm1</Module>"
    "  </ExeMgr1>"
    "  <JobProc>"
    "    <IPAddr>0.0.0.0</IPAddr>"
    "    <Port>8602</Port>"
    "  </JobProc>"
    "  <ProcMgr>"
    "    <IPAddr>127.0.0.1</IPAddr>"
    "    <Port>8603</Port>"
    "  </ProcMgr>"
    "</Columnstore>";

const char ZFIRST_MULTI_NODE[] =
    "<Columnstore Version=\"V1.0.0\">"
    "  <ClusterManager>MaxScale</ClusterManager>"
    "  <DBRoot1>"
    "    <PreallocSpace>OFF</PreallocSpace>"
    "  </DBRoot1>"
    "  <ExeMgr1>"
    "    <IPAddr>123.45.67.89</IPAddr>"
    "    <Port>8601</Port>"
    "    <Module>pm1</Module>"
    "  </ExeMgr1>"
    "  <JobProc>"
    "    <IPAddr>0.0.0.0</IPAddr>"
    "    <Port>8602</Port>"
    "  </JobProc>"
    "  <ProcMgr>"
    "    <IPAddr>123.45.67.89</IPAddr>"
    "    <Port>8603</Port>"
    "  </ProcMgr>"
    "</Columnstore>";

string remove_whitespace(const string& in)
{
    int level = 0;
    string out;
    for (auto c : in)
    {
        if (c == '<')
        {
            out += c;
            ++level;
        }
        else if (c == '>')
        {
            out += c;
            --level;
        }
        else
        {
            if (level != 0 || !isspace(c))
            {
                out += c;
            }
        }
    }

    return out;
}

string dump_xml(xmlDoc& doc)
{
    xmlBuffer* pBuffer = xmlBufferCreate();
    xmlNodeDump(pBuffer, &doc, xmlDocGetRootElement(&doc), 0, 0);
    xmlChar* pConfig = xmlBufferDetach(pBuffer);
    const char* zConfig = reinterpret_cast<const char*>(pConfig);

    string config(zConfig);

    MXS_FREE(pConfig);
    xmlBufferFree(pBuffer);

    return remove_whitespace(config);
}

void complain(const char* zWhat, const string& expected, const string& obtained)
{
    cout << zWhat << endl;
    cout << "EXPECTED\n"
         << expected
         << endl;
    cout << "OBTAINED\n"
         << obtained
         << endl;
}

int test_convert_to_first_multi_node()
{
    int rv = 0;
    unique_ptr<xmlDoc> sDoc(xmlReadMemory(ZSINGLE_NODE, sizeof(ZSINGLE_NODE) - 1, "cs.xml", NULL, 0));

    const char IP[] = "123.45.67.89";
    const char MANAGER[] = "MaxScale";

    cs::xml::convert_to_first_multi_node(*sDoc.get(), MANAGER, IP);

    string expected = remove_whitespace(ZFIRST_MULTI_NODE);
    string obtained = dump_xml(*sDoc.get());

    if (expected == obtained)
    {
        cout << "Single -> Multi Conversion ok" << endl;

        cs::xml::convert_to_single_node(*sDoc.get());

        expected = remove_whitespace(ZSINGLE_NODE);
        obtained = dump_xml(*sDoc.get());

        if (expected == obtained)
        {
            cout << "Multi -> Single Conversion ok" << endl;
        }
        else
        {
            complain("Multi -> Single Conversion NOT ok.", expected, obtained);
            rv = 1;
        }
    }
    else
    {
        complain("Single -> Multi Conversion NOT ok.", expected, obtained);
        rv = 1;
    }

    return rv;
}

}

int main()
{
    int rv = 0;
    rv += test_convert_to_first_multi_node();
    return rv;
}
