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

bool dbroots_from_array(json_t* pArray, DbRoots* pDbroots)
{
    bool rv = json_is_array(pArray);

    if (rv)
    {
        DbRoots dbroots;

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

bool services_from_array(json_t* pArray, Services* pServices)
{
    bool rv = json_is_array(pArray);

    if (rv)
    {
        Services services;

        size_t i;
        json_t* pService;
        json_array_foreach(pArray, i, pService)
        {
            const char* zName;
            json_t* pPid;
            json_object_foreach(pService, zName, pPid)
            {
                int pid = json_integer_value(pPid);
                services.emplace_back(zName, pid);
            }
        }

        pServices->swap(services);
    }

    return rv;
}

namespace
{

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

int xml_update(xmlXPathContext& xpathContext,
               const char* zXpath,
               const char* zNew_value,
               const char* zIf_value,
               UpdateWhen update_when)
{
    int n = -1;

    xmlXPathObject* pXpath_object = xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(zXpath),
                                                           &xpathContext);
    mxb_assert(pXpath_object);

    if (pXpath_object)
    {
        n = xml_update(pXpath_object->nodesetval, zNew_value, zIf_value, update_when);
        xmlXPathFreeObject(pXpath_object);
    }

    return n;
}

}

int xml::update_if(xmlDoc& xmlDoc, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    int n = -1;

    xmlXPathContext* pXpath_context = xmlXPathNewContext(&xmlDoc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = xml_update(*pXpath_context, zXpath, zNew_value, zIf_value, UpdateWhen::IF);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

int xml::update_if_not(xmlDoc& xmlDoc, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    int n = -1;

    xmlXPathContext* pXpath_context = xmlXPathNewContext(&xmlDoc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = xml_update(*pXpath_context, zXpath, zNew_value, zIf_value, UpdateWhen::IF_NOT);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

bool xml::insert(xmlDoc& xmlDoc, const char* zKey, const char* zValue, XmlLocation location)
{
    bool rv = false;

    xmlNode* pRoot = xmlDocGetRootElement(&xmlDoc);

    if (pRoot)
    {
        xmlNode* pChild = xmlNewNode(NULL, reinterpret_cast<const xmlChar*>(zKey));
        xmlNode* pContent = xmlNewText(reinterpret_cast<const xmlChar*>(zValue));
        xmlAddChild(pChild, pContent);

        xmlNode* pSibling = pRoot->xmlChildrenNode;

        if (location == XmlLocation::AT_BEGINNING && pSibling)
        {
            xmlAddPrevSibling(pSibling, pChild);
            // TODO: Sniff the indentation from the document.
            xmlNode* pLinebreak = xmlNewText(reinterpret_cast<const xmlChar*>("\n    "));
            xmlAddPrevSibling(pChild, pLinebreak);
        }
        else
        {
            // TODO: Sniff the indentation from the document.
            xmlNode* pSpace = xmlNewText(reinterpret_cast<const xmlChar*>("    "));
            xmlNode* pLinebreak = xmlNewText(reinterpret_cast<const xmlChar*>("\n"));
            xmlAddChild(pRoot, pChild);
            xmlAddPrevSibling(pChild, pSpace);
            xmlAddNextSibling(pChild, pLinebreak);
        }

        rv = true;
    }

    return rv;
}

int xml::upsert(xmlDoc& xmlDoc, const char* zXpath, const char* zValue, XmlLocation location)
{
    int rv = xml::update_if(xmlDoc, zXpath, zValue);

    if (rv == 0)
    {
        // We assume zXpath is like "/key" or "//key".
        string key(zXpath);
        auto pos = key.find_last_of("/");

        if (pos != string::npos)
        {
            key = key.substr(pos);
        }

        if (xml::insert(xmlDoc, key.c_str(), zValue, location))
        {
            rv = 1;
        }
    }

    return rv;
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

        xmlUnlinkNode(pNode);
        xmlFreeNode(pNode);
    }

    return nNodes;
}

int xml_remove(xmlXPathContext& xpathContext, const char* zXpath)
{
    int n = -1;

    xmlXPathObject* pXpath_object = xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(zXpath),
                                                           &xpathContext);
    mxb_assert(pXpath_object);

    if (pXpath_object)
    {
        n = xml_remove(pXpath_object->nodesetval);
        xmlXPathFreeObject(pXpath_object);
    }

    return n;
}

}

int xml::remove(xmlDoc& xmlDoc, const char* zXpath)
{
    int n = -1;
    xmlXPathContext* pXpath_context = xmlXPathNewContext(&xmlDoc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = xml_remove(*pXpath_context, zXpath);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

void xml::convert_to_first_multi_node(xmlDoc& xmlDoc,
                                      const string& manager,
                                      const string& server_address)
{
    // Ensure there is a "ClusterManager" key whose value is 'manager'.
    xml::upsert(xmlDoc, CLUSTERMANAGER, manager.c_str());

    // Replace all "IPAddr" values, irrespective of where they occur, with 'server_address'
    // if the current value is "127.0.0.1".
    int n = xml::update_if(xmlDoc, XPATH_IPADDR, server_address.c_str(), "127.0.0.1");
    mxb_assert(n >= 0);
}

void xml::convert_to_single_node(xmlDoc& xmlDoc)
{
    int n;
    // Remove the "ClusterManager" key.
    n = xml::remove(xmlDoc, XPATH_CLUSTERMANAGER);
    mxb_assert(n == 1);
    // Replace all "IPAddr" values, irrespective of where they occur, with "127.0.0.1", provided
    // the current value is not "0.0.0.0".
    n = xml::update_if_not(xmlDoc, XPATH_IPADDR, "127.0.0.1", "0.0.0.0");
    mxb_assert(n >= 0);
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

namespace
{

string create_config_body(xmlDoc& xmlDoc,
                          int revision,
                          const string& manager,
                          const std::chrono::seconds& timeout)
{
    xmlChar* pConfig = nullptr;
    int size = 0;

    xmlDocDumpMemory(&xmlDoc, &pConfig, &size);

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

}

string config_first_multi_node(xmlDoc& xmlDoc,
                               int revision,
                               const string& manager,
                               const string& server_address,
                               const std::chrono::seconds& timeout)
{
    xml::convert_to_first_multi_node(xmlDoc, manager, server_address);

    return create_config_body(xmlDoc, revision, manager, timeout);
}

string config_reset_node(xmlDoc& xmlDoc,
                         int revision,
                         const std::string& manager,
                         const std::chrono::seconds& timeout)
{
    xml::convert_to_single_node(xmlDoc);

    return create_config_body(xmlDoc, revision, manager, timeout);
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
