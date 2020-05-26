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
#include "columnstore.hh"
#include <libxml/xpath.h>

using std::string;
using std::vector;

namespace cs
{

const char* to_string(Version version)
{
    switch (version)
    {
    case CS_10:
        return ZCS_10;

    case CS_12:
        return ZCS_12;

    case CS_15:
        return ZCS_15;

    case CS_UNKNOWN:
        return "unknown";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

const char* to_string(ClusterMode cluster_mode)
{
    switch (cluster_mode)
    {
    case READONLY:
        return "readonly";

    case READWRITE:
        return "readwrite";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

bool from_string(const char* zCluster_mode, ClusterMode* pCluster_mode)
{
    bool rv = true;

    if (strcmp(zCluster_mode, "readonly") == 0)
    {
        *pCluster_mode = READONLY;
    }
    else if (strcmp(zCluster_mode, "readwrite") == 0)
    {
        *pCluster_mode = READWRITE;
    }
    else
    {
        rv = false;
    }

    return rv;
}

const char* to_string(DbrmMode dbrm_mode)
{
    switch (dbrm_mode)
    {
    case MASTER:
        return "master";

    case SLAVE:
        return "slave";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

bool from_string(const char* zDbrm_mode, DbrmMode* pDbrm_mode)
{
    bool rv = true;

    if (strcmp(zDbrm_mode, "master") == 0)
    {
        *pDbrm_mode = MASTER;
    }
    else if (strcmp(zDbrm_mode, "slave") == 0)
    {
        *pDbrm_mode = SLAVE;
    }
    else
    {
        rv = false;
    }

    return rv;
}

const char* rest::to_string(rest::Action action)
{
    switch (action)
    {
    case BEGIN:
        return "begin";

    case COMMIT:
        return "commit";

    case CONFIG:
        return "config";

    case ROLLBACK:
        return "rollback";

    case SHUTDOWN:
        return "shutdown";

    case STATUS:
        return "status";

    case START:
        return "start";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

bool from_string(const char* zXml, std::unique_ptr<xmlDoc>* psDoc)
{
    psDoc->reset(xmlReadMemory(zXml, strlen(zXml), "columnstore.xml", NULL, 0));
    return *psDoc ? true : false;
}

bool from_string(const char* zTimestamp, std::chrono::system_clock::time_point* pTimestamp)
{
    struct tm tm;
    bool rv = strptime(zTimestamp, "%Y-%m-%d %H:%M:%S", &tm) != nullptr;

    if (rv)
    {
        *pTimestamp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    return rv;
}

bool dbroots_from_array(json_t* pArray, DbRootIdVector* pDbroots)
{
    bool rv = json_is_array(pArray);

    if (rv)
    {
        DbRootIdVector dbroots;

        size_t i;
        json_t* pValue;
        json_array_foreach(pArray, i, pValue)
        {
            dbroots.push_back(json_integer_value(json_array_get(pArray, i)));
        }

        pDbroots->swap(dbroots);
    }

    return rv;
}

bool services_from_array(json_t* pArray, ServiceVector* pServices)
{
    bool rv = json_is_array(pArray);

    if (rv)
    {
        ServiceVector services;

        size_t i;
        json_t* pService;
        json_array_foreach(pArray, i, pService)
        {
            json_t* pName = json_object_get(pService, cs::body::NAME);
            mxb_assert(pName);
            json_t* pPid = json_object_get(pService, cs::body::PID);
            mxb_assert(pPid);

            if (pName && pPid)
            {
                auto zName = json_string_value(pName);
                auto pid = json_integer_value(pPid);

                services.emplace_back(zName, pid);
            }
            else
            {
                MXS_ERROR("Object in services array does not have 'name' and/or 'pid' fields.");
            }
        }

        pServices->swap(services);
    }

    return rv;
}

namespace
{

xmlNode& get_root(xmlDoc& csXml)
{
    xmlNode* pRoot = xmlDocGetRootElement(&csXml);
    mxb_assert(pRoot);
    mxb_assert(strcmp(reinterpret_cast<const char*>(pRoot->name), "Columnstore") == 0);
    return *pRoot;
}

}

std::vector<xmlNode*> xml::find_nodes_by_xpath(xmlNode& node, const char* zXpath)
{
    vector<xmlNode*> nodes;

    xmlXPathContext* pXpath_context = xmlXPathNewContext(node.doc);
    mxb_assert(pXpath_context);

    string path(zXpath);
    path = "./" + path;
    xmlXPathObject* pXpath_object = xmlXPathNodeEval(&node,
                                                     reinterpret_cast<const xmlChar*>(path.c_str()),
                                                     pXpath_context);


    xmlNodeSet* pNodes = pXpath_object->nodesetval;

    for (int i = 0; i < pNodes->nodeNr; ++i)
    {
        nodes.push_back(pNodes->nodeTab[i]);
    }

    xmlXPathFreeObject(pXpath_object);
    xmlXPathFreeContext(pXpath_context);

    return nodes;
}

std::vector<xmlNode*> xml::find_nodes_by_xpath(xmlDoc& csXml, const char* zXpath)
{
    return find_nodes_by_xpath(get_root(csXml), zXpath);
}

xmlNode* xml::find_node_by_xpath(xmlNode& node, const char* zXpath)
{
    vector<xmlNode*> nodes = find_nodes_by_xpath(node, zXpath);
    mxb_assert(nodes.empty() || nodes.size() == 1);

    return nodes.empty() ? nullptr : nodes.front();
}

xmlNode* xml::find_node_by_xpath(xmlDoc& csXml, const char* zXpath)
{
    return find_node_by_xpath(get_root(csXml), zXpath);
}

vector<xmlNode*> xml::find_children_by_prefix(xmlNode& parent, const char* zPrefix)
{
    vector<xmlNode*> nodes;

    int n = strlen(zPrefix);
    xmlNode* pChild = parent.children;

    while (pChild)
    {
        if (strncmp(reinterpret_cast<const char*>(pChild->name), zPrefix, n) == 0)
        {
            nodes.push_back(pChild);
        }

        pChild = pChild->next;
    }

    return nodes;
}

bool xml::find_node_id(xmlDoc& xmlDoc, const string& address, string* pNid)
{
    bool rv = false;

    xmlNode* pSmc = xml::find_node_by_xpath(xmlDoc, SYSTEMMODULECONFIG);

    if (pSmc)
    {
        auto nodes = xml::find_children_by_prefix(*pSmc, xml::MODULEIPADDR);

        for (auto* pNode : nodes)
        {
            const char* zName = reinterpret_cast<const char*>(pNode->name);
            // zName is now "ModuleIPAddrX-Y-Z", where X is the node id, Y a sequence number,
            // and Z the role. IF Z is 3, the node in question is a performance node and that's
            // what we are interested in now. The content of the node is an IP-address, and if
            // that matches the address we are looking for, then we will know the node id
            // corresponding to that address.

            string tail(zName + strlen(xml::MODULEIPADDR));
            vector<string> parts = mxb::strtok(tail, "-");

            if (parts.size() == 3)
            {
                string role = parts[2];

                if (role == xml::ROLE_PM)
                {
                    const char* zContent = reinterpret_cast<const char*>(xmlNodeGetContent(pNode));

                    if (zContent && (address == zContent))
                    {
                        *pNid = parts[0];
                        rv = true;
                        break;
                    }
                }
            }
            else
            {
                MXS_ERROR("Found in Columnstore XML configuration a ModUleIPAddr entry of "
                          "unexpected format: '%s'", zName);
            }
        }
    }

    return rv;
}

namespace
{

void xml_insert_leaf(xmlNode& parent, const char* zKey, const char* zValue, xml::XmlLocation location)
{
    mxb_assert(strchr(zKey, '/') == nullptr);

    xmlNode* pChild = xmlNewNode(NULL, reinterpret_cast<const xmlChar*>(zKey));
    xmlNode* pContent = xmlNewText(reinterpret_cast<const xmlChar*>(zValue));
    xmlAddChild(pChild, pContent);

    xmlNode* pSibling = parent.xmlChildrenNode;

    if (location == xml::XmlLocation::AT_BEGINNING && pSibling)
    {
        xmlAddPrevSibling(pSibling, pChild);
        xmlNode* pIndentation = xmlNewText(reinterpret_cast<const xmlChar*>("\n\t"));
        xmlAddPrevSibling(pChild, pIndentation);
    }
    else
    {
        xmlAddChild(&parent, pChild);
        if (pChild->prev
            && pChild->prev->type == XML_TEXT_NODE
            && strcmp(reinterpret_cast<char*>(xmlNodeGetContent(pChild->prev)), "\n") == 0)
        {
            xmlNodeSetContent(pChild->prev, reinterpret_cast<const xmlChar*>("\n\t"));
        }
        else
        {
            xmlNode* pIndentation = xmlNewText(reinterpret_cast<const xmlChar*>("\n\t"));
            xmlAddPrevSibling(pChild, pIndentation);
        }

        xmlNode* pLinebreak = xmlNewText(reinterpret_cast<const xmlChar*>("\n"));
        xmlAddNextSibling(pChild, pLinebreak);
    }
}

enum class UpdateWhen
{
    IF,
    IF_NOT
};

int xml_update(xmlNodeSet* pNodes, const char* zNew_value, const char* zIf_value, UpdateWhen update_when)
{
    int n = 0;
    int nNodes = pNodes ? pNodes->nodeNr : 0;

    // From the XML sample.
    /*
     * NOTE: the nodes are processed in reverse order, i.e. reverse document
     *       order because xmlNodeSetContent can actually free up descendant
     *       of the node and such nodes may have been selected too ! Handling
     *       in reverse order ensure that descendant are accessed first, before
     *       they get removed. Mixing XPath and modifications on a tree must be
     *       done carefully !
     */

    for (int i = nNodes - 1; i >= 0; --i)
    {
        const char* zValue = nullptr;
        auto* pNode = pNodes->nodeTab[i];

        if (zIf_value)
        {
            zValue = reinterpret_cast<const char*>(xmlNodeGetContent(pNode));
        }

        bool do_update = false;

        if (update_when == UpdateWhen::IF)
        {
            do_update = !zIf_value || (zValue && strcmp(zIf_value, zValue) == 0);
        }
        else
        {
            do_update = !zIf_value || (!zValue || strcmp(zIf_value, zValue) != 0);
        }

        if (do_update)
        {
            ++n;
            xmlNodeSetContent(pNode, reinterpret_cast<const xmlChar*>(zNew_value));

            // From the XML sample.
            /*
             * All the elements returned by an XPath query are pointers to
             * elements from the tree *except* namespace nodes where the XPath
             * semantic is different from the implementation in libxml2 tree.
             * As a result when a returned node set is freed when
             * xmlXPathFreeObject() is called, that routine must check the
             * element type. But node from the returned set may have been removed
             * by xmlNodeSetContent() resulting in access to freed data.
             * This can be exercised by running
             *       valgrind xpath2 test3.xml '//discarded' discarded
             * There is 2 ways around it:
             *   - make a copy of the pointers to the nodes from the result set
             *     then call xmlXPathFreeObject() and then modify the nodes
             * or
             *   - remove the reference to the modified nodes from the node set
             *     as they are processed, if they are not namespace nodes.
             */
            if (pNode->type != XML_NAMESPACE_DECL)
            {
                pNodes->nodeTab[i] = NULL;
            }
        }
    }

    return n;
}

int xml_update(xmlNode& node,
               xmlXPathContext& xpath_context,
               const char* zXpath,
               const char* zNew_value,
               const char* zIf_value,
               UpdateWhen update_when)
{
    int n = -1;

    string path(zXpath);
    path = "./" + path;

    xmlXPathObject* pXpath_object = xmlXPathNodeEval(&node,
                                                     reinterpret_cast<const xmlChar*>(path.c_str()),
                                                     &xpath_context);
    mxb_assert(pXpath_object);

    if (pXpath_object)
    {
        n = xml_update(pXpath_object->nodesetval, zNew_value, zIf_value, update_when);
        xmlXPathFreeObject(pXpath_object);
    }

    return n;
}

}

int xml::update_if(xmlNode& node, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    int n = -1;
    xmlXPathContext* pXpath_context = xmlXPathNewContext(node.doc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = xml_update(node, *pXpath_context, zXpath, zNew_value, zIf_value, UpdateWhen::IF);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

int xml::update_if(xmlDoc& xmlDoc, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    xmlNode* pRoot = xmlDocGetRootElement(&xmlDoc);
    mxb_assert(pRoot);

    return update_if(*pRoot, zXpath, zNew_value, zIf_value);
}

int xml::update_if_not(xmlNode& node, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    int n = -1;

    xmlXPathContext* pXpath_context = xmlXPathNewContext(node.doc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = xml_update(node, *pXpath_context, zXpath, zNew_value, zIf_value, UpdateWhen::IF_NOT);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

int xml::update_if_not(xmlDoc& xmlDoc, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    xmlNode* pRoot = xmlDocGetRootElement(&xmlDoc);
    mxb_assert(pRoot);

    return update_if_not(*pRoot, zXpath, zNew_value, zIf_value);
}

bool xml::insert(xmlNode& parent, const char* zKey, const char* zValue, XmlLocation location)
{
    mxb_assert(*zKey != '/');

    bool rv = false;

    string key(zKey);
    auto pos = key.find_last_of("/");

    if (pos == string::npos)
    {
        // zKey is a name and not a path.
        xml_insert_leaf(parent, zKey, zValue, location);
        rv = true;
    }
    else
    {
        string path = key.substr(0, pos);
        string name = key.substr(pos + 1);

        mxb_assert(&parent.doc);
        xmlXPathContext* pXpath_context = xmlXPathNewContext(parent.doc);
        mxb_assert(pXpath_context);

        path = ".//" + path; // Search from the current node.
        xmlXPathObject* pXpath_object = xmlXPathNodeEval(&parent,
                                                         reinterpret_cast<const xmlChar*>(path.c_str()),
                                                         pXpath_context);

        xmlNodeSet* pNodes = pXpath_object->nodesetval;

        if (pNodes->nodeNr != 0)
        {
            mxb_assert(pNodes->nodeNr == 1);

            xml_insert_leaf(*pNodes->nodeTab[0], name.c_str(), zValue, location);
            rv = true;
        }

        xmlXPathFreeObject(pXpath_object);
        xmlXPathFreeContext(pXpath_context);
    }

    return rv;
}

bool xml::insert(xmlDoc& csXml, const char* zKey, const char* zValue, XmlLocation location)
{
    mxb_assert(*zKey != '/');
    return insert(get_root(csXml), zKey, zValue, location);
}

bool xml::upsert(xmlNode& parent, const char* zKey, const char* zValue, XmlLocation location)
{
    bool rv = true;

    vector<xmlNode*> nodes = find_nodes_by_xpath(parent, zKey);
    mxb_assert(nodes.empty() || nodes.size() == 1);

    if (nodes.size() == 1)
    {
        auto* pNode = nodes.front();

        xmlNodeSetContent(pNode, reinterpret_cast<const xmlChar*>(zValue));
    }
    else
    {
        rv = insert(parent, zKey, zValue, location);
    }

    return rv;
}

bool xml::upsert(xmlDoc& csXml, const char* zKey, const char* zValue, XmlLocation location)
{
    return upsert(get_root(csXml), zKey, zValue, location);
}

namespace
{

int xml_remove(xmlNodeSet* pNodes)
{
    int nNodes = pNodes ? pNodes->nodeNr : 0;

    // From the XML sample.
    /*
     * NOTE: the nodes are processed in reverse order, i.e. reverse document
     *       order because xmlNodeSetContent can actually free up descendant
     *       of the node and such nodes may have been selected too ! Handling
     *       in reverse order ensure that descendant are accessed first, before
     *       they get removed. Mixing XPath and modifications on a tree must be
     *       done carefully !
     */

    for (int i = nNodes - 1; i >= 0; --i)
    {
        const char* zValue = nullptr;
        auto* pNode = pNodes->nodeTab[i];

        if (pNode->type != XML_NAMESPACE_DECL)
        {
            pNodes->nodeTab[i] = NULL;
        }

        if (pNode->prev
            && pNode->prev->type == XML_TEXT_NODE
            && strcmp(reinterpret_cast<char*>(xmlNodeGetContent(pNode->prev)), "\n\t") == 0)
        {
            auto pPrev = pNode->prev;
            xmlUnlinkNode(pPrev);
            xmlFreeNode(pPrev);
        }

        xmlUnlinkNode(pNode);
        xmlFreeNode(pNode);
    }

    return nNodes;
}

int xml_remove(xmlNode& node, xmlXPathContext& xpath_context, const char* zXpath)
{
    int n = -1;

    string path(zXpath);
    path = "./" + path;
    xmlXPathObject* pXpath_object = xmlXPathNodeEval(&node,
                                                     reinterpret_cast<const xmlChar*>(path.c_str()),
                                                     &xpath_context);
    mxb_assert(pXpath_object);

    if (pXpath_object)
    {
        n = xml_remove(pXpath_object->nodesetval);
        xmlXPathFreeObject(pXpath_object);
    }

    return n;
}

}

int xml::remove(xmlNode& node, const char* zXpath)
{
    int n = -1;
    xmlXPathContext* pXpath_context = xmlXPathNewContext(node.doc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = xml_remove(node, *pXpath_context, zXpath);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

int xml::remove(xmlDoc& xmlDoc, const char* zXpath)
{
    return remove(get_root(xmlDoc), zXpath);
}

string xml::dump(xmlDoc& doc)
{
    xmlBuffer* pBuffer = xmlBufferCreate();
    xmlNodeDump(pBuffer, &doc, xmlDocGetRootElement(&doc), 0, 0);
    xmlChar* pXml = xmlBufferDetach(pBuffer);
    const char* zXml = reinterpret_cast<const char*>(pXml);

    string xml(zXml);

    MXS_FREE(pXml);
    xmlBufferFree(pBuffer);

    return xml;
}

void xml::convert_to_first_multi_node(xmlDoc& xmlDoc,
                                      const string& manager,
                                      const string& server_address)
{
    // Ensure there is a "ClusterManager" key whose value is 'manager'.
    xml::upsert(xmlDoc, "ClusterManager", manager.c_str());

    // Replace all "IPAddr" values, irrespective of where they occur, with 'server_address'
    // if the current value is "127.0.0.1".
    int n = xml::update_if(xmlDoc, "/IPAddr", server_address.c_str(), "127.0.0.1");
    mxb_assert(n >= 0);
}

void xml::convert_to_single_node(xmlDoc& xmlDoc)
{
    MXB_AT_DEBUG(int n);
    // Remove the "ClusterManager" key.
    MXB_AT_DEBUG(n =) xml::remove(xmlDoc, CLUSTERMANAGER);
    mxb_assert(n == 1);
    // Replace all "IPAddr" values, irrespective of where they occur, with "127.0.0.1", provided
    // the current value is not "0.0.0.0".
    MXB_AT_DEBUG(n =) xml::update_if_not(xmlDoc, "/IPAddr", "127.0.0.1", "0.0.0.0");
    mxb_assert(n >= 0);
}

namespace
{

xml::DbRoots::Status add_dbroots(xmlNode& smc,
                                 xmlNode& sc,
                                 const std::string& nid,
                                 const std::vector<int>& dbroots,
                                 int n,
                                 xmlNode* pLast_dbroot,
                                 json_t* pOutput)
{
    xml::DbRoots::Status rv = xml::DbRoots::ERROR;

    string prefix(xml::MODULEDBROOTID);
    prefix += nid;

    int nRoots = dbroots.size();
    for (int i = n + 1; i <= nRoots; ++i)
    {
        string key(prefix);
        key += "-";
        key += std::to_string(i);
        key += "-";
        key += xml::ROLE_PM;
        auto zKey = reinterpret_cast<const xmlChar*>(key.c_str());
        string dbroot = std::to_string(dbroots[i - 1]);
        auto zDbroot = reinterpret_cast<const xmlChar*>(dbroot.c_str());
        xmlNode* pNew_dbroot = xmlNewNode(nullptr, zKey);
        xmlNode* pContent = xmlNewText(zDbroot);
        xmlAddChild(pNew_dbroot, pContent);

        if (pLast_dbroot)
        {
            xmlAddNextSibling(pLast_dbroot, pNew_dbroot);
        }
        else
        {
            xmlAddChild(&smc, pNew_dbroot);
        }
        pLast_dbroot = pNew_dbroot;
    }

    string key(xml::MODULEDBROOTCOUNT);
    key += nid;
    key += "-";
    key += xml::ROLE_PM;

    int nUpdated = xml::update(smc, key.c_str(), std::to_string(nRoots).c_str());

    if (nUpdated == 1)
    {
        xmlNode* pRoot_count = xml::find_node_by_xpath(sc, xml::DBROOTCOUNT);
        mxb_assert(pRoot_count);

        if (pRoot_count)
        {
            auto zCount = reinterpret_cast<const char*>(xmlNodeGetContent(pRoot_count));
            int count = atoi(zCount);

            count += (nRoots - n);

            xmlNodeSetContent(pRoot_count,
                              reinterpret_cast<const xmlChar*>(std::to_string(count).c_str()));

            for (auto i : dbroots)
            {
                auto suffix = std::to_string(i);
                string key(xml::DBROOT);
                key += suffix;

                string value("/var/lib/columnstore/data");
                value += suffix;

                xml::upsert(sc, key.c_str(), value.c_str());
            }

            rv = xml::DbRoots::UPDATED;
        }
        else
        {
            LOG_APPEND_JSON_ERROR(&pOutput,
                                  "The XML configuration lacks a %s entry.",
                                  "SystemConfig/DBRootCount");
            mxb_assert(!true);
        }
    }
    else
    {
        LOG_APPEND_JSON_ERROR(&pOutput,
                              "Could not update key '%s', db roots will not be updated.",
                              key.c_str());
    }

    return rv;
}

xml::DbRoots::Status remove_dbroots(xmlNode& smc,
                                    xmlNode& sc,
                                    const std::string& nid,
                                    const std::vector<int>& dbroots,
                                    int n,
                                    json_t* pOutput)
{
    xml::DbRoots::Status rv = xml::DbRoots::UPDATED;

    int nRoots = dbroots.size();
    for (int i = n; i > nRoots; --i)
    {
        string key(xml::MODULEDBROOTID);
        key += nid;
        key += "-";
        key += std::to_string(i);
        key += "-";
        key += xml::ROLE_PM;

        xmlNode* pNode = xml::find_node_by_xpath(smc, key.c_str());

        if (pNode)
        {
            const char* zId = reinterpret_cast<const char*>(xmlNodeGetContent(pNode));

            int nRemoved = xml::remove(smc, key.c_str());

            if (nRemoved == 1)
            {
                key = xml::DBROOT;
                key += zId;

                nRemoved = xml::remove(sc, key.c_str());

                if (nRemoved != 1)
                {
                    LOG_APPEND_JSON_ERROR(&pOutput,
                                          "The key '%s' in the Columnstore configuration "
                                          "lacks the child '%s'.",
                                          xml::SYSTEMCONFIG, key.c_str());
                    rv = xml::DbRoots::ERROR;
                }
            }
            else
            {
                LOG_APPEND_JSON_ERROR(&pOutput,
                                      "The key '%s' in the Columnstore configuration "
                                      "lacks the child '%s'.",
                                      xml::SYSTEMMODULECONFIG, key.c_str());
                rv = xml::DbRoots::ERROR;
            }
        }
        else
        {
            LOG_APPEND_JSON_ERROR(&pOutput,
                                  "The key '%s' in the Columnstore configuration "
                                  "lacks the child '%s'.",
                                  xml::SYSTEMMODULECONFIG, key.c_str());
            rv = xml::DbRoots::ERROR;
        }
    }

    if (rv == xml::DbRoots::UPDATED)
    {
        string key(xml::MODULEDBROOTCOUNT);
        key += nid;
        key += "-";
        key += xml::ROLE_PM;

        int nUpdated = xml::update(smc, key.c_str(), std::to_string(dbroots.size()).c_str());

        if (nUpdated == 1)
        {
            xmlNode* pDbrc = xml::find_node_by_xpath(sc, xml::DBROOTCOUNT);

            if (pDbrc)
            {
                const auto* zValue = reinterpret_cast<const char*>(xmlNodeGetContent(pDbrc));
                char* zEnd;
                errno = 0;
                long l = strtol(zValue, &zEnd, 10);

                bool valid = (l > 1 && errno == 0 && zEnd != zValue && *zEnd == 0);

                if (valid)
                {
                    l -= (n - nRoots);
                    mxb_assert(l >= 1);

                    nUpdated = xml::update(sc, xml::DBROOTCOUNT, std::to_string(l).c_str());

                    if (nUpdated != 1)
                    {
                        LOG_APPEND_JSON_ERROR(&pOutput,
                                              "Could not update the value of '%s/%s' in the "
                                              "Columnstore configuration.",
                                              xml::SYSTEMMODULECONFIG, key.c_str());
                        rv = xml::DbRoots::ERROR;
                    }
                }
                else
                {
                    LOG_APPEND_JSON_ERROR(&pOutput,
                                          "Could not convert value '%s' of '%s/%s' to a positive integer.",
                                          zValue, xml::SYSTEMCONFIG, xml::DBROOTCOUNT);
                    rv = xml::DbRoots::ERROR;
                }
            }
            else
            {
                LOG_APPEND_JSON_ERROR(&pOutput,
                                      "Could not find the key '%s/%s' in the Columnstore configuration.",
                                      xml::SYSTEMCONFIG, key.c_str());
                rv = xml::DbRoots::ERROR;
            }
        }
        else
        {
            LOG_APPEND_JSON_ERROR(&pOutput,
                                  "Could not update the value of '%s/%s' in the Columnstore configuration.",
                                  xml::SYSTEMMODULECONFIG, key.c_str());
            rv = xml::DbRoots::ERROR;
        }
    }

    return rv;
}

}

xml::DbRoots::Status xml::update_dbroots(xmlDoc& csXml,
                                         const std::string& address,
                                         const std::vector<int>& dbroots,
                                         json_t* pOutput)
{
    DbRoots::Status rv = DbRoots::ERROR;

    string nid;
    if (xml::find_node_id(csXml, address.c_str(), &nid))
    {
        xmlNode* pSmc = xml::find_node_by_xpath(csXml, xml::SYSTEMMODULECONFIG);

        if (pSmc)
        {
            rv = DbRoots::NO_CHANGE;

            string prefix(xml::MODULEDBROOTID);
            prefix += nid;
            vector<xmlNode*> nodes = xml::find_children_by_prefix(*pSmc, prefix.c_str());

            // Irrespective of the dbroots values, the ModuleDBRootID entries are numbered
            // consecutively, starting from 1. So we just need the count.
            int n = 0;
            xmlNode* pLast_dbroot = nullptr;

            for (auto* pNode : nodes)
            {
                const char* zName = reinterpret_cast<const char*>(pNode->name);
                string tail(zName + prefix.size() - 1); // zName is "ModuleDBRootIDX-Y-Z", we want "X-Y-Z"
                vector<string> parts = mxb::strtok(tail, "-");

                if (parts.size() == 3)
                {
                    const string& role = parts[2];

                    if (role == xml::ROLE_PM)
                    {
                        pLast_dbroot = pNode;
                        ++n;
                    }
                }
                else
                {
                    LOG_APPEND_JSON_ERROR(&pOutput,
                                          "'%s' is an invalid entry for a ModuleDBRootID entry. "
                                          "There does not seem to be a proper trailing "
                                          "node-sequence-role part.", zName);
                    mxb_assert(!true);
                    rv = DbRoots::ERROR;
                }
            }

            if (rv != DbRoots::ERROR)
            {
                int nRoots = dbroots.size();
                xmlNode* pSc = xml::find_node_by_xpath(csXml, xml::SYSTEMCONFIG);
                mxb_assert(pSc);

                if (n == nRoots)
                {
                    MXS_NOTICE("The DB roots for '%s' in the Columnstore configuration matches "
                               "what the node itself reports.",
                               address.c_str());
                    mxb_assert(rv == DbRoots::NO_CHANGE);
                }
                else if (n < nRoots)
                {
                    rv = add_dbroots(*pSmc, *pSc, nid, dbroots, n, pLast_dbroot, pOutput);
                }
                else
                {
                    mxb_assert(n > nRoots);
                    rv = remove_dbroots(*pSmc, *pSc, nid, dbroots, n, pOutput);
                }
            }
        }
        else
        {
            LOG_APPEND_JSON_ERROR(&pOutput,
                                  "The XML configuration lacks a Columnstore/%s entry.",
                                  xml::SYSTEMMODULECONFIG);
            mxb_assert(!true);
        }
    }
    else
    {
        LOG_APPEND_JSON_ERROR(&pOutput,
                              "Cannot figure out node id for server '%s' from XML configuration.",
                              address.c_str());
    }

    return rv;
}

string rest::create_url(const SERVER& server,
                        int64_t port,
                        const string& rest_base,
                        rest::Action action)
{
    string url("https://");
    url += server.address();
    url += ":";
    url += std::to_string(port);
    url += rest_base;
    url += "/node/";

    url += to_string(action);

    return url;
}

namespace body
{

string begin(const std::chrono::seconds& timeout, int id)
{
    std::ostringstream body;
    body << "{\"" << TIMEOUT << "\": "
         << timeout.count()
         << ", \"" << ID << "\": "
         << id
         << "}";

    return body.str();
}

string config(const xmlDoc& csXml,
              int revision,
              const string& manager,
              const std::chrono::seconds& timeout)
{
    xmlChar* pConfig = nullptr;
    int size = 0;

    xmlDocDumpMemory(const_cast<xmlDoc*>(&csXml), &pConfig, &size);

    json_t* pBody = json_object();
    json_object_set_new(pBody, CONFIG, json_stringn(reinterpret_cast<const char*>(pConfig), size));
    json_object_set_new(pBody, REVISION, json_integer(revision));
    json_object_set_new(pBody, MANAGER, json_string(manager.c_str()));
    json_object_set_new(pBody, TIMEOUT, json_integer(timeout.count()));

    xmlFree(pConfig);

    char* zBody = json_dumps(pBody, 0);
    json_decref(pBody);

    string body(zBody);
    MXS_FREE(zBody);

    return body;
}

std::string config_set_cluster_mode(ClusterMode mode,
                                    int revision,
                                    const std::string& manager,
                                    const std::chrono::seconds& timeout)
{
    std::ostringstream body;
    body << "{"
         << "\"" << CLUSTER_MODE << "\": " << "\"" << cs::to_string(mode) << "\", "
         << "\"" << REVISION << "\": " << revision << ","
         << "\"" << TIMEOUT << "\": " << timeout.count() << ","
         << "\"" << MANAGER << "\": " << "\"" << manager << "\""
         << "}";

    return body.str();
}

string shutdown(const std::chrono::seconds& timeout)
{
    std::ostringstream body;
    body << "{"
         << "\"" << TIMEOUT << "\": " << timeout.count()
         << "}";

    return body.str();
}

}
}
