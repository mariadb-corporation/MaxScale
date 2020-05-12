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

const char* to_version_string(Version version)
{
    switch (version)
    {
    case CS_10:
        return "1.0";

    case CS_12:
        return "1.2";

    case CS_15:
        return "1.5";

    case CS_UNKNOWN:
        return "unknown";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

const char* to_config_string(Version version)
{
    switch (version)
    {
    case CS_10:
        return CS_10_CONFIG_STRING;

    case CS_12:
        return CS_12_CONFIG_STRING;

    case CS_15:
        return CS_15_CONFIG_STRING;

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
    case READ_ONLY:
        return "readonly";

    case READ_WRITE:
        return "readwrite";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

bool from_string(const char* zCluster_mode, ClusterMode* pCluster_mode)
{
    bool rv = true;

    if (strcmp(zCluster_mode, "readonly") == 0
        || strcmp(zCluster_mode, "read_only") == 0
        || strcmp(zCluster_mode, "readonly") == 0)
    {
        *pCluster_mode = READ_ONLY;
    }
    else if (strcmp(zCluster_mode, "read-write") == 0
             || strcmp(zCluster_mode, "read_write") == 0
             || strcmp(zCluster_mode, "readwrite") == 0)
    {
        *pCluster_mode = READ_WRITE;
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

int update(xmlNodeSet* pNodes, const char* zNew_value, const char* zIf_value, UpdateWhen update_when)
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

int update(xmlXPathContext& xpathContext,
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
        n = update(pXpath_object->nodesetval, zNew_value, zIf_value, update_when);
        xmlXPathFreeObject(pXpath_object);
    }

    return n;
}

}

int update_if(xmlDoc& xmlDoc, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    int n = -1;

    xmlXPathContext* pXpath_context = xmlXPathNewContext(&xmlDoc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = update(*pXpath_context, zXpath, zNew_value, zIf_value, UpdateWhen::IF);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

int update_if_not(xmlDoc& xmlDoc, const char* zXpath, const char* zNew_value, const char* zIf_value)
{
    int n = -1;

    xmlXPathContext* pXpath_context = xmlXPathNewContext(&xmlDoc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = update(*pXpath_context, zXpath, zNew_value, zIf_value, UpdateWhen::IF_NOT);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

bool insert(xmlDoc& xmlDoc, const char* zKey, const char* zValue, XmlLocation location)
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

int upsert(xmlDoc& xmlDoc, const char* zXpath, const char* zValue, XmlLocation location)
{
    int rv = update_if(xmlDoc, zXpath, zValue);

    if (rv == 0)
    {
        // We assume zXpath is like "/key" or "//key".
        string key(zXpath);
        auto pos = key.find_last_of("/");

        if (pos != string::npos)
        {
            key = key.substr(pos);
        }

        if (insert(xmlDoc, key.c_str(), zValue, location))
        {
            rv = 1;
        }
    }

    return rv;
}

namespace
{

int remove(xmlNodeSet* pNodes)
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

int remove(xmlXPathContext& xpathContext, const char* zXpath)
{
    int n = -1;

    xmlXPathObject* pXpath_object = xmlXPathEvalExpression(reinterpret_cast<const xmlChar*>(zXpath),
                                                           &xpathContext);
    mxb_assert(pXpath_object);

    if (pXpath_object)
    {
        n = remove(pXpath_object->nodesetval);
        xmlXPathFreeObject(pXpath_object);
    }

    return n;
}

}

int remove(xmlDoc& xmlDoc, const char* zXpath)
{
    int n = -1;
    xmlXPathContext* pXpath_context = xmlXPathNewContext(&xmlDoc);
    mxb_assert(pXpath_context);

    if (pXpath_context)
    {
        n = remove(*pXpath_context, zXpath);
        xmlXPathFreeContext(pXpath_context);
    }

    return n;
}

std::string rest::create_url(const SERVER& server,
                             int64_t port,
                             const std::string& rest_base,
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

}
