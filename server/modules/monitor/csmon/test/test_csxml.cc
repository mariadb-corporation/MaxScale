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
#include <libxml/xpath.h>
#include <maxbase/log.hh>

using namespace std;
namespace xml = cs::xml;

namespace
{

unique_ptr<xmlDoc> compile_xml(const char* zXml);

bool equal(xmlNode& lhs, xmlNode& rhs);
bool equal(xmlDoc& lhs, xmlDoc& rhs);
bool equal(const unique_ptr<xmlDoc>& sLhs, const unique_ptr<xmlDoc>& sRhs);

string dump(const std::unique_ptr<xmlDoc>& sDoc)
{
    return mxb::xml::dump(*sDoc.get());
}

}


namespace
{

const char ZSINGLE_NODE[] = R"(
<Columnstore Version="V1.0.0">
  <DBRoot1>
    <PreallocSpace>OFF</PreallocSpace>
  </DBRoot1>
  <ExeMgr1>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8601</Port>
    <Module>pm1</Module>
  </ExeMgr1>
  <JobProc>
    <IPAddr>0.0.0.0</IPAddr>
    <Port>8602</Port>
  </JobProc>
  <ProcMgr>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8603</Port>
  </ProcMgr>
</Columnstore>)";

const char ZFIRST_MULTI_NODE[] = R"(
<Columnstore Version="V1.0.0">
  <ClusterManager>10.11.12.13</ClusterManager>
  <DBRoot1>
    <PreallocSpace>OFF</PreallocSpace>
  </DBRoot1>
  <ExeMgr1>
    <IPAddr>198.168.0.1</IPAddr>
    <Port>8601</Port>
    <Module>pm1</Module>
  </ExeMgr1>
  <JobProc>
    <IPAddr>0.0.0.0</IPAddr>
    <Port>8602</Port>
  </JobProc>
  <ProcMgr>
    <IPAddr>198.168.0.1</IPAddr>
    <Port>8603</Port>
  </ProcMgr>
</Columnstore>)";

int test_convert_to_first_multi_node()
{
    int rv = 0;
    unique_ptr<xmlDoc> sDoc = compile_xml(ZSINGLE_NODE);

    const char IP[] = "198.168.0.1";
    const char MANAGER[] = "10.11.12.13";

    xml::convert_to_first_multi_node(*sDoc.get(), MANAGER, IP);

    unique_ptr<xmlDoc> sExpected = compile_xml(ZFIRST_MULTI_NODE);

    if (equal(sExpected, sDoc))
    {
        cout << "Single -> Multi Conversion ok" << endl;

        xml::convert_to_single_node(*sDoc.get());

        sExpected = compile_xml(ZSINGLE_NODE);

        if (equal(sExpected, sDoc))
        {
            cout << "Multi -> Single Conversion ok" << endl;
        }
        else
        {
            cout << "Multi -> Single Conversion NOT ok." << endl;
            rv = 1;
        }
    }
    else
    {
        cout << "Single -> Multi Conversion NOT ok." << endl;
        rv = 1;
    }

    return rv;
}

}

namespace
{

const char ZSCAN_1_2[] = R"(
<Columnstore Version="V1.0.0">
  <SystemConfig>
    <DBRootCount>2</DBRootCount>
    <DBRoot1>/var/lib/columnstore/data1</DBRoot1>
    <DBRoot2>/var/lib/columnstore/data2</DBRoot2>
  </SystemConfig>
  <SystemModuleConfig>
    <ModuleIPAddr1-1-3>192.168.0.1</ModuleIPAddr1-1-3>
    <ModuleDBRootCount1-3>1</ModuleDBRootCount1-3>
    <ModuleDBRootID1-1-3>1</ModuleDBRootID1-1-3>
    <ModuleIPAddr2-1-3>192.168.0.2</ModuleIPAddr2-1-3>
    <ModuleDBRootCount2-3>1</ModuleDBRootCount2-3>
    <ModuleDBRootID2-1-3>2</ModuleDBRootID2-1-3>
  </SystemModuleConfig>
</Columnstore>
)";

const char ZSCAN_1_2_3[] = R"(
<Columnstore Version="V1.0.0">
  <SystemConfig>
    <DBRootCount>3</DBRootCount>
    <DBRoot1>/var/lib/columnstore/data1</DBRoot1>
    <DBRoot2>/var/lib/columnstore/data2</DBRoot2>
    <DBRoot3>/var/lib/columnstore/data3</DBRoot3>
  </SystemConfig>
  <SystemModuleConfig>
    <ModuleIPAddr1-1-3>192.168.0.1</ModuleIPAddr1-1-3>
    <ModuleDBRootCount1-3>2</ModuleDBRootCount1-3>
    <ModuleDBRootID1-1-3>1</ModuleDBRootID1-1-3>
    <ModuleDBRootID1-2-3>3</ModuleDBRootID1-2-3>
    <ModuleIPAddr2-1-3>192.168.0.2</ModuleIPAddr2-1-3>
    <ModuleDBRootCount2-3>1</ModuleDBRootCount2-3>
    <ModuleDBRootID2-1-3>2</ModuleDBRootID2-1-3>
  </SystemModuleConfig>
</Columnstore>
)";

const char ZSCAN_1_2_3_4[] = R"(
<Columnstore Version="V1.0.0">
  <SystemConfig>
    <DBRootCount>4</DBRootCount>
    <DBRoot1>/var/lib/columnstore/data1</DBRoot1>
    <DBRoot2>/var/lib/columnstore/data2</DBRoot2>
    <DBRoot3>/var/lib/columnstore/data3</DBRoot3>
    <DBRoot4>/var/lib/columnstore/data4</DBRoot4>
  </SystemConfig>
  <SystemModuleConfig>
    <ModuleIPAddr1-1-3>192.168.0.1</ModuleIPAddr1-1-3>
    <ModuleDBRootCount1-3>2</ModuleDBRootCount1-3>
    <ModuleDBRootID1-1-3>1</ModuleDBRootID1-1-3>
    <ModuleDBRootID1-2-3>3</ModuleDBRootID1-2-3>
    <ModuleIPAddr2-1-3>192.168.0.2</ModuleIPAddr2-1-3>
    <ModuleDBRootCount2-3>2</ModuleDBRootCount2-3>
    <ModuleDBRootID2-1-3>2</ModuleDBRootID2-1-3>
    <ModuleDBRootID2-2-3>4</ModuleDBRootID2-2-3>
  </SystemModuleConfig>
</Columnstore>
)";

const char ZSCAN_1_2_4[] = R"(
<Columnstore Version="V1.0.0">
  <SystemConfig>
    <DBRootCount>3</DBRootCount>
    <DBRoot1>/var/lib/columnstore/data1</DBRoot1>
    <DBRoot2>/var/lib/columnstore/data2</DBRoot2>
    <DBRoot4>/var/lib/columnstore/data4</DBRoot4>
  </SystemConfig>
  <SystemModuleConfig>
    <ModuleIPAddr1-1-3>192.168.0.1</ModuleIPAddr1-1-3>
    <ModuleDBRootCount1-3>1</ModuleDBRootCount1-3>
    <ModuleDBRootID1-1-3>1</ModuleDBRootID1-1-3>
    <ModuleIPAddr2-1-3>192.168.0.2</ModuleIPAddr2-1-3>
    <ModuleDBRootCount2-3>2</ModuleDBRootCount2-3>
    <ModuleDBRootID2-1-3>2</ModuleDBRootID2-1-3>
    <ModuleDBRootID2-2-3>4</ModuleDBRootID2-2-3>
  </SystemModuleConfig>
</Columnstore>
)";

bool update_dbroots(const char* zCase,
                    const unique_ptr<xmlDoc>& sDoc,
                    const string& address,
                    const vector<int>& dbroots,
                    const char* zExpected)
{
    bool rv = false;
    json_t* pOutput = json_object();

    auto status = xml::update_dbroots(*sDoc.get(), address, dbroots, pOutput);

    switch (status)
    {
    case xml::DbRoots::UPDATED:
        {
            auto sExpected = compile_xml(zExpected);

            if (equal(sDoc, sExpected))
            {
                cout << zCase << ": Correctly handled.";
                rv = true;
            }
            else
            {
                cout << zCase << ": Config updated, but result not the expected one.\n"
                     << "\n"
                     << "EXPECTED:\n" << dump(sExpected) << "\n"
                     << "\n"
                     << "OBTAINED:\n" << dump(sDoc) << endl;
            }
        }
        break;

    case xml::DbRoots::NO_CHANGE:
        cout << zCase << ": Change was not detected." << endl;
        break;

    case xml::DbRoots::ERROR:
        cout << zCase << ": DbRoot update faile." << endl;
        break;
    }

    cout << endl;

    json_decref(pOutput);

    return rv;
}

int test_scan_for_dbroots()
{
    int rv = 0;
    unique_ptr<xmlDoc> sDoc = compile_xml(ZSCAN_1_2);

    json_t* pOutput = json_object();
    string address { "192.168.0.1" };
    vector<int> dbroots;

    if (rv == 0)
    {
        // Try the same dbroots as there are in the configuration.
        dbroots.push_back(1);

        auto status = xml::update_dbroots(*sDoc.get(), address, dbroots, pOutput);

        if (status == xml::DbRoots::NO_CHANGE)
        {
            cout << "Identical configuration was detected as such." << endl;
        }
        else
        {
            cout << "No change not detected." << endl;
            rv = 1;
        }
    }

    if (rv == 0)
    {
        dbroots.push_back(3);
        // Now there are dbroots 1,3 but the config only has 1, so the config must be updated.

        if (!update_dbroots("(1) -> (1,3)", sDoc, address, dbroots, ZSCAN_1_2_3))
        {
            rv = 1;
        }
    }

    if (rv == 0)
    {
        // Now we add root 4 to the other node.
        dbroots.clear();
        dbroots.push_back(2); // Present in the initial config.
        dbroots.push_back(4);

        address = "192.168.0.2";

        if (!update_dbroots("(2) -> (2, 4)", sDoc, address, dbroots, ZSCAN_1_2_3_4))
        {
            rv = 1;
        }
    }

    if (rv == 0)
    {
        // Now we remove dbroot 3 from node 1.
        dbroots.clear();
        dbroots.push_back(1);

        address = "192.168.0.1";

        if (!update_dbroots("(1,3) -> (1)", sDoc, address, dbroots, ZSCAN_1_2_4))
        {
            rv = 1;
        }
    }

    if (rv == 0)
    {
        // Now we remove dbroot 4 from node2.
        dbroots.clear();
        dbroots.push_back(2);

        address = "192.168.0.2";

        if (!update_dbroots("(2,4) -> (2)", sDoc, address, dbroots, ZSCAN_1_2))
        {
            rv = 1;
        }
    }

    return rv;
}
}

namespace
{

const char ZCLUSTER_CONFIG[] = R"(
<Columnstore Version="V1.0.0">
  <ClusterManager>10.11.12.13</ClusterManager>
  <SystemConfig>
    <DBRootCount>3</DBRootCount>
    <DBRoot1>/var/lib/columnstore/data1</DBRoot1>
    <DBRoot2>/var/lib/columnstore/data2</DBRoot2>
    <DBRoot3>/var/lib/columnstore/data3</DBRoot3>
  </SystemConfig>
  <SystemModuleConfig>
    <ModuleIPAddr1-1-3>192.168.0.1</ModuleIPAddr1-1-3>
    <ModuleDBRootCount1-3>2</ModuleDBRootCount1-3>
    <ModuleDBRootID1-1-3>1</ModuleDBRootID1-1-3>
    <ModuleDBRootID1-2-3>3</ModuleDBRootID1-2-3>
    <ModuleIPAddr2-1-3>192.168.0.2</ModuleIPAddr2-1-3>
    <ModuleDBRootCount2-3>1</ModuleDBRootCount2-3>
    <ModuleDBRootID2-1-3>2</ModuleDBRootID2-1-3>
  </SystemModuleConfig>
  <PrimitiveServers>
    <Count>2</Count>
  </PrimitiveServers>
  <PMS1>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS1>
  <PMS2>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS2>
  <PMS3>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS3>
  <PMS4>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS4>
  <PMS5>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS5>
  <PMS6>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS6>
  <PMS7>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS7>
  <PMS8>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS8>
  <PMS9>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS9>
  <PMS10>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS10>
  <PMS11>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS11>
  <PMS12>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS12>
</Columnstore>
)";

const char ZNODE_CONFIG[] = R"(
<Columnstore Version="V1.0.0">
  <ClusterManager>10.11.12.13</ClusterManager>
  <SystemConfig>
    <DBRootCount>1</DBRootCount>
    <DBRoot1>/var/lib/columnstore/data1</DBRoot1>
  </SystemConfig>
  <SystemModuleConfig>
    <ModuleIPAddr1-1-3>127.0.0.1</ModuleIPAddr1-1-3>
    <ModuleDBRootCount1-3>1</ModuleDBRootCount1-3>
    <ModuleDBRootID1-1-3>1</ModuleDBRootID1-1-3>
  </SystemModuleConfig>
  <PMS1>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS1>
  <PMS2>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS2>
  <PMS3>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS3>
  <PMS4>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS4>
  <PMS5>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS5>
  <PMS6>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS6>
  <PMS7>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS7>
  <PMS8>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS8>
  <PMS9>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS9>
  <PMS10>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS10>
  <PMS11>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS11>
  <PMS12>
    <IPAddr>127.0.0.1</IPAddr>
    <Port>8620</Port>
  </PMS12>
</Columnstore>
)";

const char ZMERGED_CONFIG[] = R"(
<Columnstore Version="V1.0.0">
  <ClusterManager>10.11.12.13</ClusterManager>
  <NextNodeId>4</NextNodeId>
  <NextDBRootId>5</NextDBRootId>
  <SystemConfig>
    <DBRootCount>4</DBRootCount>
    <DBRoot1>/var/lib/columnstore/data1</DBRoot1>
    <DBRoot2>/var/lib/columnstore/data2</DBRoot2>
    <DBRoot3>/var/lib/columnstore/data3</DBRoot3>
    <DBRoot4>/var/lib/columnstore/data4</DBRoot4>
  </SystemConfig>
  <SystemModuleConfig>
    <ModuleIPAddr1-1-3>192.168.0.1</ModuleIPAddr1-1-3>
    <ModuleDBRootCount1-3>2</ModuleDBRootCount1-3>
    <ModuleDBRootID1-1-3>1</ModuleDBRootID1-1-3>
    <ModuleDBRootID1-2-3>3</ModuleDBRootID1-2-3>
    <ModuleIPAddr2-1-3>192.168.0.2</ModuleIPAddr2-1-3>
    <ModuleDBRootCount2-3>1</ModuleDBRootCount2-3>
    <ModuleDBRootID2-1-3>2</ModuleDBRootID2-1-3>
    <ModuleIPAddr3-1-3>192.168.0.3</ModuleIPAddr3-1-3>
    <ModuleDBRootCount3-3>1</ModuleDBRootCount3-3>
    <ModuleDBRootID3-1-3>4</ModuleDBRootID3-1-3>
  </SystemModuleConfig>
  <PrimitiveServers>
    <Count>3</Count>
  </PrimitiveServers>
  <PMS1>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS1>
  <PMS2>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS2>
  <PMS3>
    <IPAddr>192.168.0.3</IPAddr>
    <Port>8620</Port>
  </PMS3>
  <PMS4>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS4>
  <PMS5>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS5>
  <PMS6>
    <IPAddr>192.168.0.3</IPAddr>
    <Port>8620</Port>
  </PMS6>
  <PMS7>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS7>
  <PMS8>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS8>
  <PMS9>
    <IPAddr>192.168.0.3</IPAddr>
    <Port>8620</Port>
  </PMS9>
  <PMS10>
    <IPAddr>192.168.0.1</IPAddr>
    <Port>8620</Port>
  </PMS10>
  <PMS11>
    <IPAddr>192.168.0.2</IPAddr>
    <Port>8620</Port>
  </PMS11>
  <PMS12>
    <IPAddr>192.168.0.3</IPAddr>
    <Port>8620</Port>
  </PMS12>
</Columnstore>
)";

int test_add_multi_node()
{
    int rv = 1;
    json_t* pOutput = json_object();

    unique_ptr<xmlDoc> sCluster = compile_xml(ZCLUSTER_CONFIG);
    unique_ptr<xmlDoc> sNode = compile_xml(ZNODE_CONFIG);

    bool added = cs::xml::add_multi_node(*sCluster.get(), *sNode.get(), "192.168.0.3", pOutput);
    mxb_assert(added);

    unique_ptr<xmlDoc> sMerged = compile_xml(ZMERGED_CONFIG);

    if (equal(sCluster, sMerged))
    {
        cout << "Node added successfully." << endl;
        rv = 0;
    }
    else
    {
        cout << "Node NOT added successfully." << endl;
    }

    json_decref(pOutput);

    return rv;
}
}

int main()
{
    mxb::Log log;

    int rv = 0;
    rv += test_convert_to_first_multi_node();
    rv += test_scan_for_dbroots();
    rv += test_add_multi_node();

    return rv;
}

namespace
{

unique_ptr<xmlDoc> compile_xml(const char* zXml)
{
    unique_ptr<xmlDoc> sDoc(xmlReadMemory(zXml, strlen(zXml), "cs.xml", NULL, 0));
    return sDoc;
}

bool equal(const string& path,
           xmlNode& lhs, xmlXPathContext& lContext,
           xmlNode& rhs, xmlXPathContext& rContext);

bool equal_children(const string& path,
                    xmlNode& lhs, xmlXPathContext& lContext,
                    xmlNode& rhs, xmlXPathContext& rContext)
{
    bool rv = true;
    mxb_assert(strcmp(reinterpret_cast<const char*>(lhs.name),
                      reinterpret_cast<const char*>(rhs.name)) == 0);

    xmlNode* pL_child = lhs.children;

    if (pL_child
        && pL_child->type == XML_TEXT_NODE
        && !pL_child->next
        && !pL_child->children)
    {
        // Only one child that is text without children.
        xmlNode* pR_child = rhs.children;

        if (pR_child
            && pR_child->type == XML_TEXT_NODE
            && !pR_child->next
            && !pR_child->children)
        {
            // Also only one child that is text without children.
            auto* pL_content = xmlNodeGetContent(&lhs);
            auto* pR_content = xmlNodeGetContent(&rhs);

            const char* zL_content = reinterpret_cast<const char*>(pL_content);
            const char* zR_content = reinterpret_cast<const char*>(pR_content);

            if (zL_content && zR_content)
            {
                if (strcmp(zL_content, zR_content) != 0)
                {
                    cout << path << "(L): " << zL_content << endl;
                    cout << path << "(R): " << zR_content << endl;
                    rv = false;
                }
            }
            else if (pL_content && !pR_content)
            {
                cout << path << "(L): " << zL_content << endl;
                cout << path << "(R): NO CONTENT" << endl;
                rv = false;
            }
            else if (pR_content && !pL_content)
            {
                cout << path << "(L): NO CONTENT" << endl;
                cout << path << "(R): " << zR_content << endl;
                rv = false;
            }
        }
        else
        {
            cout << path << "(L): Single text node child." << endl;
            cout << path << "(R): NOT single text node child." << endl;
            rv = false;
        }
    }
    else
    {
        while (rv && pL_child)
        {
            if (pL_child->type == XML_ELEMENT_NODE)
            {
                mxb_assert(pL_child->name);

                string name(reinterpret_cast<const char*>(pL_child->name));
                string full_name = path + "/" + name;
                string xpath = "./" + name;
                const xmlChar* pXpath = reinterpret_cast<const xmlChar*>(xpath.c_str());

                xmlXPathObject* pXpath_object = xmlXPathNodeEval(&rhs, pXpath, &rContext);
                xmlNodeSet* pNodes = pXpath_object->nodesetval;
                mxb_assert(pNodes->nodeNr <= 1);

                if (pNodes->nodeNr == 0)
                {
                    cout << "\"" << full_name << "\" found in first document, but not in other." << endl;
                    rv = false;
                }
                else
                {
                    mxb_assert(pNodes->nodeNr == 1);

                    xmlNode* pR_node = pNodes->nodeTab[0];

                    rv = equal(full_name, *pL_child, lContext, *pR_node, rContext);
                }
            }

            pL_child = pL_child->next;
        }
    }

    return rv;
}

bool equal(const string& path,
           xmlNode& lhs, xmlXPathContext& lContext,
           xmlNode& rhs, xmlXPathContext& rContext)
{
    mxb_assert(strcmp(reinterpret_cast<const char*>(lhs.name),
                      reinterpret_cast<const char*>(rhs.name)) == 0);

    bool rv = equal_children(path, lhs, lContext, rhs, rContext);
    if (rv)
    {
        rv = equal_children(path, rhs, rContext, lhs, lContext);
    }

    return rv;
}

bool equal(xmlNode& lhs, xmlNode& rhs)
{
    bool rv = false;

    const char* zLeft_name = reinterpret_cast<const char*>(lhs.name);
    const char* zRight_name = reinterpret_cast<const char*>(rhs.name);

    if (strcmp(zLeft_name, zRight_name) == 0)
    {
        xmlXPathContext* pL_context = xmlXPathNewContext(lhs.doc);
        xmlXPathContext* pR_context = xmlXPathNewContext(rhs.doc);
        mxb_assert(pL_context && pR_context);

        rv = equal(zLeft_name, lhs, *pL_context, rhs, *pR_context);

        xmlXPathFreeContext(pR_context);
        xmlXPathFreeContext(pL_context);
    }
    else
    {
        cout << zLeft_name << " != " << zRight_name << endl;
    }

    return rv;
}

bool equal(xmlDoc& lhs, xmlDoc& rhs)
{
    xmlNode* pL = xmlDocGetRootElement(&lhs);
    xmlNode* pR = xmlDocGetRootElement(&rhs);

    mxb_assert(pL && pR);

    return equal(*pL, *pR);
}

bool equal(const unique_ptr<xmlDoc>& sLhs, const unique_ptr<xmlDoc>& sRhs)
{
    return equal(*sLhs.get(), *sRhs.get());
}
}
