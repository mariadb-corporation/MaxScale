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

#include <unistd.h>
#include <fstream>
#include <iostream>
#include <libxml/xpath.h>
#include <maxbase/log.hh>
#include <maxscale/maxscale_test.h>
#include "columnstore.hh"

using namespace std;
namespace xml = cs::xml;

namespace
{

const string TEST_DIR_PATH { SOURCE_DIR "/server/modules/monitor/csmon/test" };

const string PATH_CS_SINGLE_NODE { TEST_DIR_PATH + "/" + "cs-single-node.xml" };
const string PATH_CS_FIRST_MULTI_NODE { TEST_DIR_PATH + "/" + "cs-first-multi-node.xml" };

bool equal(const xmlNode& lhs, const xmlNode& rhs)
{
    return mxb::xml::equal(lhs, rhs, &cout);
}

bool equal(const xmlDoc& lhs, const xmlDoc& rhs)
{
    return mxb::xml::equal(lhs, rhs, &cout);
}

bool equal(const unique_ptr<xmlDoc>& sLhs, const unique_ptr<xmlDoc>& sRhs)
{
    return equal(*sLhs.get(), *sRhs.get());
}

string dump(const std::unique_ptr<xmlDoc>& sDoc)
{
    return mxb::xml::dump(*sDoc.get());
}

xmlNode& get_root(xmlDoc& csXml)
{
    xmlNode* pRoot = xmlDocGetRootElement(&csXml);
    mxb_assert(pRoot);
    mxb_assert(strcmp(reinterpret_cast<const char*>(pRoot->name), "Columnstore") == 0);
    return *pRoot;
}

xmlNode& get_root(const unique_ptr<xmlDoc>& sXml)
{
    return get_root(*sXml.get());
}

}

namespace maxbase
{
namespace xml
{
unique_ptr<xmlDoc> load_file(const std::string& path)
{
    unique_ptr<xmlDoc> sDoc;

    std::ifstream in(path, std::ios::ate);

    if (in)
    {
        auto size = in.tellg();
        std::string s(size, '\0');
        in.seekg(0);
        if (in.read(&s[0], size))
        {
            sDoc = mxb::xml::load(s);
        }
    }

    return sDoc;
}
}
}


namespace
{

int test_convert_to_first_multi_node()
{
    int rv = 0;
    unique_ptr<xmlDoc> sDoc = mxb::xml::load_file(PATH_CS_SINGLE_NODE);

    const char IP[] = "198.168.0.1";
    const char MANAGER[] = "10.11.12.13";

    json_t* pOutput = json_object();
    xml::convert_to_first_multi_node(*sDoc.get(), MANAGER, IP, pOutput);
    json_decref(pOutput);

    unique_ptr<xmlDoc> sExpected = mxb::xml::load_file(PATH_CS_FIRST_MULTI_NODE);

    // The revision must be updated as otherwise there will be a discrepancy.
    string revision = mxb::xml::get_content_as<string>(get_root(sDoc), cs::xml::CONFIGREVISION);
    mxb::xml::set_content(get_root(sExpected), cs::xml::CONFIGREVISION, revision);

    if (equal(sExpected, sDoc))
    {
        cout << "Single -> Multi Conversion ok" << endl;

        xml::convert_to_single_node(*sDoc.get());
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
            auto sExpected = mxb::xml::load(zExpected);

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
    unique_ptr<xmlDoc> sDoc = mxb::xml::load(ZSCAN_1_2);

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

    unique_ptr<xmlDoc> sCluster = mxb::xml::load(ZCLUSTER_CONFIG);
    unique_ptr<xmlDoc> sNode = mxb::xml::load(ZNODE_CONFIG);

    bool added = cs::xml::add_multi_node(*sCluster.get(), *sNode.get(), "192.168.0.3", pOutput);
    mxb_assert(added);

    unique_ptr<xmlDoc> sMerged = mxb::xml::load(ZMERGED_CONFIG);

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
